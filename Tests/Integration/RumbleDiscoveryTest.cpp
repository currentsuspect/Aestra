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
    assert(manager.initialize());
    std::cout << "✅ PASS\n";

    std::cout << "TEST: rumble is discoverable by id... ";
    const PluginInfo* rumbleInfo = manager.findPlugin("com.Aestrastudios.rumble");
    assert(rumbleInfo != nullptr);
    assert(rumbleInfo->format == PluginFormat::Internal);
    assert(rumbleInfo->type == PluginType::Instrument);
    assert(rumbleInfo->hasMidiInput == true);
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
    assert(foundRumble);
    std::cout << "✅ PASS\n";

    std::cout << "TEST: createInstanceById works for rumble... ";
    auto instance = manager.createInstanceById("com.Aestrastudios.rumble");
    assert(instance != nullptr);
    assert(instance->initialize(48000.0, 512));
    instance->shutdown();
    std::cout << "✅ PASS\n";

    manager.shutdown();

    std::cout << "\nRumble discovery test passed.\n";
    return 0;
}
