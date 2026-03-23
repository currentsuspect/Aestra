
#include "PluginManager.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace Aestra::Audio;

int main() {
    std::cout << "===========================================\n";
    std::cout << "  Aestra PluginScanTest (VST3 Verification)\n";
    std::cout << "===========================================\n\n";

    // Use PluginManager instead of raw Scanner
    auto& manager = PluginManager::getInstance();

    std::cout << "[Step 0] Initializing PluginManager...\n";
    if (!manager.initialize()) {
        std::cerr << "[ERROR] PluginManager initialization failed!\n";
        return 1;
    }

    // 1. Setup paths
    std::cout << "[Step 1] Initializing Search Paths...\n";
    auto& scanner = manager.getScanner();
    scanner.addDefaultSearchPaths();

    auto paths = scanner.getSearchPaths();
    for (const auto& p : paths) {
        std::cout << "  -> Path: " << p.string() << "\n";
    }
    std::cout << "\n";

    // 2. Scan (Blocking)
    std::cout << "[Step 2] Scanning Plugins (Blocking)...\n";
    auto plugins = scanner.scanBlocking();

    std::cout << "[Step 3] Scan Complete. Found " << plugins.size() << " plugins.\n\n";

    if (plugins.empty()) {
        std::cout << "[WARNING] No verified plugins found. Exiting.\n";
        manager.shutdown();
        return 0;
    }

    // 3. Try to Instantiate the First Plugin
    const PluginInfo* target = nullptr;
    for (const auto& p : plugins) {
        if (p.format == PluginFormat::VST3) {
            target = &p;
            break;
        }
    }

    if (!target) {
        std::cout << "[WARNING] No VST3 plugins found to test instantiation.\n";
    } else {
        std::cout << "===========================================\n";
        std::cout << "  Testing Instantiation: " << target->name << "\n";
        std::cout << "===========================================\n";

        auto instance = manager.createInstance(*target);
        if (instance) {
            std::cout << "[SUCCESS] Instance created.\n";

            if (instance->initialize(44100.0, 512)) {
                std::cout << "[SUCCESS] Plugin initialized (44.1kHz, 512 samples).\n";
                std::cout << "  - Parameters: " << instance->getParameterCount() << "\n";
                std::cout << "  - Latency: " << instance->getLatencySamples() << "\n";
                std::cout << "  - Has Editor: " << (instance->hasEditor() ? "Yes" : "No") << "\n";
            } else {
                std::cerr << "[ERROR] Plugin initialize() failed.\n";
            }
        } else {
            std::cerr << "[ERROR] Failed to create plugin instance.\n";
        }
    }

    manager.shutdown();
    std::cout << "\n[SUCCESS] Test logic completed.\n";
    return 0;
}
