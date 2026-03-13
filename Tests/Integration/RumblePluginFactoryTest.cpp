// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RumblePluginFactoryTest
// Verifies that the in-process plugin factory can instantiate the internal Rumble plugin.

#include "Plugin/PluginFactory.h"

#include <cassert>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;

int main() {
    std::cout << "\n=== Aestra Rumble Plugin Factory Test ===\n";

    InProcessPluginFactory factory;

    PluginInfo info;
    info.id = "com.Aestrastudios.rumble";
    info.name = "Aestra Rumble";
    info.vendor = "Aestra Studios";
    info.version = "0.1.0";
    info.category = "Instrument";
    info.format = PluginFormat::Internal;
    info.type = PluginType::Instrument;
    info.numAudioInputs = 0;
    info.numAudioOutputs = 2;
    info.hasMidiInput = true;
    info.hasMidiOutput = false;
    info.hasEditor = false;

    PluginInstancePtr instance;
    factory.createPluginAsync(info, [&](PluginInstancePtr created) { instance = created; });

    std::cout << "TEST: factory returned instance... ";
    assert(instance != nullptr);
    std::cout << "✅ PASS\n";

    std::cout << "TEST: plugin metadata matches... ";
    const auto& pluginInfo = instance->getInfo();
    assert(pluginInfo.id == "com.Aestrastudios.rumble");
    assert(pluginInfo.name == "Aestra Rumble");
    assert(pluginInfo.type == PluginType::Instrument);
    assert(pluginInfo.format == PluginFormat::Internal);
    assert(pluginInfo.numAudioOutputs == 2);
    assert(pluginInfo.hasMidiInput == true);
    assert(pluginInfo.hasEditor == false);
    std::cout << "✅ PASS\n";

    std::cout << "TEST: plugin initializes and exposes parameters... ";
    assert(instance->initialize(48000.0, 512));
    assert(instance->getParameterCount() == 4);
    assert(!instance->getParameters().empty());
    assert(instance->getTailSamples() > 0);
    instance->shutdown();
    std::cout << "✅ PASS\n";

    std::cout << "\nAll Rumble plugin factory tests passed.\n";
    return 0;
}
