// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "../include/NomadLog.h"
#include "../include/NomadFile.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace Nomad;

#define TEST_ASSERT(condition, message) \
    if (!(condition)) { \
        std::cerr << "FAILED: " << message << std::endl; \
        return false; \
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
    TEST_ASSERT(content.find("Multi-logger debug message") != std::string::npos, 
                "File should contain debug message");
    TEST_ASSERT(content.find("Multi-logger info message") != std::string::npos, 
                "File should contain info message");

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
    NOMAD_LOG_DEBUG("Macro debug message");
    NOMAD_LOG_INFO("Macro info message");
    NOMAD_LOG_WARNING("Macro warning message");
    NOMAD_LOG_ERROR("Macro error message");

    // Test stream-style logging
    NOMAD_LOG_STREAM_INFO << "Stream info: " << 42 << " " << 3.14;
    NOMAD_LOG_STREAM_WARNING << "Stream warning: " << "test";

    // Give file logger time to flush
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Verify file contains messages
    std::string content = File::readAllText(logFile);
    TEST_ASSERT(content.find("Global debug message") != std::string::npos, 
                "Should contain global debug message");
    TEST_ASSERT(content.find("Macro info message") != std::string::npos, 
                "Should contain macro info message");
    TEST_ASSERT(content.find("Stream info: 42 3.14") != std::string::npos, 
                "Should contain stream info message");

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
                NOMAD_LOG_STREAM_INFO << "Thread " << t << " message " << i;
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

    TEST_ASSERT(messageCount == numThreads * messagesPerThread, 
                "Should have all messages from all threads");

    // Cleanup
    std::remove(logFile.c_str());

    std::cout << "  âœ“ Thread Safety tests passed" << std::endl;
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================
int main() {
    std::cout << "\n==================================" << std::endl;
    std::cout << "  NomadCore Logging Tests" << std::endl;
    std::cout << "==================================" << std::endl;

    bool allPassed = true;
    allPassed &= testConsoleLogger();
    allPassed &= testFileLogger();
    allPassed &= testMultiLogger();
    allPassed &= testGlobalLogger();
    allPassed &= testThreadSafety();

    std::cout << "\n==================================" << std::endl;
    if (allPassed) {
        std::cout << "  âœ“ ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << "  âœ— SOME TESTS FAILED" << std::endl;
    }
    std::cout << "==================================" << std::endl;

    return allPassed ? 0 : 1;
}
