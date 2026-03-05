#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

namespace sync {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

class Logger {
public:
    static Logger& getInstance();
    
    void setLevel(LogLevel level);
    void setLogToFile(bool enabled, const std::string& filePath = "");
    
    void log(LogLevel level, const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    void writeLog(LogLevel level, const std::string& message);
    std::string levelToString(LogLevel level) const;
    std::string getTimestamp() const;
    
    LogLevel currentLevel_{LogLevel::Info};
    bool logToFile_{false};
    std::unique_ptr<std::ofstream> logFile_;
    std::mutex logMutex_;
};

// Convenience macros
#define LOG_DEBUG(msg) sync::Logger::getInstance().debug(msg)
#define LOG_INFO(msg) sync::Logger::getInstance().info(msg)
#define LOG_WARNING(msg) sync::Logger::getInstance().warning(msg)
#define LOG_ERROR(msg) sync::Logger::getInstance().error(msg)

} // namespace sync

