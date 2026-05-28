// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/PluginScanner.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

std::filesystem::path makeTempDir() {
    const auto base = std::filesystem::temp_directory_path() / "Aestra_tests";
    std::filesystem::create_directories(base);
    for (int i = 0; i < 1000; ++i) {
        const auto candidate = base / ("PluginScannerSigpipe_" + std::to_string(i));
        if (!std::filesystem::exists(candidate)) {
            std::filesystem::create_directories(candidate);
            return candidate;
        }
    }

    const auto fallback = base / "PluginScannerSigpipe_fallback";
    std::filesystem::create_directories(fallback);
    return fallback;
}
} // namespace

int main() {
#ifdef _WIN32
    std::cout << "[SKIP] PluginScannerSigpipeTest is Unix-only\n";
    return 0;
#else
    const auto dir = makeTempDir();
    const auto clapPath = dir / "broken.clap";
    {
        std::ofstream out(clapPath, std::ios::binary);
        out << "not a clap plugin";
    }

    const char* previous = std::getenv("AESTRA_PLUGIN_HOST_PATH");
    const std::string previousValue = previous ? previous : "";
    setenv("AESTRA_PLUGIN_HOST_PATH", "/bin/false", 1);

    Aestra::Audio::PluginScanner scanner;
    const auto plugins = scanner.rescanPlugin(clapPath);

    if (previous) {
        setenv("AESTRA_PLUGIN_HOST_PATH", previousValue.c_str(), 1);
    } else {
        unsetenv("AESTRA_PLUGIN_HOST_PATH");
    }

    std::filesystem::remove_all(dir);

    if (!plugins.empty()) {
        require(plugins.front().format == Aestra::Audio::PluginFormat::CLAP, "Fallback plugin format should be CLAP");
    }

    std::cout << "[PASS] PluginScannerSigpipeTest\n";
    return 0;
#endif
}
