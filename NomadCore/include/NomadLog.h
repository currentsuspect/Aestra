// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <mutex>
#include <memory>
#include <sstream>
#include <ctime>
#include <iomanip>
#include "NomadThreading.h"
#include <thread>
#include <atomic>
#include <cstring>

namespace Nomad {

// =============================================================================
// Log Levels
// =============================================================================
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

// =============================================================================
// Logger Interface
// =============================================================================
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel level, const std::string& message) = 0;
    virtual void setLevel(LogLevel level) = 0;
    virtual LogLevel getLevel() const = 0;
};

// =============================================================================
// Console Logger
// =============================================================================
class ConsoleLogger : public ILogger {
public:
    ConsoleLogger(LogLevel minLevel = LogLevel::Info) : minLevel_(minLevel) {}

    void log(LogLevel level, const std::string& message) override {
        if (level < minLevel_) return;

        std::lock_guard<std::mutex> lock(mutex_);
        
        std::cout << "[" << getTimestamp() << "] ";
        std::cout << getLevelString(level) << " ";
        std::cout << message << std::endl;
    }

    void setLevel(LogLevel level) override {
        minLevel_ = level;
    }

    LogLevel getLevel() const override {
        return minLevel_;
    }

private:
    LogLevel minLevel_;
    std::mutex mutex_;

    std::string getTimestamp() const {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::stringstream ss;
        ss << std::put_time(&tm, "%H:%M:%S");
        return ss.str();
    }

    std::string getLevelString(LogLevel level) const {
        switch (level) {
            case LogLevel::Debug:   return "[DEBUG]";
            case LogLevel::Info:    return "[INFO] ";
            case LogLevel::Warning: return "[WARN] ";
            case LogLevel::Error:   return "[ERROR]";
            default:                return "[?????]";
        }
    }
};

// =============================================================================
// File Logger
// =============================================================================
class FileLogger : public ILogger {
public:
    FileLogger(const std::string& filename, LogLevel minLevel = LogLevel::Info)
        : minLevel_(minLevel), filename_(filename) {
        file_.open(filename, std::ios::out | std::ios::app);
    }

    ~FileLogger() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    void log(LogLevel level, const std::string& message) override {
        if (level < minLevel_) return;
        if (!file_.is_open()) return;

        std::lock_guard<std::mutex> lock(mutex_);
        
        file_ << "[" << getTimestamp() << "] ";
        file_ << getLevelString(level) << " ";
        file_ << message << std::endl;
        file_.flush();
    }

    void setLevel(LogLevel level) override {
        minLevel_ = level;
    }

    LogLevel getLevel() const override {
        return minLevel_;
    }

    bool isOpen() const {
        return file_.is_open();
    }

private:
    LogLevel minLevel_;
    std::string filename_;
    std::ofstream file_;
    std::mutex mutex_;

    std::string getTimestamp() const {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    std::string getLevelString(LogLevel level) const {
        switch (level) {
            case LogLevel::Debug:   return "[DEBUG]";
            case LogLevel::Info:    return "[INFO] ";
            case LogLevel::Warning: return "[WARN] ";
            case LogLevel::Error:   return "[ERROR]";
            default:                return "[?????]";
        }
    }
};

// =============================================================================
// Multi-Logger (logs to multiple destinations)
// =============================================================================
class MultiLogger : public ILogger {
public:
    MultiLogger(LogLevel minLevel = LogLevel::Info) : minLevel_(minLevel) {}

    void addLogger(std::shared_ptr<ILogger> logger) {
        std::lock_guard<std::mutex> lock(mutex_);
        loggers_.push_back(logger);
    }

    void log(LogLevel level, const std::string& message) override {
        if (level < minLevel_) return;

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& logger : loggers_) {
            logger->log(level, message);
        }
    }

    void setLevel(LogLevel level) override {
        minLevel_ = level;
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& logger : loggers_) {
            logger->setLevel(level);
        }
    }

    LogLevel getLevel() const override {
        return minLevel_;
    }

private:
    LogLevel minLevel_;
    std::vector<std::shared_ptr<ILogger>> loggers_;
    std::mutex mutex_;
};

