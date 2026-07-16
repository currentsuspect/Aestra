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
    info.version = "0.2.0";
    info.category = "Instrument";
    info.format = PluginFormat::Internal;
    info.type = PluginType::Instrument;
    info.numAudioInputs = 0;
    info.numAudioOutputs = 2;
    info.hasMidiInput = true;
    info.hasMidiOutput = false;
    info.hasEditor = true;

    PluginInstancePtr instance;
    factory.createPluginAsync(info, [&](PluginInstancePtr created) { instance = created; });

    std::cout << "TEST: factory returned instance... ";
    if (!instance) {
        std::cerr << "FAIL: factory returned null instance\n";
        return 1;
    }
    std::cout << "✅ PASS\n";

    std::cout << "TEST: plugin metadata matches... ";
    const auto& pluginInfo = instance->getInfo();
    if (pluginInfo.id != "com.Aestrastudios.rumble" || pluginInfo.name != "Aestra Rumble" ||
        pluginInfo.type != PluginType::Instrument || pluginInfo.format != PluginFormat::Internal ||
        pluginInfo.numAudioOutputs != 2 || !pluginInfo.hasMidiInput || !pluginInfo.hasEditor) {
        std::cerr << "FAIL: plugin metadata mismatch\n";
        return 1;
    }
    std::cout << "✅ PASS\n";

    std::cout << "TEST: plugin initializes and exposes parameters... ";
    if (!instance->initialize(48000.0, 512) || instance->getParameterCount() != 25 ||
        instance->getParameters().empty() || instance->getTailSamples() == 0) {
        std::cerr << "FAIL: plugin failed initialization/parameter checks\n";
        return 1;
    }
    instance->shutdown();
    std::cout << "✅ PASS\n";

    std::cout << "\nAll Rumble plugin factory tests passed.\n";
    return 0;
}
