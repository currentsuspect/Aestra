// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RumbleUsagePathTest
// Minimal end-to-end usage path for internal Rumble instrument:
// PluginManager lifecycle -> factory-backed instance creation -> MIDI in -> audio out.

#include "Plugin/PluginManager.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace Aestra::Audio;

namespace {
PluginInfo makeRumbleInfo() {
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
    return info;
}

struct BufferStats {
    float peak = 0.0f;
    double rms = 0.0;
    bool hasInvalid = false;
};

BufferStats analyze(const std::vector<float>& interleaved) {
    BufferStats stats;
    long double sumSquares = 0.0;

    for (float sample : interleaved) {
        if (!std::isfinite(sample)) {
            stats.hasInvalid = true;
        }
        stats.peak = std::max(stats.peak, std::abs(sample));
        sumSquares += static_cast<long double>(sample) * static_cast<long double>(sample);
    }

    if (!interleaved.empty()) {
        stats.rms = std::sqrt(sumSquares / static_cast<long double>(interleaved.size()));
    }
    return stats;
}
} // namespace

int main() {
    std::cout << "\n=== Aestra Rumble Usage Path Test ===\n";

    auto& manager = PluginManager::getInstance();
    std::cout << "TEST: plugin manager initializes... ";
    assert(manager.initialize());
    std::cout << "✅ PASS\n";

    auto instance = manager.createInstance(makeRumbleInfo());
    std::cout << "TEST: manager creates internal rumble instance... ";
    assert(instance != nullptr);
    std::cout << "✅ PASS\n";

    std::cout << "TEST: instance initializes and activates... ";
    assert(instance->initialize(48000.0, 256));
    instance->activate();
    assert(instance->isActive());
    std::cout << "✅ PASS\n";

    instance->setParameter(0, 0.58f); // decay
    instance->setParameter(1, 0.20f); // drive
    instance->setParameter(2, 0.36f); // tone
    instance->setParameter(3, 0.54f); // gain

    constexpr uint32_t sampleRate = 48000;
    constexpr uint32_t blockSize = 256;
    constexpr uint32_t totalFrames = sampleRate * 2;
    const uint32_t totalBlocks = (totalFrames + blockSize - 1) / blockSize;

    std::vector<float> left(blockSize, 0.0f);
    std::vector<float> right(blockSize, 0.0f);
    float* outputs[2] = {left.data(), right.data()};

    std::vector<float> interleaved;
    interleaved.reserve(static_cast<size_t>(totalFrames) * 2);

    bool noteOnSent = false;
    bool noteOffSent = false;
    uint32_t renderedFrames = 0;

    for (uint32_t block = 0; block < totalBlocks; ++block) {
        MidiBuffer midi;

        if (!noteOnSent) {
            midi.addNoteOn(1, 36, 112, 0);
            noteOnSent = true;
        }

        const uint32_t noteOffFrame = sampleRate;
        if (!noteOffSent && renderedFrames <= noteOffFrame && noteOffFrame < renderedFrames + blockSize) {
            midi.addNoteOff(1, 36, 0, noteOffFrame - renderedFrames);
            noteOffSent = true;
        }

        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);

        const uint32_t framesThisBlock = std::min(blockSize, totalFrames - renderedFrames);
        instance->process(nullptr, outputs, 0, 2, framesThisBlock, &midi, nullptr);

        for (uint32_t i = 0; i < framesThisBlock; ++i) {
            interleaved.push_back(left[i]);
            interleaved.push_back(right[i]);
        }

        renderedFrames += framesThisBlock;
    }

    const auto stats = analyze(interleaved);

    std::cout << "TEST: rendered audio is sane and audible... ";
    assert(!stats.hasInvalid);
    assert(stats.peak > 1.0e-5f);
    assert(stats.peak < 0.98f);
    assert(stats.rms > 1.0e-4);
    std::cout << "✅ PASS\n";

    std::cout << "  Peak: " << stats.peak << "\n";
    std::cout << "  RMS: " << stats.rms << "\n";

    instance->deactivate();
    instance->shutdown();
    manager.shutdown();

    std::cout << "\nRumble usage path test passed.\n";
    return 0;
}