// =============================================================================
// Global Logger Instance
// =============================================================================
class Log {
public:
    static void init(std::shared_ptr<ILogger> logger) {
        instance().logger_ = logger;
    }

    static void debug(const std::string& message) {
        if (instance().logger_) {
            instance().logger_->log(LogLevel::Debug, message);
        }
    }

    static void info(const std::string& message) {
        if (instance().logger_) {
            instance().logger_->log(LogLevel::Info, message);
        }
    }

    static void warning(const std::string& message) {
        if (instance().logger_) {
            instance().logger_->log(LogLevel::Warning, message);
        }
    }

    static void error(const std::string& message) {
        if (instance().logger_) {
            instance().logger_->log(LogLevel::Error, message);
        }
    }

    static void setLevel(LogLevel level) {
        if (instance().logger_) {
            instance().logger_->setLevel(level);
        }
    }

    static std::shared_ptr<ILogger> getLogger() {
        return instance().logger_;
    }

private:
    Log() {
        // Default to console logger
        logger_ = std::make_shared<ConsoleLogger>();
    }

    static Log& instance() {
        static Log log;
        return log;
    }

    std::shared_ptr<ILogger> logger_;
};

// =============================================================================
// Convenience Macros
// =============================================================================
#define NOMAD_LOG_DEBUG(msg)   Nomad::Log::debug(msg)
#define NOMAD_LOG_INFO(msg)    Nomad::Log::info(msg)
#define NOMAD_LOG_WARNING(msg) Nomad::Log::warning(msg)
#define NOMAD_LOG_ERROR(msg)   Nomad::Log::error(msg)

// Stream-style logging
#define NOMAD_LOG_STREAM_DEBUG   Nomad::LogStream(Nomad::LogLevel::Debug)
#define NOMAD_LOG_STREAM_INFO    Nomad::LogStream(Nomad::LogLevel::Info)
#define NOMAD_LOG_STREAM_WARNING Nomad::LogStream(Nomad::LogLevel::Warning)
#define NOMAD_LOG_STREAM_ERROR   Nomad::LogStream(Nomad::LogLevel::Error)

// =============================================================================
// Stream-style Logger Helper
// =============================================================================
class LogStream {
public:
    LogStream(LogLevel level) : level_(level) {}

    ~LogStream() {
        Log::getLogger()->log(level_, stream_.str());
    }

    template<typename T>
    LogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

private:
    LogLevel level_;
    std::stringstream stream_;
};

// =============================================================================
// Async Logger (Audio-Thread Safe)
// =============================================================================
struct LogMessage {
    LogLevel level;
    char message[256]; // Fixed size for ring buffer
};

class AsyncLogger : public ILogger {
public:
    AsyncLogger(std::shared_ptr<ILogger> target) 
        : target_(target), running_(true) {
        worker_ = std::thread(&AsyncLogger::processQueue, this);
    }

    ~AsyncLogger() {
        running_ = false;
        if (worker_.joinable()) {
            worker_.join();
        }
        // Drain remaining
        processQueueSinglePass();
    }

    void log(LogLevel level, const std::string& message) override {
        // Truncate if necessary
        LogMessage msg;
        msg.level = level;
        
        size_t len = message.length();
        if (len >= sizeof(msg.message)) len = sizeof(msg.message) - 1;
        
        std::memcpy(msg.message, message.c_str(), len);
        msg.message[len] = '\0';

        if (!queue_.push(msg)) {
            // Drop or fallback? For audio thread safety, we drop.
            // printf("Log dropped!\n"); 
        }
    }

    void setLevel(LogLevel level) override {
        if (target_) target_->setLevel(level);
    }

    LogLevel getLevel() const override {
        return target_ ? target_->getLevel() : LogLevel::Info;
    }

private:
    std::shared_ptr<ILogger> target_;
    Nomad::LockFreeRingBuffer<LogMessage, 1024> queue_;
    std::thread worker_;
    std::atomic<bool> running_;

    void processQueue() {
        while (running_) {
            processQueueSinglePass();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void processQueueSinglePass() {
        LogMessage msg;
        while (queue_.pop(msg)) {
            if (target_) {
                target_->log(msg.level, msg.message);
            }
        }
    }
};
} // namespace Nomad
