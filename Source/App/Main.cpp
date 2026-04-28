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
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <objbase.h>
#include <windows.h>
#elif __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

using namespace Aestra;

namespace {
std::filesystem::path getExecutableDirectory() {
#ifdef _WIN32
    char exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::filesystem::path(std::string(exePath, len)).parent_path();
    }
#elif __APPLE__
    char exePath[PATH_MAX] = {};
    uint32_t size = sizeof(exePath);
    if (_NSGetExecutablePath(exePath, &size) == 0) {
        return std::filesystem::path(exePath).parent_path();
    }
#else
    char exePath[4096] = {};
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        return std::filesystem::path(exePath).parent_path();
    }
#endif
    return {};
}

std::filesystem::path findAestraFontDirectory() {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    roots.push_back(fs::current_path());

    const fs::path exeDir = getExecutableDirectory();
    if (!exeDir.empty() && exeDir != roots.front()) {
        roots.push_back(exeDir);
    }

    std::error_code ec;
    for (const auto& root : roots) {
        fs::path probe = root;
        while (!probe.empty()) {
            const fs::path candidate = probe / "AestraAssets" / "fonts";
            const fs::path geistMedium = candidate / "Geist" / "Geist-Medium.ttf";
            if (fs::exists(geistMedium, ec) && !ec) {
                return candidate;
            }

            const fs::path parent = probe.parent_path();
            if (parent == probe) {
                break;
            }
            probe = parent;
        }
    }

    return {};
}

void configureFontDirectoryEnv() {
    if (std::getenv("AESTRA_FONT_DIR") != nullptr) {
        Log::info("AESTRA_FONT_DIR already set externally");
        return;
    }

    const auto fontDir = findAestraFontDirectory();
    if (fontDir.empty()) {
        Log::warning("Could not auto-detect AestraAssets/fonts; Geist may fall back to system font");
        return;
    }

#ifdef _WIN32
    _putenv_s("AESTRA_FONT_DIR", fontDir.string().c_str());
#else
    setenv("AESTRA_FONT_DIR", fontDir.string().c_str(), 1);
#endif

    Log::info("Configured AESTRA_FONT_DIR: " + fontDir.string());
}
} // namespace

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
    #if defined(_DEBUG) && defined(_WIN32)
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
    configureFontDirectoryEnv();
    
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
    configureFontDirectoryEnv();
    
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

    // Session 020: Replaced std::quick_exit with normal return.
    //
    // Root cause of the prior hang: PluginManager::shutdown() called
    // m_scanner.cancelScan() but did not join the scanner thread. The
    // thread join was deferred to PluginScanner::~PluginScanner() during
    // static singleton destruction, where it could block on file I/O
    // or a stale cancel-check loop.
    //
    // Fix: PluginManager::shutdown() now calls m_scanner.cancelAndJoin()
    // which explicitly joins the thread during the deterministic shutdown
    // sequence, before static destruction.
    //
    // Remaining static singletons (AppLifecycle, AudioThreadStats,
    // Preferences, NUIThemeManager) have trivial or default destructors
    // and are safe to destroy in unspecified order.
    //
    // If a future shutdown hang reappears, re-enable quick_exit as a
    // last-resort fallback and add targeted diagnostics to identify the
    // blocking subsystem.
    return exitCode;
}
