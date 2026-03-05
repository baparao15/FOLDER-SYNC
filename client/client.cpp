#include <chrono>
#include <filesystem>
#include <thread>

#include "client/sender.h"
#include "client/watcher.h"
#include "shared/file_handler.h"
#include "shared/config.h"
#include "shared/logger.h"
#include "shared/utils.h"

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
    
    const auto syncPath = config.syncFolder;
    const auto interval = std::chrono::seconds(config.syncIntervalSeconds);
    const std::uint32_t localPeerId = 1;  // Client peer ID

    sync::ensureDirectory(syncPath);

    DirectoryWatcher watcher(syncPath);
    NetworkSender sender(config.serverHost, config.serverPort);
    FileHandler handler(syncPath);

    LOG_INFO("=== Bidirectional Sync Client ===");
    LOG_INFO("Sync folder: " + syncPath.string());
    LOG_INFO("Sync interval: " + std::to_string(interval.count()) + " seconds");
    LOG_INFO("Server: " + config.serverHost + ":" + std::to_string(config.serverPort));
    
    // Try to connect initially
    LOG_INFO("Connecting to server at " + config.serverHost + ":" + std::to_string(config.serverPort) + "...");
    int retryCount = 0;
    while (!sender.connect()) {
        retryCount++;
        LOG_WARNING("Failed to connect to server (attempt " + std::to_string(retryCount) + ")");
        if (retryCount >= 3) {
            LOG_ERROR("Cannot connect after 3 attempts. Exiting.");
            return 1;
        }
        LOG_INFO("Retrying in 2 seconds...");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    LOG_INFO("Connected to server successfully!");
    LOG_INFO("Monitoring for changes...");

    // Start a background thread to receive incoming events from server
    std::thread receiverThread([&](){
        while (true) {
            sync::FileEvent incoming;
            if (sender.receive(incoming)) {
                LOG_INFO("Received event from server - Path: " + incoming.relativePath + ", PeerID: " + std::to_string(incoming.peerId) + ", Type: " + sync::operationToString(incoming.type));
                // Ignore events originating from this peer
                if (incoming.peerId == localPeerId) {
                    LOG_INFO("Ignoring event from client itself (loop prevention)");
                    continue;
                }
                LOG_INFO("Applying event from server: " + incoming.relativePath + " (" + std::to_string(incoming.payload.size()) + " bytes)");
                if (handler.apply(incoming)) {
                    // Update watcher snapshot so we don't resend this change
                    watcher.markPathAsSynced(incoming.relativePath,
                        incoming.type == sync::OperationType::Delete);
                    LOG_INFO("Successfully applied event from server");
                } else {
                    LOG_WARNING("Failed to apply event from server: " + incoming.relativePath);
                }
            } else {
                // Read failed - short sleep to avoid tight loop
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
    });
    receiverThread.detach();

    while (true) {
        // Collect local changes and send to server
        const auto events = watcher.collectChanges();
        if (!events.empty()) {
            LOG_INFO("Found " + std::to_string(events.size()) + " local change(s). Sending to server...");
            // Mark events with local peer ID
            auto localEvents = events;
            for (auto& event : localEvents) {
                event.peerId = localPeerId;
                LOG_INFO("Sending event to server - Path: " + event.relativePath + ", PeerID: " + std::to_string(event.peerId) + ", Size: " + std::to_string(event.payload.size()) + " bytes");
            }
            if (!sender.sendBatch(localEvents)) {
                LOG_ERROR("Failed to send events. Attempting to reconnect...");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (sender.connect()) {
                    LOG_INFO("Reconnected. Retrying send...");
                    if (!sender.sendBatch(localEvents)) {
                        LOG_ERROR("Still failed after reconnect. Skipping this batch.");
                    } else {
                        LOG_INFO("Successfully sent " + std::to_string(localEvents.size()) + " event(s).");
                    }
                } else {
                    LOG_ERROR("Cannot reconnect. Will retry on next sync cycle.");
                }
            } else {
                LOG_INFO("Successfully sent " + std::to_string(localEvents.size()) + " event(s).");
            }
        }
        std::this_thread::sleep_for(interval);
    }
}

