// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RumbleDiscoveryTest
// Verifies internal plugins participate in normal discovery/lookup flows.

#include "Plugin/PluginManager.h"

#include <cassert>
#include <iostream>

using namespace Aestra::Audio;

int main() {
    std::cout << "\n=== Aestra Rumble Discovery Test ===\n";

    auto& manager = PluginManager::getInstance();

    std::cout << "TEST: plugin manager initializes... ";
    if (!manager.initialize()) {
        std::cerr << "FAIL: plugin manager failed to initialize\n";
        return 1;
    }
    std::cout << "✅ PASS\n";

    std::cout << "TEST: rumble is discoverable by id... ";
    const PluginInfo* rumbleInfo = manager.findPlugin("com.Aestrastudios.rumble");
    if (!rumbleInfo || rumbleInfo->format != PluginFormat::Internal ||
        rumbleInfo->type != PluginType::Instrument || !rumbleInfo->hasMidiInput) {
        std::cerr << "FAIL: rumble metadata missing or incorrect\n";
        manager.shutdown();
        return 1;
    }
    std::cout << "✅ PASS\n";

    std::cout << "TEST: internal instruments include rumble... ";
    const auto instruments = manager.getInstrumentPlugins();
    bool foundRumble = false;
    for (const auto& plugin : instruments) {
        if (plugin.id == "com.Aestrastudios.rumble") {
            foundRumble = true;
            break;
        }
    }
    if (!foundRumble) {
        std::cerr << "FAIL: rumble missing from instrument plugin list\n";
        manager.shutdown();
        return 1;
    }
    std::cout << "✅ PASS\n";

    std::cout << "TEST: createInstanceById works for rumble... ";
    auto instance = manager.createInstanceById("com.Aestrastudios.rumble");
    if (!instance || !instance->initialize(48000.0, 512)) {
        std::cerr << "FAIL: createInstanceById returned null or failed initialization\n";
        manager.shutdown();
        return 1;
    }
    instance->shutdown();
    std::cout << "✅ PASS\n";

    manager.shutdown();

    std::cout << "\nRumble discovery test passed.\n";
    return 0;
}
