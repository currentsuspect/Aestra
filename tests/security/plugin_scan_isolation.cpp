// © 2026 Aestra Studios — All Rights Reserved.
// Plugin scan isolation proof: real CLAP metadata is read in AestraPluginHost.

#include "Plugin/PluginScanner.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

using namespace Aestra::Audio;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: SecPluginScanIsolation <AestraPluginHost path> <real CLAP path>\n";
        return 2;
    }

    const std::filesystem::path hostPath = argv[1];
    const std::filesystem::path clapPath = argv[2];
    if (!std::filesystem::exists(hostPath) || !std::filesystem::exists(clapPath)) {
        std::cout << "[SKIP] helper or CLAP test plugin path does not exist\n";
        return 77;
    }

#ifdef _WIN32
    _putenv_s("AESTRA_PLUGIN_HOST_PATH", hostPath.string().c_str());
#else
    setenv("AESTRA_PLUGIN_HOST_PATH", hostPath.string().c_str(), 1);
#endif

    PluginScanner scanner;
    auto plugins = scanner.rescanPlugin(clapPath);
    if (plugins.empty()) {
        std::cerr << "isolated scanner did not return CLAP metadata\n";
        return 1;
    }

    const auto& first = plugins.front();
    if (first.id.empty() || first.name.empty() || first.path != clapPath || first.format != PluginFormat::CLAP) {
        std::cerr << "isolated scanner returned invalid CLAP metadata\n";
        return 1;
    }

    std::cout << "[PASS] Plugin scanner isolated CLAP metadata probe via helper: " << first.name << "\n";
    return 0;
}
