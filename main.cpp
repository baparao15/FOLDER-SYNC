#include <chrono>
#include <filesystem>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <sstream>
#include <iomanip>

#include "shared/file_handler.h"
#include "server/receiver.h"
#include "server/web_server.h"
#include "shared/config.h"
#include "shared/logger.h"
#include "shared/utils.h"
#include "shared/browser_launcher.h"
#include "client/watcher.h"
#include "client/sender.h"
#include "shared/thread_safe_queue.h"

int main(int argc, char* argv[]) {
    // Load configuration
    sync::Config config;
    try {
        config = sync::Config::loadOrDefault("config.json");
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to load config: " + std::string(ex.what()));
        config = sync::Config::loadOrDefault("config.json");
    }
    
    // Override web server port from command line if provided
    if (argc > 1) {
        try {
            int portArg = std::stoi(argv[1]);
            if (portArg > 0 && portArg < 65536) {
                config.webServerPort = portArg;
                LOG_INFO("Using web server port from command line: " + std::to_string(portArg));
            } else {
                LOG_ERROR("Invalid port number. Using default from config.");
            }
        } catch (const std::exception& ex) {
            LOG_ERROR("Invalid port argument. Using default from config.");
        }
    }
    
    // Setup logger
    sync::LogLevel logLevel = sync::LogLevel::Info;
    if (config.logLevel == "DEBUG") logLevel = sync::LogLevel::Debug;
    else if (config.logLevel == "WARNING") logLevel = sync::LogLevel::Warning;
    else if (config.logLevel == "ERROR") logLevel = sync::LogLevel::Error;
    
    sync::Logger::getInstance().setLevel(logLevel);
    sync::Logger::getInstance().setLogToFile(config.logToFile, config.logFilePath);
    
    LOG_INFO("=== Unified Folder Sync Application ===");
    LOG_INFO("Web interface port: " + std::to_string(config.webServerPort));
    
    // Construct absolute path to web directory
    std::filesystem::path webPath = std::filesystem::current_path() / "web";
    LOG_INFO("Web directory path: " + webPath.string());
    
    // Initialize web server
    sync::WebServer webServer(config.webServerPort, webPath.string());
    
    // Shared state
    std::atomic<bool> running{true};
    std::atomic<bool> syncStarted{false};
    std::string selectedRole = "unset";
    std::filesystem::path syncPath;
    std::string serverHost;
    std::uint16_t serverPort = 8080;
    
    std::mutex startMutex;
    std::condition_variable startCV;
    
    // Set up web server callbacks
    webServer.setRoleCallback([&](const std::string& role) {
        LOG_INFO("Role selected: " + role);
        selectedRole = role;
    });
    
    webServer.setStartCallback([&](const std::string& role, const std::string& folder, 
                                     const std::string& serverIP, std::uint16_t sPort) {
        LOG_INFO("Starting sync - Role: " + role + ", Folder: " + folder);
        
        std::lock_guard<std::mutex> lock(startMutex);
        selectedRole = role;
        syncPath = folder;
        serverHost = serverIP.empty() ? config.serverHost : serverIP;
        serverPort = sPort;
        syncStarted = true;
        
        webServer.addLog("Configuration set - Role: " + role);
        webServer.addLog("Sync folder: " + folder);
        if (role == "client") {
            webServer.addLog("Server: " + serverHost + ":" + std::to_string(serverPort));
        }
        
        startCV.notify_one();
    });
    
    webServer.setConfigCallback([&](const std::string& folder) {
        LOG_INFO("Configuration updated via web: " + folder);
        syncPath = folder;
        sync::ensureDirectory(syncPath);
        webServer.addLog("Sync folder updated to: " + folder);
    });
    
    // Start web server
    if (!webServer.start()) {
        LOG_ERROR("Failed to start web server");
        return 1;
    }
    
    LOG_INFO("Web server started successfully");
    LOG_INFO("Access web interface at: http://localhost:" + std::to_string(config.webServerPort));
    webServer.addLog("Application started - Please select your role");
    
    // Auto-launch browser
    if (config.autoLaunchBrowser) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::string url = "http://localhost:" + std::to_string(config.webServerPort);
        if (sync::launchBrowser(url)) {
            LOG_INFO("Browser launched to: " + url);
        }
    }
    
    // Update initial status - always start fresh (not initialized)
    sync::SyncStatus status;
    status.isRunning = false;
    webServer.updateStatus(status);
    
    LOG_INFO("Waiting for role selection from web interface...");
    
    // Wait for user to select role and start sync
    {
        std::unique_lock<std::mutex> lock(startMutex);
        startCV.wait(lock, [&]{ return syncStarted.load(); });
    }
    
    LOG_INFO("Sync initialization started with role: " + selectedRole);
    
    // Ensure sync directory exists
    sync::ensureDirectory(syncPath);
    
    // Start sync based on selected role
    if (selectedRole == "server") {
        // SERVER MODE
        const std::uint32_t serverPeerId = 2;
        
        FileHandler handler(syncPath);
        NetworkReceiver receiver(config.serverPort);
        DirectoryWatcher watcher(syncPath);
        
        sync::ThreadSafeQueue<sync::FileEvent> incomingQueue;
        sync::ThreadSafeQueue<sync::FileEvent> outgoingQueue;
        
        LOG_INFO("=== Server Mode ===");
        LOG_INFO("Sync folder: " + syncPath.string());
        LOG_INFO("Listening on port: " + std::to_string(config.serverPort));
        
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
        
        // Connection handling
        auto waitForClient = [&]() {
            LOG_INFO("Waiting for client connection...");
            webServer.addLog("Waiting for client connection...");
            while (!receiver.open()) {
                LOG_ERROR("Failed to start network listener. Retrying in 1s...");
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
                    if (event.peerId != serverPeerId) {
                        incomingQueue.push(std::move(event));
                    }
                } else {
                    LOG_WARNING("Client disconnected. Waiting for reconnection...");
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
                    if (!receiver.send(event)) {
                        LOG_WARNING("Failed to send event. Retrying...");
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    } else {
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
            // Process incoming events
            while (auto eventOpt = incomingQueue.tryPop()) {
                auto& event = *eventOpt;
                LOG_INFO("Applying remote event: " + event.relativePath);
                webServer.addLog("Received: " + event.relativePath);
                if (handler.apply(event)) {
                    watcher.markPathAsSynced(event.relativePath, event.type == sync::OperationType::Delete);
                    status.totalFilesSynced++;
                    status.totalBytesSynced += event.payload.size();
                }
            }
            
            // Collect local changes
            const auto events = watcher.collectChanges();
            if (!events.empty()) {
                LOG_INFO("Server found " + std::to_string(events.size()) + " local change(s)");
                webServer.addLog("Detected " + std::to_string(events.size()) + " local changes");
                for (const auto& ev : events) {
                    outgoingQueue.push(ev);
                }
            }
            
            // Update web status periodically
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
        
        receiverThread.join();
        senderThread.join();
        
    } else if (selectedRole == "client") {
        // CLIENT MODE
        const std::uint32_t localPeerId = 1;
        const auto interval = std::chrono::seconds(config.syncIntervalSeconds);
        
        DirectoryWatcher watcher(syncPath);
        NetworkSender sender(serverHost, serverPort);
        FileHandler handler(syncPath);
        
        LOG_INFO("=== Client Mode ===");
        LOG_INFO("Sync folder: " + syncPath.string());
        LOG_INFO("Server: " + serverHost + ":" + std::to_string(serverPort));
        
        status.isRunning = true;
        status.syncFolder = syncPath.string();
        webServer.updateStatus(status);
        
        // Try to connect
        LOG_INFO("Connecting to server...");
        webServer.addLog("Connecting to server: " + serverHost + ":" + std::to_string(serverPort));
        
        int retryCount = 0;
        while (!sender.connect()) {
            retryCount++;
            LOG_WARNING("Failed to connect (attempt " + std::to_string(retryCount) + ")");
            webServer.addLog("Connection attempt " + std::to_string(retryCount) + " failed");
            if (retryCount >= 5) {
                LOG_ERROR("Cannot connect after 5 attempts");
                webServer.addLog("ERROR: Cannot connect to server");
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        
        LOG_INFO("Connected to server successfully!");
        webServer.addLog("Connected to server successfully!");
        
        // Receiver thread
        std::thread receiverThread([&](){
            while (running) {
                sync::FileEvent incoming;
                if (sender.receive(incoming)) {
                    if (incoming.peerId == localPeerId) {
                        continue;
                    }
                    LOG_INFO("Received event from server: " + incoming.relativePath);
                    webServer.addLog("Received: " + incoming.relativePath);
                    if (handler.apply(incoming)) {
                        watcher.markPathAsSynced(incoming.relativePath,
                            incoming.type == sync::OperationType::Delete);
                        status.totalFilesSynced++;
                        status.totalBytesSynced += incoming.payload.size();
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            }
        });
        receiverThread.detach();
        
        // Main loop
        long long updateCounter = 0;
        while (running) {
            const auto events = watcher.collectChanges();
            if (!events.empty()) {
                LOG_INFO("Found " + std::to_string(events.size()) + " local change(s)");
                webServer.addLog("Sending " + std::to_string(events.size()) + " changes");
                
                auto localEvents = events;
                for (auto& event : localEvents) {
                    event.peerId = localPeerId;
                }
                
                if (!sender.sendBatch(localEvents)) {
                    LOG_ERROR("Failed to send events. Reconnecting...");
                    webServer.addLog("Connection lost - attempting to reconnect");
                    if (sender.connect()) {
                        LOG_INFO("Reconnected successfully");
                        webServer.addLog("Reconnected to server");
                    }
                } else {
                    status.totalFilesSynced += localEvents.size();
                }
            }
            
            // Update web status periodically
            updateCounter++;
            if (updateCounter % 10 == 0) {
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                std::ostringstream oss;
                oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
                status.lastSyncTime = oss.str();
                webServer.updateStatus(status);
            }
            
            std::this_thread::sleep_for(interval);
        }
    }
    
    webServer.stop();
    return 0;
}
