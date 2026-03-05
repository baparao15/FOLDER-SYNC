#include <chrono>
#include <filesystem>
#include <thread>
#include <atomic>

#include "shared/file_handler.h"
#include "server/receiver.h"
#include "server/web_server.h"
#include "shared/config.h"
#include "shared/logger.h"
#include "shared/utils.h"
#include "client/watcher.h"
#include "shared/thread_safe_queue.h"

int main() {
    // Load configuration
    sync::Config config;
    try {
        config = sync::Config::loadOrDefault("config.json");
        if (!config.validate()) {
            LOG_ERROR("Invalid configuration");
            return 1;
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to load config: " + std::string(ex.what()));
        LOG_INFO("Using default configuration");
        config = sync::Config::loadOrDefault("config.json");
    }
    
    // Setup logger
    sync::LogLevel logLevel = sync::LogLevel::Info;
    if (config.logLevel == "DEBUG") logLevel = sync::LogLevel::Debug;
    else if (config.logLevel == "WARNING") logLevel = sync::LogLevel::Warning;
    else if (config.logLevel == "ERROR") logLevel = sync::LogLevel::Error;
    
    sync::Logger::getInstance().setLevel(logLevel);
    sync::Logger::getInstance().setLogToFile(config.logToFile, config.logFilePath);
    
    auto syncPath = config.syncFolder;
    const std::uint32_t serverPeerId = 2;

    sync::ensureDirectory(syncPath);

    FileHandler handler(syncPath);
    NetworkReceiver receiver(config.serverPort);
    DirectoryWatcher watcher(syncPath);

    sync::ThreadSafeQueue<sync::FileEvent> incomingQueue;
    sync::ThreadSafeQueue<sync::FileEvent> outgoingQueue;
    std::atomic<bool> running{true};

    LOG_INFO("=== Directory Sync Server ===");
    LOG_INFO("Sync folder: " + syncPath.string());
    LOG_INFO("Listening on port: " + std::to_string(config.serverPort));
    LOG_INFO("Web interface on port: " + std::to_string(config.webServerPort));
    
    // Initialize web server
    sync::WebServer webServer(config.webServerPort, "web");
    
    // Set up web server callbacks
    webServer.setConfigCallback([&syncPath, &watcher, &webServer](const std::string& folder) {
        LOG_INFO("Configuration updated via web: " + folder);
        syncPath = folder;
        sync::ensureDirectory(syncPath);
        webServer.addLog("Sync folder updated to: " + folder);
    });
    
    webServer.setConnectCallback([&webServer](const std::string& ip, std::uint16_t port) {
        LOG_INFO("Web UI requested connection to: " + ip + ":" + std::to_string(port));
        webServer.addLog("Attempting to connect to client: " + ip);
        // For now, just log the request - actual connection handled by client
        return true;
    });
    
    // Start web server
    if (!webServer.start()) {
        LOG_ERROR("Failed to start web server");
    } else {
        LOG_INFO("Web server started successfully");
        LOG_INFO("Access web interface at: http://localhost:" + std::to_string(config.webServerPort));
        webServer.addLog("Server started successfully");
    }
    
    // Update initial status
    sync::SyncStatus status;
    status.isRunning = true;
    status.syncFolder = syncPath.string();
    webServer.updateStatus(status);
    
    // Function to perform initial sync of all existing files
    auto performInitialSync = [&]() {
        LOG_INFO("Performing initial sync of existing files...");
        webServer.addLog("Scanning existing files for initial sync...");
        
        try {
            if (!std::filesystem::exists(syncPath)) {
                LOG_WARNING("Sync path does not exist, skipping initial sync");
                return;
            }
            
            int fileCount = 0;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                    syncPath, std::filesystem::directory_options::skip_permission_denied)) {
                
                const auto relative = sync::relativePathOf(syncPath, entry.path());
                
                if (entry.is_directory()) {
                    // Queue directory creation
                    sync::FileEvent event;
                    event.type = sync::OperationType::Create;
                    event.relativePath = relative;
                    event.payload.clear(); // Empty payload for directory
                    event.peerId = serverPeerId;
                    outgoingQueue.push(std::move(event));
                    fileCount++;
                    LOG_INFO("Queued directory for initial sync: " + relative);
                } else if (entry.is_regular_file()) {
                    // Queue file creation
                    sync::FileEvent event;
                    event.type = sync::OperationType::Create;
                    event.relativePath = relative;
                    event.payload = sync::readFileBytes(entry.path());
                    event.peerId = serverPeerId;
                    outgoingQueue.push(std::move(event));
                    fileCount++;
                    LOG_INFO("Queued file for initial sync: " + relative + " (" + 
                             std::to_string(event.payload.size()) + " bytes)");
                }
            }
            
            LOG_INFO("Initial sync queued " + std::to_string(fileCount) + " items");
            webServer.addLog("Queued " + std::to_string(fileCount) + " existing files for sync");
        } catch (const std::exception& ex) {
            LOG_ERROR("Error during initial sync: " + std::string(ex.what()));
        }
    };
    
    // Connection handling logic
    auto waitForClient = [&]() {
        LOG_INFO("Waiting for client connection...");
        webServer.addLog("Waiting for client connection...");
        while (!receiver.open()) {
            LOG_ERROR("Failed to start network listener or accept client. Retrying in 1s...");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        LOG_INFO("Client connected!");
        webServer.addLog("Client connected successfully");
        
        // Perform initial sync after client connects
        performInitialSync();
    };

    waitForClient();

    // Receiver Thread
    std::thread receiverThread([&](){
        while (running) {
            sync::FileEvent event;
            if (receiver.next(event)) {
                LOG_INFO("Received event from client - Path: " + event.relativePath + ", PeerID: " + std::to_string(event.peerId) + ", Type: " + sync::operationToString(event.type));
                if (event.peerId != serverPeerId) {
                    LOG_INFO("Queuing event for processing (not from server)");
                    incomingQueue.push(std::move(event));
                } else {
                    LOG_INFO("Ignoring event from server itself (loop prevention)");
                }
            } else {
                LOG_WARNING("Client disconnected (Receive). Waiting for new connection...");
                webServer.addLog("Client disconnected - waiting for reconnection");
                waitForClient();
            }
        }
    });

    // Sender Thread
    std::thread senderThread([&](){
        while (running) {
            auto eventOpt = outgoingQueue.tryPop();
            if (eventOpt) {
                auto& event = *eventOpt;
                event.peerId = serverPeerId;
                LOG_INFO("Sending event to client - Path: " + event.relativePath + ", PeerID: " + std::to_string(event.peerId) + ", Size: " + std::to_string(event.payload.size()) + " bytes");
                if (!receiver.send(event)) {
                    LOG_WARNING("Failed to send event (Client disconnected?). Pushing back...");
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                } else {
                    LOG_INFO("Successfully sent event to client");
                    status.totalFilesSynced++;
                    status.totalBytesSynced += event.payload.size();
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });

    // Main Loop
    long long updateCounter = 0;
    while (running) {
        // 1. Process incoming events
        while (auto eventOpt = incomingQueue.tryPop()) {
            auto& event = *eventOpt;
            LOG_INFO("Applying remote event from client: " + event.relativePath + " (" + std::to_string(event.payload.size()) + " bytes)");
            webServer.addLog("Received from client: " + event.relativePath);
            if (handler.apply(event)) {
                watcher.markPathAsSynced(event.relativePath, event.type == sync::OperationType::Delete);
                status.totalFilesSynced++;
                status.totalBytesSynced += event.payload.size();
                LOG_INFO("Successfully applied remote event");
            } else {
                LOG_WARNING("Failed to apply remote event: " + event.relativePath);
            }
        }

        // 2. Collect local changes
        const auto events = watcher.collectChanges();
        if (!events.empty()) {
            LOG_INFO("Server found " + std::to_string(events.size()) + " local change(s). Queuing...");
            webServer.addLog("Detected " + std::to_string(events.size()) + " local changes");
            for (const auto& ev : events) {
                outgoingQueue.push(ev);
            }
        }

        // 3. Update web status periodically
        updateCounter++;
        if (updateCounter % 10 == 0) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
            status.lastSyncTime = oss.str();
            status.syncFolder = syncPath.string();
            webServer.updateStatus(status);
        }

        std::this_thread::sleep_for(std::chrono::seconds(config.syncIntervalSeconds));
    }

    webServer.stop();
    receiverThread.join();
    senderThread.join();
    return 0;
}

