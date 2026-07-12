// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/PluginScanner.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#ifndef _WIN32
#include "../Support/TestTempDirectory.h"

#include <unistd.h>
#endif

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

#ifndef _WIN32
std::string findFalseExecutable() {
    const char* candidates[] = {"/bin/false", "/usr/bin/false", "/usr/local/bin/false"};
    for (const char* candidate : candidates) {
        if (access(candidate, X_OK) == 0) {
            return candidate;
        }
    }
    return {};
}
#endif
} // namespace

int main() {
#ifdef _WIN32
    std::cout << "[SKIP] PluginScannerSigpipeTest is Unix-only\n";
    return 0;
#else
    const Aestra::Tests::ScopedTempDirectory dirScope{"PluginScannerSigpipe"};
    const auto& dir = dirScope.path();
    const auto clapPath = dir / "broken.clap";
    {
        std::ofstream out(clapPath, std::ios::binary);
        out << "not a clap plugin";
    }

    const std::string falseExecutable = findFalseExecutable();
    if (falseExecutable.empty()) {
        std::cout << "[SKIP] PluginScannerSigpipeTest: no false executable found\n";
        std::filesystem::remove_all(dir);
        return 0;
    }

    const char* previous = std::getenv("AESTRA_PLUGIN_HOST_PATH");
    const std::string previousValue = previous ? previous : "";
    setenv("AESTRA_PLUGIN_HOST_PATH", falseExecutable.c_str(), 1);

    Aestra::Audio::PluginScanner scanner;
    const auto plugins = scanner.rescanPlugin(clapPath);

    if (previous) {
        setenv("AESTRA_PLUGIN_HOST_PATH", previousValue.c_str(), 1);
    } else {
        unsetenv("AESTRA_PLUGIN_HOST_PATH");
    }

    std::filesystem::remove_all(dir);

#ifdef AESTRA_TEST_HAS_CLAP
    require(plugins.empty(), "Broken out-of-process CLAP scan should not return plugin metadata");
#else
    if (!plugins.empty()) {
        require(plugins.front().format == Aestra::Audio::PluginFormat::CLAP, "Fallback plugin format should be CLAP");
        require(plugins.front().id == "broken", "Core fallback should only use the broken CLAP filename");
    }
#endif

    std::cout << "[PASS] PluginScannerSigpipeTest\n";
    return 0;
#endif
}
