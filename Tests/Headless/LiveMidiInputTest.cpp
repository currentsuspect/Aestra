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

double toneAmplitude(const std::vector<float>& interleaved, double frequency, uint32_t sampleRate,
                     size_t skipFrames = 1024) {
    const size_t frameCount = interleaved.size() / 2;
    if (frameCount <= skipFrames) {
        return 0.0;
    }
    double real = 0.0;
    double imag = 0.0;
    const double omega = 2.0 * 3.14159265358979 * frequency / static_cast<double>(sampleRate);
    for (size_t frame = skipFrames; frame < frameCount; ++frame) {
        const double sample = static_cast<double>(interleaved[frame * 2]);
        const double phase = omega * static_cast<double>(frame);
        real += sample * std::cos(phase);
        imag -= sample * std::sin(phase);
    }
    return 2.0 * std::sqrt(real * real + imag * imag) / static_cast<double>(frameCount - skipFrames);
}

bool hasMidiEvent(const Aestra::Audio::MidiBuffer& midi, uint8_t status, uint8_t pitch) {
    for (size_t index = 0; index < midi.getEventCount(); ++index) {
        const auto& event = midi.getEvent(index);
        if ((event.data[0] & 0xF0u) == status && event.data[1] == pitch) {
            return true;
        }
    }
    return false;
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
    // Default envelope on purpose: a 250 ms one-shot under the default 300 ms
    // release is the #452 shape, so this doubles as engine-level regression
    // coverage (primary coverage: SamplerOneShotEnvelopeTest).
    {
        const uint32_t sampleFrames = sampleRate / 4;
        std::vector<float> data(sampleFrames, 0.0f);
        for (uint32_t i = 0; i < sampleFrames; ++i) {
            data[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 220.0 * (static_cast<double>(i) / sampleRate));
        }
        require(sampler->loadSampleData("live-midi-test-tone", std::move(data), sampleRate, 1),
                "loadSampleData failed");
    }

    UnitID unitId = unitManager.createUnit("Live Sampler", UnitType::Sampler);
    unitManager.attachPlugin(unitId, "com.Aestrastudios.sampler", sampler);
    unitManager.setUnitEnabled(unitId, true);
    unitManager.setUnitMixerChannel(unitId, MASTER_MIXER_CHANNEL_ID); // route directly to master in Arsenal mode
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
    const double rootAmplitude = toneAmplitude(chordAudio, 220.0, sampleRate);
    const double fifthFrequency = 220.0 * std::pow(2.0, 7.0 / 12.0);
    const double fifthAmplitude = toneAmplitude(chordAudio, fifthFrequency, sampleRate);
    const double rootOnlyFifthLeak = toneAmplitude(noteAudio, fifthFrequency, sampleRate);
    require(rootAmplitude > 1.0e-3, "Chord render is missing its root frequency");
    require(fifthAmplitude > 1.0e-3, "Chord render is missing its transposed fifth");
    require(fifthAmplitude > rootOnlyFifthLeak * 4.0,
            "Transposed fifth is not distinguishable from root-only spectral leakage");
    engine.postLiveMidiEvent(unitId, kNoteOff, kMiddleC, 0);
    engine.postLiveMidiEvent(unitId, kNoteOff, kMiddleC + 7, 0);
    renderBlocks(112, nullptr);

    // ---------------- 5. Pattern one-shots preserve Piano Roll pitch and play
    // through their natural sample end instead of being gated by the short
    // visual note length. Looping sampler modes still receive note-offs.
    PatternID chordPattern = patternManager.createPattern();
    auto* pattern = patternManager.getPattern(chordPattern);
    require(pattern != nullptr, "Failed to create chord pattern");
    pattern->type = PatternSource::Type::Midi;
    pattern->lengthBeats = 4.0;
    pattern->payload = MidiPayload{};
    auto& chordNotes = std::get<MidiPayload>(pattern->payload).notes;
    MidiNote rootNote;
    rootNote.pitch = kMiddleC;
    rootNote.startBeat = 0.0;
    rootNote.durationBeats = 0.25;
    rootNote.velocity = 1.0f;
    rootNote.unitId = unitId;
    chordNotes.push_back(rootNote);
    MidiNote fifthNote = rootNote;
    fifthNote.pitch = kMiddleC + 7;
    chordNotes.push_back(fifthNote);

    PatternPlaybackEngine chordScheduler(&clock, &patternManager, &unitManager);
    chordScheduler.schedulePatternInstance(chordPattern, 0.0, 1);
    chordScheduler.refillWindow(0, sampleRate, sampleRate);
    MidiBuffer scheduledMidi;
    PatternPlaybackEngine::UnitMidiRoute scheduledRoute{unitId, &scheduledMidi};
    chordScheduler.processAudio(0, sampleRate, &scheduledRoute, 1);
    require(hasMidiEvent(scheduledMidi, kNoteOn, kMiddleC), "Pattern chord lost its root pitch");
    require(hasMidiEvent(scheduledMidi, kNoteOn, kMiddleC + 7), "Pattern chord lost its fifth pitch");
    require(!hasMidiEvent(scheduledMidi, kNoteOff, kMiddleC), "One-shot root was gated by Piano Roll note length");
    require(!hasMidiEvent(scheduledMidi, kNoteOff, kMiddleC + 7), "One-shot fifth was gated by Piano Roll note length");

    sampler->setLoopEnabled(true);
    PatternPlaybackEngine loopScheduler(&clock, &patternManager, &unitManager);
    loopScheduler.schedulePatternInstance(chordPattern, 0.0, 2);
    loopScheduler.refillWindow(0, sampleRate, sampleRate);
    MidiBuffer loopMidi;
    PatternPlaybackEngine::UnitMidiRoute loopRoute{unitId, &loopMidi};
    loopScheduler.processAudio(0, sampleRate, &loopRoute, 1);
    require(hasMidiEvent(loopMidi, kNoteOff, kMiddleC), "Looping root lost its Piano Roll note-off");
    require(hasMidiEvent(loopMidi, kNoteOff, kMiddleC + 7), "Looping fifth lost its Piano Roll note-off");
    sampler->setLoopEnabled(false);

    // ---------------- 6. Events for unknown units are dropped harmlessly.
    require(engine.postLiveMidiEvent(unitId + 999, kNoteOn, kMiddleC, 100), "unknown-unit event rejected at queue");
    std::vector<float> unknownUnit;
    renderBlocks(16, &unknownUnit);
    const auto unknownStats = analyze(unknownUnit);
    require(unknownStats.peak < 1.0e-3f, "Event for unknown unit produced audio");

    std::cout << "Live note peak: " << noteStats.peak << ", chord peak: " << chordStats.peak << "\n";
    std::cout << "[PASS] LiveMidiInputTest\n";
    return 0;
}
