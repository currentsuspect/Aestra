// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../include/AestraFile.h"
#include "../include/AestraLog.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace Aestra;

#define TEST_ASSERT(condition, message)                  \
    if (!(condition)) {                                  \
        std::cerr << "FAILED: " << message << std::endl; \
        return false;                                    \
    }

// =============================================================================
// Console Logger Tests
// =============================================================================
bool testConsoleLogger() {
    std::cout << "\nTesting ConsoleLogger..." << std::endl;

    ConsoleLogger logger(LogLevel::Debug);

    // Test all log levels
    logger.log(LogLevel::Debug, "This is a debug message");
    logger.log(LogLevel::Info, "This is an info message");
    logger.log(LogLevel::Warning, "This is a warning message");
    logger.log(LogLevel::Error, "This is an error message");

    // Test level filtering
    logger.setLevel(LogLevel::Warning);
    TEST_ASSERT(logger.getLevel() == LogLevel::Warning, "Level should be Warning");

    logger.log(LogLevel::Debug, "This debug should NOT appear");
    logger.log(LogLevel::Info, "This info should NOT appear");
    logger.log(LogLevel::Warning, "This warning SHOULD appear");
    logger.log(LogLevel::Error, "This error SHOULD appear");

    std::cout << "  âœ“ ConsoleLogger tests passed" << std::endl;
    return true;
}

// =============================================================================
// File Logger Tests
// =============================================================================
bool testFileLogger() {
    std::cout << "\nTesting FileLogger..." << std::endl;

    const std::string logFile = "test_log.txt";

    // Remove old log file if exists
    std::remove(logFile.c_str());

    {
        FileLogger logger(logFile, LogLevel::Debug);
        TEST_ASSERT(logger.isOpen(), "Log file should be open");

        logger.log(LogLevel::Debug, "Debug message");
        logger.log(LogLevel::Info, "Info message");
        logger.log(LogLevel::Warning, "Warning message");
        logger.log(LogLevel::Error, "Error message");
    } // Logger destructor closes file

    // Verify file was created and contains data
    TEST_ASSERT(File::exists(logFile), "Log file should exist");

    std::string content = File::readAllText(logFile);
    TEST_ASSERT(!content.empty(), "Log file should not be empty");
    TEST_ASSERT(content.find("Debug message") != std::string::npos, "Should contain debug message");
    TEST_ASSERT(content.find("Info message") != std::string::npos, "Should contain info message");
    TEST_ASSERT(content.find("Warning message") != std::string::npos, "Should contain warning message");
    TEST_ASSERT(content.find("Error message") != std::string::npos, "Should contain error message");

    // Test level filtering
    std::remove(logFile.c_str());
    {
        FileLogger logger(logFile, LogLevel::Error);
        logger.log(LogLevel::Debug, "Should not appear");
        logger.log(LogLevel::Info, "Should not appear");
        logger.log(LogLevel::Warning, "Should not appear");
        logger.log(LogLevel::Error, "Should appear");
    }

    content = File::readAllText(logFile);
    TEST_ASSERT(content.find("Should not appear") == std::string::npos, "Should not contain filtered messages");
    TEST_ASSERT(content.find("Should appear") != std::string::npos, "Should contain error message");

    // Cleanup
    std::remove(logFile.c_str());

    std::cout << "  âœ“ FileLogger tests passed" << std::endl;
    return true;
}

// =============================================================================
// Multi-Logger Tests
// =============================================================================
bool testMultiLogger() {
    std::cout << "\nTesting MultiLogger..." << std::endl;

    const std::string logFile = "test_multi_log.txt";
    std::remove(logFile.c_str());

    auto consoleLogger = std::make_shared<ConsoleLogger>(LogLevel::Info);
    auto fileLogger = std::make_shared<FileLogger>(logFile, LogLevel::Debug);

    MultiLogger multiLogger(LogLevel::Debug);
    multiLogger.addLogger(consoleLogger);
    multiLogger.addLogger(fileLogger);

    multiLogger.log(LogLevel::Debug, "Multi-logger debug message");
    multiLogger.log(LogLevel::Info, "Multi-logger info message");
    multiLogger.log(LogLevel::Warning, "Multi-logger warning message");
    multiLogger.log(LogLevel::Error, "Multi-logger error message");

    // Give file logger time to flush
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Verify file contains messages
    std::string content = File::readAllText(logFile);
    TEST_ASSERT(content.find("Multi-logger debug message") != std::string::npos, "File should contain debug message");
    TEST_ASSERT(content.find("Multi-logger info message") != std::string::npos, "File should contain info message");

    // Cleanup
    std::remove(logFile.c_str());

    std::cout << "  âœ“ MultiLogger tests passed" << std::endl;
    return true;
}

