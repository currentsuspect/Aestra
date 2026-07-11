// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// HardwareMidiInputTest
// Proves the hardware MIDI input path end-to-end minus the device: (1) the
// pure RtMidi-message translation (MidiInputService::translateMessage) and
// (2) AudioEngine::postHardwareMidiEvent reaching an Arsenal unit's instrument
// audibly with the transport stopped, including interleaving with the UI
// keyboard queue. Uses only header-inline APIs, so it links against
// AestraAudioCore on every CI lane — no RtMidi backend, no devices.

#include "Core/AudioEngine.h"
#include "IO/MidiInputService.h"
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

    constexpr uint8_t kNoteOn = 0x90;
    constexpr uint8_t kNoteOff = 0x80;
    constexpr uint8_t kMiddleC = 60;

    // ---------------- Part 1: pure message translation.
    {
        const uint8_t noteOn[3] = {0x91, 60, 100}; // channel 2 note-on
        const auto t = MidiInputService::translateMessage(noteOn, 3);
        require(t.has_value(), "note-on not translated");
        require(t->status == 0x91 && t->data1 == 60 && t->data2 == 100, "note-on translated wrong");

        const uint8_t noteOff[3] = {0x83, 60, 64};
        const auto off = MidiInputService::translateMessage(noteOff, 3);
        require(off.has_value(), "note-off not translated");
        require(off->status == 0x83, "note-off channel not preserved");

        // Note-on velocity 0 normalizes to note-off on the same channel.
        const uint8_t vel0[3] = {0x95, 72, 0};
        const auto norm = MidiInputService::translateMessage(vel0, 3);
        require(norm.has_value(), "velocity-0 note-on not translated");
        require(norm->status == 0x85 && norm->data1 == 72 && norm->data2 == 0,
                "velocity-0 note-on not normalized to note-off");

        // Non-note channel voice, realtime, and short/null messages are dropped.
        const uint8_t cc[3] = {0xB0, 7, 100};
        require(!MidiInputService::translateMessage(cc, 3).has_value(), "CC not rejected");
        const uint8_t bend[3] = {0xE0, 0, 64};
        require(!MidiInputService::translateMessage(bend, 3).has_value(), "pitch bend not rejected");
        const uint8_t clock[1] = {0xF8};
        require(!MidiInputService::translateMessage(clock, 1).has_value(), "realtime clock not rejected");
        require(!MidiInputService::translateMessage(noteOn, 2).has_value(), "truncated message not rejected");
        require(!MidiInputService::translateMessage(nullptr, 3).has_value(), "null message not rejected");

        // Data bytes with the high bit set are masked into range, not passed raw.
        const uint8_t dirty[3] = {0x90, 0xFF, 0xFF};
        const auto masked = MidiInputService::translateMessage(dirty, 3);
        require(masked.has_value() && masked->data1 == 0x7F && masked->data2 == 0x7F,
                "data bytes not masked to 7 bits");
    }

    // ---------------- Part 2: hardware queue reaches the instrument audibly.
    constexpr uint32_t sampleRate = 48000;
    constexpr uint32_t blockSize = 256;
    constexpr uint32_t channels = 2;

    UnitManager unitManager;
    PatternManager patternManager;
    TimelineClock clock(120.0);
    PatternPlaybackEngine playbackEngine(&clock, &patternManager, &unitManager);

    // Built-in sampler with an injected 0.25 s constant-tone sample — core-mode,
    // no premium plugins, no file I/O (same rig as LiveMidiInputTest).
    auto sampler = std::make_shared<Plugins::SamplerPlugin>();
    require(sampler->initialize(sampleRate, blockSize), "SamplerPlugin failed to initialize");
    sampler->activate();
    sampler->setEnvelope(0.005f, 0.05f, 0.8f, 0.05f);
    {
        const uint32_t sampleFrames = sampleRate / 4;
        std::vector<float> data(sampleFrames, 0.0f);
        for (uint32_t i = 0; i < sampleFrames; ++i) {
            data[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 220.0 * (static_cast<double>(i) / sampleRate));
        }
        require(sampler->loadSampleData("hw-midi-test-tone", std::move(data), sampleRate, 1), "loadSampleData failed");
    }

    UnitID unitId = unitManager.createUnit("HW Sampler", UnitGroup::Synth);
    unitManager.attachPlugin(unitId, "com.Aestrastudios.sampler", sampler);
    unitManager.setUnitEnabled(unitId, true);
    unitManager.setUnitMixerChannel(unitId, -1);
    unitManager.captureUnitPluginState(unitId);

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine failed to initialize");
    engine.setSampleRate(sampleRate);
    engine.setBufferConfig(blockSize, channels);
    engine.setBPM(120.0f);
    engine.setUnitManager(&unitManager);
    engine.setPatternPlaybackEngine(&playbackEngine);
    engine.setPatternPlaybackMode(true, 2.0);
    // Transport deliberately stopped: hardware input must sound without play.

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

    // 2a. Silence before any input.
    std::vector<float> silentLead;
    renderBlocks(8, &silentLead);
    const auto leadStats = analyze(silentLead);
    require(!leadStats.hasInvalid, "Lead-in contains NaN/Inf");
    require(leadStats.peak < 1.0e-6f, "Output not silent before any hardware input");

    // 2b. Hardware note-on (transport stopped) becomes audible.
    require(engine.postHardwareMidiEvent(unitId, kNoteOn, kMiddleC, 110), "postHardwareMidiEvent rejected note-on");
    std::vector<float> noteAudio;
    renderBlocks(32, &noteAudio);
    const auto noteStats = analyze(noteAudio);
    require(!noteStats.hasInvalid, "Hardware-note audio contains NaN/Inf");
    require(noteStats.peak > 1.0e-4f, "Hardware note-on produced silence with transport stopped");
    require(noteStats.peak < 0.99f, "Hardware-note output peak too high");

    // 2c. Hardware note-off: voice ends, back to silence (no stuck voices).
    engine.postHardwareMidiEvent(unitId, kNoteOff, kMiddleC, 0);
    renderBlocks(96, nullptr);
    std::vector<float> tail;
    renderBlocks(16, &tail);
    const auto tailStats = analyze(tail);
    require(!tailStats.hasInvalid, "Post-note audio contains NaN/Inf");
    require(tailStats.peak < 1.0e-3f, "Hardware voice did not return to silence");

    // 2d. Both queues drain the same block: keyboard + hardware chord sounds.
    require(engine.postLiveMidiEvent(unitId, kNoteOn, kMiddleC, 110), "keyboard chord note rejected");
    require(engine.postHardwareMidiEvent(unitId, kNoteOn, kMiddleC + 7, 110), "hardware chord note rejected");
    std::vector<float> chordAudio;
    renderBlocks(32, &chordAudio);
    const auto chordStats = analyze(chordAudio);
    require(!chordStats.hasInvalid, "Mixed-queue chord audio contains NaN/Inf");
    require(chordStats.peak > 1.0e-4f, "Mixed-queue chord produced silence");
    engine.postLiveMidiEvent(unitId, kNoteOff, kMiddleC, 0);
    engine.postHardwareMidiEvent(unitId, kNoteOff, kMiddleC + 7, 0);
    renderBlocks(112, nullptr);

    // 2e. Hardware events for unknown units are dropped harmlessly.
    require(engine.postHardwareMidiEvent(unitId + 999, kNoteOn, kMiddleC, 100),
            "unknown-unit hardware event rejected at queue");
    std::vector<float> unknownUnit;
    renderBlocks(16, &unknownUnit);
    const auto unknownStats = analyze(unknownUnit);
    require(unknownStats.peak < 1.0e-3f, "Hardware event for unknown unit produced audio");

    std::cout << "Hardware note peak: " << noteStats.peak << ", mixed chord peak: " << chordStats.peak << "\n";
    std::cout << "[PASS] HardwareMidiInputTest\n";
    return 0;
}
