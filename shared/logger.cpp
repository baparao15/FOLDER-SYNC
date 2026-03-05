#include "shared/logger.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace sync {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(logMutex_);
    currentLevel_ = level;
}

void Logger::setLogToFile(bool enabled, const std::string& filePath) {
    std::lock_guard<std::mutex> lock(logMutex_);
    logToFile_ = enabled;
    if (enabled && !filePath.empty()) {
        logFile_ = std::make_unique<std::ofstream>(filePath, std::ios::app);
        if (!logFile_->is_open()) {
            logToFile_ = false;
            std::cerr << "Warning: Failed to open log file: " << filePath << std::endl;
        }
    } else {
        logFile_.reset();
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level >= currentLevel_) {
        writeLog(level, message);
    }
}

void Logger::debug(const std::string& message) {
    log(LogLevel::Debug, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::Info, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::Warning, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::Error, message);
}

void Logger::writeLog(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex_);
    
    std::string logLine = "[" + getTimestamp() + "] [" + levelToString(level) + "] " + message;
    
    // Write to console
    if (level >= LogLevel::Warning) {
        std::cerr << logLine << std::endl;
    } else {
        std::cout << logLine << std::endl;
    }
    
    // Write to file if enabled
    if (logToFile_ && logFile_ && logFile_->is_open()) {
        *logFile_ << logLine << std::endl;
        logFile_->flush();
    }
}

std::string Logger::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string Logger::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

} // namespace sync

