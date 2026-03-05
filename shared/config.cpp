#include "shared/config.h"

#include <fstream>
#include <sstream>
#include <string>

namespace sync {

// Helper function to extract string value from JSON
static std::string extractJsonString(const std::string& line, const std::string& key) {
    size_t keyPos = line.find("\"" + key + "\"");
    if (keyPos == std::string::npos) return "";
    
    size_t colonPos = line.find(':', keyPos);
    if (colonPos == std::string::npos) return "";
    
    size_t quoteStart = line.find('"', colonPos);
    if (quoteStart == std::string::npos) return "";
    quoteStart++; // Skip opening quote
    
    size_t quoteEnd = line.find('"', quoteStart);
    if (quoteEnd == std::string::npos) return "";
    
    std::string value = line.substr(quoteStart, quoteEnd - quoteStart);
    
    // Unescape backslashes
    std::string unescaped;
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '\\' && i + 1 < value.length()) {
            if (value[i + 1] == '\\') {
                unescaped += '\\';
                ++i;
            } else if (value[i + 1] == 'n') {
                unescaped += '\n';
                ++i;
            } else {
                unescaped += value[i];
            }
        } else {
            unescaped += value[i];
        }
    }
    return unescaped;
}

// Helper function to extract number value from JSON
static int extractJsonInt(const std::string& line, const std::string& key) {
    size_t keyPos = line.find("\"" + key + "\"");
    if (keyPos == std::string::npos) return 0;
    
    size_t colonPos = line.find(':', keyPos);
    if (colonPos == std::string::npos) return 0;
    
    colonPos++; // Skip colon
    while (colonPos < line.length() && (line[colonPos] == ' ' || line[colonPos] == '\t')) {
        colonPos++;
    }
    
    size_t end = colonPos;
    while (end < line.length() && line[end] != ',' && line[end] != '}' && line[end] != '\n' && line[end] != '\r') {
        end++;
    }
    
    std::string valueStr = line.substr(colonPos, end - colonPos);
    try {
        return std::stoi(valueStr);
    } catch (...) {
        return 0;
    }
}

// Helper function to extract boolean value from JSON
static bool extractJsonBool(const std::string& line, const std::string& key) {
    size_t keyPos = line.find("\"" + key + "\"");
    if (keyPos == std::string::npos) return false;
    
    size_t colonPos = line.find(':', keyPos);
    if (colonPos == std::string::npos) return false;
    
    colonPos++; // Skip colon
    while (colonPos < line.length() && (line[colonPos] == ' ' || line[colonPos] == '\t')) {
        colonPos++;
    }
    
    std::string value = line.substr(colonPos, 10); // Check for "true" or "false"
    return value.find("true") != std::string::npos;
}

Config Config::loadFromFile(const std::filesystem::path& configPath) {
    Config config;
    
    if (!std::filesystem::exists(configPath)) {
        throw std::runtime_error("Config file not found: " + configPath.string());
    }
    
    std::ifstream file(configPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + configPath.string());
    }
    
    // Read entire file
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Extract values
    std::string syncFolder = extractJsonString(content, "sync_folder");
    if (!syncFolder.empty()) {
        config.syncFolder = syncFolder;
    }
    // Also support dest_folder as legacy option
    if (config.syncFolder.empty()) {
        std::string destFolder = extractJsonString(content, "dest_folder");
        if (!destFolder.empty()) {
            config.syncFolder = destFolder;
        }
    }
    
    int interval = extractJsonInt(content, "sync_interval_seconds");
    if (interval > 0) {
        config.syncIntervalSeconds = interval;
    }
    
    std::string logLevel = extractJsonString(content, "log_level");
    if (!logLevel.empty()) {
        config.logLevel = logLevel;
    }
    
    config.logToFile = extractJsonBool(content, "log_to_file");
    
    std::string logPath = extractJsonString(content, "log_file_path");
    if (!logPath.empty()) {
        config.logFilePath = logPath;
    }
    
    std::string host = extractJsonString(content, "server_host");
    if (!host.empty()) {
        config.serverHost = host;
    }
    
    int port = extractJsonInt(content, "server_port");
    if (port > 0 && port <= 65535) {
        config.serverPort = static_cast<std::uint16_t>(port);
    }
    
    int webPort = extractJsonInt(content, "web_server_port");
    if (webPort > 0 && webPort <= 65535) {
        config.webServerPort = static_cast<std::uint16_t>(webPort);
    }
    
    std::string role = extractJsonString(content, "role");
    if (!role.empty()) {
        config.role = role;
    }
    
    // Check for auto_launch_browser (default is true)
    std::string autoLaunchStr = content;
    if (autoLaunchStr.find("\"auto_launch_browser\"") != std::string::npos) {
        config.autoLaunchBrowser = extractJsonBool(content, "auto_launch_browser");
    }
    
    return config;
}

Config Config::loadOrDefault(const std::filesystem::path& configPath) {
    try {
        return loadFromFile(configPath);
    } catch (const std::exception& ex) {
        // Return default config if file doesn't exist or can't be read
        Config defaultConfig;
        defaultConfig.syncFolder = "C:\\Users\\pendy\\Desktop\\sync";
        defaultConfig.syncIntervalSeconds = 10;
        defaultConfig.logLevel = "INFO";
        defaultConfig.logToFile = false;
        defaultConfig.logFilePath = "sync.log";
        defaultConfig.serverHost = "127.0.0.1";
        defaultConfig.serverPort = 5050;
        return defaultConfig;
    }
}

bool Config::validate() const {
    if (syncFolder.empty()) {
        return false;
    }
    if (syncIntervalSeconds <= 0) {
        return false;
    }
    if (serverPort == 0) {
        return false;
    }
    return true;
}

} // namespace sync

