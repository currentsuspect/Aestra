// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RumbleArsenalAudibleTest
// Proves Arsenal pattern playback can drive an internal Rumble unit to audible output.

#include "Core/AudioEngine.h"
#include "Models/PatternManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"
#include "Playback/TimelineClock.h"
#include "Plugin/PluginManager.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

struct Stats {
    float peak = 0.0f;
    double rms = 0.0;
    bool hasInvalid = false;
};

Stats analyze(const std::vector<float>& interleaved) {
    Stats stats;
    long double sumSquares = 0.0;
    for (float s : interleaved) {
        if (!std::isfinite(s)) {
            stats.hasInvalid = true;
        }
        stats.peak = std::max(stats.peak, std::abs(s));
        sumSquares += static_cast<long double>(s) * static_cast<long double>(s);
    }
    if (!interleaved.empty()) {
        stats.rms = std::sqrt(sumSquares / static_cast<long double>(interleaved.size()));
    }
    return stats;
}
} // namespace

int main() {
    using namespace Aestra::Audio;

    constexpr uint32_t sampleRate = 48000;
    constexpr uint32_t blockSize = 256;
    constexpr uint32_t totalFrames = sampleRate * 2;
    constexpr uint32_t channels = 2;

    auto& pluginManager = PluginManager::getInstance();
    require(pluginManager.initialize(), "PluginManager failed to initialize");

    UnitManager unitManager;
    PatternManager patternManager;
    TimelineClock clock(120.0);
    PatternPlaybackEngine playbackEngine(&clock, &patternManager, &unitManager);

    auto rumble = pluginManager.createInstanceById("com.Aestrastudios.rumble");
    require(rumble != nullptr, "Failed to create Rumble instance");
    require(rumble->initialize(sampleRate, blockSize), "Failed to initialize Rumble instance");
    rumble->activate();
    rumble->setParameter(0, 0.68f);
    rumble->setParameter(1, 0.24f);
    rumble->setParameter(2, 0.42f);
    rumble->setParameter(3, 0.58f);

    UnitID unitId = unitManager.createUnit("Aestra Rumble", UnitGroup::Synth);
    unitManager.attachPlugin(unitId, "com.Aestrastudios.rumble", rumble);
    unitManager.setUnitEnabled(unitId, true);
    unitManager.setUnitMixerChannel(unitId, -1); // route directly to master in Arsenal mode
    unitManager.captureUnitPluginState(unitId);

    PatternID patternId = patternManager.createPattern();
    auto* pattern = patternManager.getPattern(patternId);
    require(pattern != nullptr, "Failed to create MIDI pattern");
    pattern->name = "Rumble Test Pattern";
    pattern->type = PatternSource::Type::Midi;
    pattern->lengthBeats = 2.0;
    pattern->payload = MidiPayload{};

    auto& notes = std::get<MidiPayload>(pattern->payload).notes;
    notes.push_back(MidiNote{36, 0.0, 0.5, 110.0f, unitId});
    notes.push_back(MidiNote{38, 1.0, 0.4, 100.0f, unitId});

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine failed to initialize");
    engine.setSampleRate(sampleRate);
    engine.setBufferConfig(blockSize, channels);
    engine.setBPM(120.0f);
    engine.setUnitManager(&unitManager);
    engine.setPatternPlaybackEngine(&playbackEngine);
    engine.setPatternPlaybackMode(true, 2.0);
    engine.setTransportPlaying(true);

    playbackEngine.schedulePatternInstance(patternId, 0.0, 1);

    std::vector<float> output(blockSize * channels, 0.0f);
    std::vector<float> captured;
    captured.reserve(totalFrames * channels);

    uint32_t framesRemaining = totalFrames;
    while (framesRemaining > 0) {
        const uint32_t framesThisBlock = std::min(blockSize, framesRemaining);
        std::fill(output.begin(), output.end(), 0.0f);
        engine.processBlock(output.data(), nullptr, framesThisBlock, 0.0);
        captured.insert(captured.end(), output.begin(), output.begin() + static_cast<std::ptrdiff_t>(framesThisBlock * channels));
        framesRemaining -= framesThisBlock;
    }

    const auto stats = analyze(captured);
    require(!stats.hasInvalid, "Rendered audio contains NaN/Inf");
    require(stats.peak > 1.0e-4f, "Rendered Arsenal output is silent");
    require(stats.peak < 0.99f, "Rendered Arsenal output peak is too high");
    require(stats.rms > 1.0e-4, "Rendered Arsenal output RMS is too low");

    std::cout << "Peak: " << stats.peak << "\n";
    std::cout << "RMS: " << stats.rms << "\n";
    std::cout << "[PASS] RumbleArsenalAudibleTest\n";
    return 0;
}
