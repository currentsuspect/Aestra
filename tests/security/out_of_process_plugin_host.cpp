// © 2026 Aestra Studios — All Rights Reserved.
// Out-of-process plugin host containment proof.

#include "Plugin/PluginFactory.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

using namespace Aestra::Audio;

namespace {

PluginInfo makeInfo(const std::string& id) {
    PluginInfo info;
    info.id = id;
    info.name = id;
    info.vendor = "Aestra Test";
    info.version = "1";
    info.category = "Effect";
    info.format = PluginFormat::VST3;
    info.type = PluginType::Effect;
    info.path = "/tmp/aestra-test-plugin.vst3";
    info.numAudioInputs = 2;
    info.numAudioOutputs = 2;
    return info;
}

PluginInfo makeRealClapProbeInfo(const std::string& path) {
    PluginInfo info;
    info.id = "__aestra_probe_first__";
    info.name = "CLAP probe";
    info.vendor = "Aestra Test";
    info.version = "1";
    info.category = "Effect";
    info.format = PluginFormat::CLAP;
    info.type = PluginType::Effect;
    info.path = path;
    info.numAudioInputs = 2;
    info.numAudioOutputs = 2;
    return info;
}

PluginInstancePtr create(OutOfProcessPluginFactory& factory, const PluginInfo& info) {
    PluginInstancePtr created;
    factory.createPluginAsync(info, [&](PluginInstancePtr instance) { created = std::move(instance); });
    return created;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: SecOutOfProcessPluginHost <AestraPluginHost path> [real CLAP path]\n";
        return 2;
    }

    OutOfProcessPluginFactory factory(argv[1]);

    auto instance = create(factory, makeInfo("__aestra_test_echo__"));
    if (!instance) {
        std::cerr << "failed to create isolated echo plugin\n";
        return 1;
    }

    if (!instance->initialize(48000.0, 64)) {
        std::cerr << "failed to initialize isolated echo plugin\n";
        return 1;
    }
    instance->activate();
    if (!instance->isActive() || instance->isCrashed()) {
        std::cerr << "isolated echo plugin not active\n";
        return 1;
    }

    float inL[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    float inR[4] = {-0.1f, -0.2f, -0.3f, -0.4f};
    const float* inputs[2] = {inL, inR};
    float outL[4] = {};
    float outR[4] = {};
    float* outputs[2] = {outL, outR};
    instance->process(inputs, outputs, 2, 2, 4);

    if (std::memcmp(inL, outL, sizeof(inL)) != 0 || std::memcmp(inR, outR, sizeof(inR)) != 0) {
        std::cerr << "isolated plugin proxy did not pass through audio safely\n";
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    float nextInL[4] = {0.9f, 0.8f, 0.7f, 0.6f};
    float nextInR[4] = {-0.9f, -0.8f, -0.7f, -0.6f};
    const float* nextInputs[2] = {nextInL, nextInR};
    std::memset(outL, 0, sizeof(outL));
    std::memset(outR, 0, sizeof(outR));
    instance->process(nextInputs, outputs, 2, 2, 4);

    if (std::memcmp(inL, outL, sizeof(inL)) != 0 || std::memcmp(inR, outR, sizeof(inR)) != 0) {
        std::cerr << "isolated plugin proxy did not return helper-processed output on the next callback\n";
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    MidiBuffer midi;
    midi.addNoteOn(1, 60, 100, 1);
    float midiInL[4] = {0.11f, 0.22f, 0.33f, 0.44f};
    float midiInR[4] = {-0.11f, -0.22f, -0.33f, -0.44f};
    const float* midiInputs[2] = {midiInL, midiInR};
    std::memset(outL, 0, sizeof(outL));
    std::memset(outR, 0, sizeof(outR));
    instance->process(midiInputs, outputs, 2, 2, 4, &midi, nullptr);
    if (std::memcmp(nextInL, outL, sizeof(nextInL)) != 0 || std::memcmp(nextInR, outR, sizeof(nextInR)) != 0) {
        std::cerr << "isolated plugin proxy did not survive MIDI-bearing process command\n";
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::memset(outL, 0, sizeof(outL));
    std::memset(outR, 0, sizeof(outR));
    instance->process(nextInputs, outputs, 2, 2, 4);
    if (std::memcmp(midiInL, outL, sizeof(midiInL)) != 0 || std::memcmp(midiInR, outR, sizeof(midiInR)) != 0) {
        std::cerr << "isolated plugin proxy did not return helper-processed MIDI block output\n";
        return 1;
    }

    if (!instance->saveState().empty() || !instance->loadState({}) ||
        instance->loadState(std::vector<uint8_t>{1, 2, 3})) {
        std::cerr << "isolated plugin proxy state protocol handling failed for echo helper\n";
        return 1;
    }

    auto missingVst3 = create(factory, makeInfo("com.aestra.missing-vst3"));
    if (missingVst3) {
        std::cerr << "missing real VST3 plugin should not fall back to echo processing\n";
        return 1;
    }

    auto crashed = create(factory, makeInfo("__aestra_test_crash__"));
    if (crashed) {
        std::cerr << "crashing plugin should not produce a usable instance\n";
        return 1;
    }

    if (argc >= 3) {
        auto realClap = create(factory, makeRealClapProbeInfo(argv[2]));
        if (!realClap) {
            std::cerr << "failed to load real CLAP plugin out of process: " << argv[2] << "\n";
            return 1;
        }
        if (!realClap->initialize(48000.0, 64)) {
            std::cerr << "failed to initialize real CLAP plugin proxy\n";
            return 1;
        }
        realClap->activate();
        if (!realClap->isActive() || realClap->isCrashed()) {
            std::cerr << "real CLAP plugin proxy not active\n";
            return 1;
        }
        std::memset(outL, 0, sizeof(outL));
        std::memset(outR, 0, sizeof(outR));
        realClap->process(inputs, outputs, 2, 2, 4);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        realClap->process(nextInputs, outputs, 2, 2, 4);
        for (float sample : outL) {
            if (!std::isfinite(sample)) {
                std::cerr << "real CLAP plugin returned non-finite left-channel audio\n";
                return 1;
            }
        }
        for (float sample : outR) {
            if (!std::isfinite(sample)) {
                std::cerr << "real CLAP plugin returned non-finite right-channel audio\n";
                return 1;
            }
        }
        if (realClap->isCrashed()) {
            std::cerr << "real CLAP plugin proxy crashed during process\n";
            return 1;
        }
        realClap->shutdown();
    }

    std::cout << "[PASS] Out-of-process plugin host contains helper crashes and keeps parent alive.\n";
    return 0;
}
