// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#define AESTRA_BUILD_ID "Aestra-2025-Core"

/**
 * @file Main.cpp
 * @brief Aestra - Main Application Entry Point
 * 
 * Uses WinMain for Windows GUI subsystem (no console window).
 * All logging goes to runtime_log.txt in the working directory.
 */

#include "AestraApp.h"
#include "../AestraCore/include/AestraLog.h"
#include <iostream>
#include <cstdlib>
#include <objbase.h>
#include <string>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace Aestra;

// =============================================================================
// Initialize Logging to File
// =============================================================================
static void initializeFileLogging() {
    // Create a multi-logger that writes to file only (no console in GUI mode)
    auto multiLogger = std::make_shared<MultiLogger>(LogLevel::Debug);
    
    // File logger - always log to runtime_log.txt
    auto fileLogger = std::make_shared<FileLogger>("runtime_log.txt", LogLevel::Debug);
    if (fileLogger->isOpen()) {
        multiLogger->addLogger(fileLogger);
    }
    
    // In debug builds, also keep console logging if attached
    #ifdef _DEBUG
    if (GetConsoleWindow() != nullptr) {
        multiLogger->addLogger(std::make_shared<ConsoleLogger>(LogLevel::Debug));
    }
    #endif
    
    Log::init(multiLogger);
    Log::info("========================================");
    Log::info("Aestra Starting - " AESTRA_BUILD_ID);
    Log::info("Working Directory: " + std::filesystem::current_path().string());
    Log::info("========================================");
}

// =============================================================================
// Main Entry Point (WinMain for Windows GUI subsystem)
// =============================================================================
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;
    
    // Initialize file logging FIRST (before any other logging)
    initializeFileLogging();
    
    // Initialize COM as STA (Single-Threaded Apartment) for ASIO support
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        Log::error("Failed to initialize COM");
        return 1;
    }

    int exitCode = 0;
    
    // Parse command line for project file (e.g. double-clicking .aes file)
    std::string projectPath;
    if (lpCmdLine && lpCmdLine[0] != '\0') {
        projectPath = lpCmdLine;
        // Remove surrounding quotes if present
        if (projectPath.front() == '"' && projectPath.back() == '"') {
            projectPath = projectPath.substr(1, projectPath.size() - 2);
        }
        // Validate it's a .aes file
        if (projectPath.size() >= 4 && 
            projectPath.substr(projectPath.size() - 4) == ".aes") {
            Log::info("Opening project: " + projectPath);
        } else {
            projectPath.clear(); // Not a project file, ignore
        }
    }
#else
int main(int argc, char* argv[]) {
    initializeFileLogging();
    
    int exitCode = 0;
    std::string projectPath;
    if (argc > 1) {
        projectPath = argv[1];
        if (projectPath.size() >= 4 && 
            projectPath.substr(projectPath.size() - 4) == ".aes") {
            Log::info("Opening project: " + projectPath);
        } else {
            projectPath.clear();
        }
    }
#endif
    
    try {
        AestraApp app;
        
        if (!app.initialize(projectPath)) {
            Log::error("Failed to initialize Aestra");
            exitCode = 1;
        } else {
            app.run();
        }
    }
    catch (const std::exception& e) {
        Log::error(std::string("Fatal error: ") + e.what());
        exitCode = 1;
    }
    catch (...) {
        Log::error("Unknown fatal error");
        exitCode = 1;
    }

#ifdef _WIN32
    // Clean up COM
    CoUninitialize();
#endif
    
    Log::info("Aestra Exiting with code: " + std::to_string(exitCode));
    
    // TODO: [P2] Investigate shutdown hang - static singleton destructors (PluginManager,
    // AudioEngine) or ASIO/COM cleanup order causes process to hang after all explicit
    // cleanup completes. Using quick_exit to bypass, but root cause still unknown.
    // See: AestraApp::shutdown() for explicit cleanup that runs successfully.
    std::quick_exit(exitCode);
}
