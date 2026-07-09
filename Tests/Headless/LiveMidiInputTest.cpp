// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// LiveMidiInputTest
// Proves live note input (AudioEngine::postLiveMidiEvent) reaches an Arsenal
// unit's instrument and produces audible output with the transport STOPPED —
// playing an instrument must not require pressing play. Uses the built-in
// SamplerPlugin with an injected synthetic sample, so it runs in core mode on
// every CI lane (no premium modules, no files, no devices).

#include "Core/AudioEngine.h"
#include "Models/PatternManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"
#include "Playback/TimelineClock.h"
#include "Plugin/SamplerPlugin.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
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
    bool hasInvalid = false;
};

Stats analyze(const std::vector<float>& interleaved) {
    Stats stats;
    for (float s : interleaved) {
        if (!std::isfinite(s)) {
            stats.hasInvalid = true;
        }
        stats.peak = std::max(stats.peak, std::abs(s));
    }
    return stats;
}
} // namespace

int main() {
    using namespace Aestra::Audio;

    constexpr uint32_t sampleRate = 48000;
    constexpr uint32_t blockSize = 256;
    constexpr uint32_t channels = 2;
    constexpr uint8_t kNoteOn = 0x90;
    constexpr uint8_t kNoteOff = 0x80;
    constexpr uint8_t kMiddleC = 60;

    UnitManager unitManager;
    PatternManager patternManager;
    TimelineClock clock(120.0);
    PatternPlaybackEngine playbackEngine(&clock, &patternManager, &unitManager);

    // Built-in sampler with an injected 0.25 s constant-tone sample — core-mode,
    // no premium plugins, no file I/O.
    auto sampler = std::make_shared<Plugins::SamplerPlugin>();
    require(sampler->initialize(sampleRate, blockSize), "SamplerPlugin failed to initialize");
    sampler->activate();
    // Short release: with the 300 ms default, a one-shot sample shorter than
    // the release time enters Release at trigger with releaseGain captured at 0
    // and never sounds (filed as a sampler bug; this test targets the live-MIDI
    // routing path, not that envelope quirk).
    sampler->setEnvelope(0.005f, 0.05f, 0.8f, 0.05f);
    {
        const uint32_t sampleFrames = sampleRate / 4;
        std::vector<float> data(sampleFrames, 0.0f);
        for (uint32_t i = 0; i < sampleFrames; ++i) {
            data[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 220.0 * (static_cast<double>(i) / sampleRate));
        }
        require(sampler->loadSampleData("live-midi-test-tone", std::move(data), sampleRate, 1),
                "loadSampleData failed");
    }

    UnitID unitId = unitManager.createUnit("Live Sampler", UnitGroup::Synth);
    unitManager.attachPlugin(unitId, "com.Aestrastudios.sampler", sampler);
    unitManager.setUnitEnabled(unitId, true);
    unitManager.setUnitMixerChannel(unitId, -1); // route directly to master in Arsenal mode
    unitManager.captureUnitPluginState(unitId);

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine failed to initialize");
    engine.setSampleRate(sampleRate);
    engine.setBufferConfig(blockSize, channels);
    engine.setBPM(120.0f);
    engine.setUnitManager(&unitManager);
    engine.setPatternPlaybackEngine(&playbackEngine);
    engine.setPatternPlaybackMode(true, 2.0);
    // Deliberately NOT calling setTransportPlaying(true): live input must sound
    // while the transport is stopped.

    std::vector<float> output(blockSize * channels, 0.0f);
    auto renderBlocks = [&](uint32_t blocks, std::vector<float>* capture) {
        for (uint32_t b = 0; b < blocks; ++b) {
            std::fill(output.begin(), output.end(), 0.0f);
            engine.processBlock(output.data(), nullptr, blockSize, 0.0);
            if (capture != nullptr) {
                capture->insert(capture->end(), output.begin(), output.end());
            }
        }
    };

    // ---------------- 1. Silence before any input.
    std::vector<float> silentLead;
    renderBlocks(8, &silentLead);
    const auto leadStats = analyze(silentLead);
    require(!leadStats.hasInvalid, "Lead-in contains NaN/Inf");
    require(leadStats.peak < 1.0e-6f, "Output not silent before any live input");

    // ---------------- 2. Live note-on (transport stopped) becomes audible.
    require(engine.postLiveMidiEvent(unitId, kNoteOn, kMiddleC, 110), "postLiveMidiEvent rejected note-on");
    std::vector<float> noteAudio;
    renderBlocks(32, &noteAudio); // ~170 ms, well inside the 250 ms sample
    const auto noteStats = analyze(noteAudio);
    require(!noteStats.hasInvalid, "Live-note audio contains NaN/Inf");
    require(noteStats.peak > 1.0e-4f, "Live note-on produced silence with transport stopped");
    require(noteStats.peak < 0.99f, "Live-note output peak too high");

    // ---------------- 3. Voice ends (note-off / sample end): back to silence.
    // This pins "no stuck voices", independent of the sampler's exact
    // note-off envelope semantics.
    engine.postLiveMidiEvent(unitId, kNoteOff, kMiddleC, 0);
    renderBlocks(96, nullptr); // ~510 ms — beyond the sample length + release
    std::vector<float> tail;
    renderBlocks(16, &tail);
    const auto tailStats = analyze(tail);
    require(!tailStats.hasInvalid, "Post-note audio contains NaN/Inf");
    require(tailStats.peak < 1.0e-3f, "Live voice did not return to silence");

    // ---------------- 4. Chords: two simultaneous notes sound together.
    require(engine.postLiveMidiEvent(unitId, kNoteOn, kMiddleC, 110), "chord note 1 rejected");
    require(engine.postLiveMidiEvent(unitId, kNoteOn, kMiddleC + 7, 110), "chord note 2 rejected");
    std::vector<float> chordAudio;
    renderBlocks(32, &chordAudio);
    const auto chordStats = analyze(chordAudio);
    require(!chordStats.hasInvalid, "Chord audio contains NaN/Inf");
    require(chordStats.peak > 1.0e-4f, "Chord produced silence");
    engine.postLiveMidiEvent(unitId, kNoteOff, kMiddleC, 0);
    engine.postLiveMidiEvent(unitId, kNoteOff, kMiddleC + 7, 0);
    renderBlocks(112, nullptr);

    // ---------------- 5. Events for unknown units are dropped harmlessly.
    require(engine.postLiveMidiEvent(unitId + 999, kNoteOn, kMiddleC, 100),
            "unknown-unit event rejected at queue");
    std::vector<float> unknownUnit;
    renderBlocks(16, &unknownUnit);
    const auto unknownStats = analyze(unknownUnit);
    require(unknownStats.peak < 1.0e-3f, "Event for unknown unit produced audio");

    std::cout << "Live note peak: " << noteStats.peak << ", chord peak: " << chordStats.peak << "\n";
    std::cout << "[PASS] LiveMidiInputTest\n";
    return 0;
}
