// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// TransportStopResetTest — regression for v0.7.1 T-8 (single-stop playhead reset).
//
// Single STOP must land the transport at 0 authoritatively, glitch-free.
// The model commits the reset INTO the transport command — the audio-thread
// drain is the single authority, and a UI-side rewind after the fact races
// it (8714ede9 rule). Stop also drops the stored cue (play() must not
// resurrect a scrubbed position) and clears any display override (a count-in
// leftover would pin the UI to a stale position — the "display-override on
// soft stop" half of T-8).
//
// Pause is deliberately NOT touched: pause keeps the #590 preserve-sentinel
// path so pause/resume never rewinds under UI lag.
//
// These assertions fail on the pre-fix producer: stop() pushed the stored
// play-start cue, so a scrubbed position X survived the stop and the playhead
// "went back" to the cue instead of resetting to 0.

#include "Core/AudioCommandQueue.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using Aestra::Audio::AudioEngine;
using Aestra::Audio::AudioQueueCommand;
using Aestra::Audio::AudioQueueCommandType;
using Aestra::Audio::kTransportPreservePosition;
using Aestra::Audio::TrackManager;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) {
        ++g_failures;
    }
}

// --- Producer side: stop() commits the 0 reset, drops the cue, clears overrides --
//
// The reported bug ("single stop → playhead goes back to the cue"): a scrubbed
// piano-roll position X lived in playStartPosition, stop() echoed X into the
// transport command, and the authoritative drain landed there. UI-side rewinds
// raced the drain (flash to 0, snap back). The fix must make the reset part of
// the command itself.
void producerStopEmitsZeroReset() {
    constexpr double kSampleRate = 48000.0;

    TrackManager tm;
    tm.setOutputSampleRate(kSampleRate);

    AudioQueueCommand lastCmd{};
    bool sawCommand = false;
    tm.setCommandSink([&](const AudioQueueCommand& cmd) {
        lastCmd = cmd;
        sawCommand = true;
    });

    // A scrubbed playhead + display override (count-in leftover) pre-stop.
    tm.setPosition(2.0);
    tm.setPlayStartPosition(2.0);
    tm.setDisplayPositionOverride(2.0);

    tm.stop();

    check(sawCommand, "stop emits a transport command");
    check(lastCmd.type == AudioQueueCommandType::SetTransportState,
          "stop command is SetTransportState");
    check(lastCmd.value1 == 0.0f, "stop command requests stop (playing = 0)");
    check(lastCmd.samplePos == 0,
          "stop carries the 0 reset in the command (authoritative drain lands at 0)");
    check(tm.getPosition() == 0.0, "stop resets the cached UI position to 0");
    check(tm.getPlayStartPosition() == 0.0,
          "stop drops the stored cue — play() cannot resurrect a scrubbed position");
    check(tm.getUIPosition() == 0.0, "stop clears the display override (no stale pin)");
}

// Pause must stay on the preserve-sentinel path — T-8 must not regress #590.
void producerPauseStillPreserves() {
    constexpr double kSampleRate = 48000.0;

    TrackManager tm;
    tm.setOutputSampleRate(kSampleRate);

    AudioQueueCommand lastCmd{};
    bool sawCommand = false;
    tm.setCommandSink([&](const AudioQueueCommand& cmd) {
        lastCmd = cmd;
        sawCommand = true;
    });

    tm.setPosition(2.0);
    tm.pause();

    check(sawCommand, "pause emits a transport command");
    check(lastCmd.samplePos == kTransportPreservePosition,
          "pause still carries the preserve-position sentinel (#590 unchanged)");
}

// --- Consumer side: engine lands exactly on the command, stays put after stop ----

void engineStopsAtZeroAndStays() {
    constexpr uint32_t kFrames = 512;
    constexpr uint32_t kChannels = 2;
    constexpr uint32_t kSampleRate = 48000;
    constexpr int kBlocksWhilePlaying = 8;

    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);
    engine.setMetronomeEnabled(false);
    if (!engine.initialize()) {
        check(false, "engine initializes");
        return;
    }
    engine.setGlobalSamplePos(0);

    std::vector<float> out(static_cast<size_t>(kFrames) * kChannels, 0.0f);
    auto pushTransport = [&](float playing, uint64_t samplePos) {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = playing;
        cmd.samplePos = samplePos;
        engine.commandQueue().push(cmd);
    };

    // Play from the cue (2 beats in), let the playhead advance well past it.
    constexpr uint64_t kCueSamples = 48000; // beat 2 @120 BPM, 48 kHz
    pushTransport(1.0f, kCueSamples);
    for (int i = 0; i < kBlocksWhilePlaying; ++i) {
        engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    }
    const uint64_t advancedPos = engine.getGlobalSamplePos();
    check(advancedPos >= kCueSamples + static_cast<uint64_t>(kFrames) * kBlocksWhilePlaying,
          "playhead advanced past the cue during playback");

    // The new model stop(): command carries 0 — the drain must land on exactly 0.
    pushTransport(0.0f, 0);
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    check(engine.getGlobalSamplePos() == 0,
          "stop lands the playhead at 0 authoritatively (got " +
              std::to_string(engine.getGlobalSamplePos()) + ")");
    check(!engine.isTransportPlaying(), "stop stops transport");

    // No advance-after-stop creep: stopped blocks with tails rendering must not
    // move the playhead (the advance/fade interplay half of T-8).
    for (int i = 0; i < 4; ++i) {
        engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    }
    check(engine.getGlobalSamplePos() == 0, "playhead stays at 0 while stopped (no creep)");
}

} // namespace

int main() {
    std::cout << "=== Transport Stop Reset (T-8, single-stop playhead) ===\n";
    producerStopEmitsZeroReset();
    producerPauseStillPreserves();
    engineStopsAtZeroAndStays();

    std::cout << (g_failures == 0 ? "ALL PASSED\n"
                                  : "FAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}