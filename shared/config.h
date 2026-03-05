#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace sync {

struct Config {
    std::filesystem::path syncFolder;
    int syncIntervalSeconds{10};
    std::string logLevel{"INFO"};
    bool logToFile{false};
    std::string logFilePath{"sync.log"};
    std::string serverHost{"127.0.0.1"};
    std::uint16_t serverPort{8080};
    std::uint16_t webServerPort{8888};
    std::string role{"unset"};  // "server", "client", or "unset"
    bool autoLaunchBrowser{true};
    
    // Load configuration from JSON file
    static Config loadFromFile(const std::filesystem::path& configPath);
    
    // Load with defaults if file doesn't exist
    static Config loadOrDefault(const std::filesystem::path& configPath = "config.json");
    
    // Validate configuration
    bool validate() const;
};

} // namespace sync