// =============================================================================
// Global Logger Tests
// =============================================================================
bool testGlobalLogger() {
    std::cout << "\nTesting Global Logger..." << std::endl;

    const std::string logFile = "test_global_log.txt";
    std::remove(logFile.c_str());

    // Initialize with file logger
    auto fileLogger = std::make_shared<FileLogger>(logFile, LogLevel::Debug);
    Log::init(fileLogger);

    // Test convenience functions
    Log::debug("Global debug message");
    Log::info("Global info message");
    Log::warning("Global warning message");
    Log::error("Global error message");

    // Test macros
    AESTRA_LOG_DEBUG("Macro debug message");
    AESTRA_LOG_INFO("Macro info message");
    AESTRA_LOG_WARNING("Macro warning message");
    AESTRA_LOG_ERROR("Macro error message");

    // Test stream-style logging
    AESTRA_LOG_STREAM_INFO << "Stream info: " << 42 << " " << 3.14;
    AESTRA_LOG_STREAM_WARNING << "Stream warning: "
                              << "test";

    // Give file logger time to flush
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Verify file contains messages
    std::string content = File::readAllText(logFile);
    TEST_ASSERT(content.find("Global debug message") != std::string::npos, "Should contain global debug message");
    TEST_ASSERT(content.find("Macro info message") != std::string::npos, "Should contain macro info message");
    TEST_ASSERT(content.find("Stream info: 42 3.14") != std::string::npos, "Should contain stream info message");

    // Cleanup
    std::remove(logFile.c_str());

    std::cout << "  âœ“ Global Logger tests passed" << std::endl;
    return true;
}

// =============================================================================
// Thread Safety Tests
// =============================================================================
bool testThreadSafety() {
    std::cout << "\nTesting Thread Safety..." << std::endl;

    const std::string logFile = "test_thread_log.txt";
    std::remove(logFile.c_str());

    auto fileLogger = std::make_shared<FileLogger>(logFile, LogLevel::Debug);
    Log::init(fileLogger);

    const int numThreads = 4;
    const int messagesPerThread = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([t, messagesPerThread]() {
            for (int i = 0; i < messagesPerThread; ++i) {
                AESTRA_LOG_STREAM_INFO << "Thread " << t << " message " << i;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Give file logger time to flush
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify file contains expected number of messages
    std::string content = File::readAllText(logFile);
    int messageCount = 0;
    size_t pos = 0;
    while ((pos = content.find("[INFO]", pos)) != std::string::npos) {
        messageCount++;
        pos++;
    }

    TEST_ASSERT(messageCount == numThreads * messagesPerThread, "Should have all messages from all threads");

    // Cleanup
    std::remove(logFile.c_str());

    std::cout << "  âœ“ Thread Safety tests passed" << std::endl;
    return true;
}

// =============================================================================
// Logger Swap During a Log Call (heap-corruption regression)
// =============================================================================
// Log's entry points used to read the logger_ member twice and hold no
// reference to it, so replacing the logger while a call was in flight destroyed
// the logger that call was still executing inside. Racing two threads to prove
// that is flaky; replacing the logger from *inside* log() makes it
// deterministic, because the facade holds the only remaining reference.
namespace {

class SelfReplacingLogger : public ILogger {
public:
    // The trace outlives the logger, so the order stays readable either way.
    explicit SelfReplacingLogger(std::vector<std::string>& trace) : trace_(trace) {}
    ~SelfReplacingLogger() override { trace_.push_back("dtor"); }

    void log(LogLevel, const std::string&) override {
        trace_.push_back("log-enter");
        if (!replaced_) {
            replaced_ = true;
            Log::shutdown(); // drops the facade's reference to this object
        }
        trace_.push_back("log-exit");
    }

    void setLevel(LogLevel level) override { level_ = level; }
    LogLevel getLevel() const override { return level_; }

private:
    std::vector<std::string>& trace_;
    LogLevel level_ = LogLevel::Trace;
    bool replaced_ = false;
};

} // namespace

bool testLoggerSwapDuringLog() {
    std::cout << "\nTesting logger swap during a log call..." << std::endl;

    std::vector<std::string> trace;
    {
        auto logger = std::make_shared<SelfReplacingLogger>(trace);
        Log::init(logger);
    } // local reference dropped: the facade now holds the only one

    Log::info("this message replaces the logger that is printing it");

    // Because the entry point snapshots the logger into a local shared_ptr, the
    // call runs to completion and the logger is released only when that
    // snapshot dies — after log() returns. Without the snapshot the destructor
    // interleaves, giving log-enter / dtor / log-exit, and "log-exit" is then
    // written through a freed object.
    const std::vector<std::string> expected{"log-enter", "log-exit", "dtor"};
    std::string got;
    for (const auto& step : trace) {
        got += step;
        got += ' ';
    }
    TEST_ASSERT(trace == expected, "logger must outlive the log call that replaced it; order was: " + got);

    // Sanity: the facade is still usable afterwards (shutdown() installed a
    // console logger), so a failure above cannot be mistaken for a dead facade.
    TEST_ASSERT(Log::getLogger() != nullptr, "a logger must remain installed after the swap");
    Log::info("facade still works after the swap");

    std::cout << "  Logger swap tests passed" << std::endl;
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================
int main() {
    std::cout << "\n==================================" << std::endl;
    std::cout << "  AestraCore Logging Tests" << std::endl;
    std::cout << "==================================" << std::endl;

    bool allPassed = true;
    allPassed &= testConsoleLogger();
    allPassed &= testFileLogger();
    allPassed &= testMultiLogger();
    allPassed &= testGlobalLogger();
    allPassed &= testThreadSafety();
    allPassed &= testLoggerSwapDuringLog();

    std::cout << "\n==================================" << std::endl;
    if (allPassed) {
        std::cout << "  âœ“ ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << "  âœ— SOME TESTS FAILED" << std::endl;
    }
    std::cout << "==================================" << std::endl;

    return allPassed ? 0 : 1;
}
