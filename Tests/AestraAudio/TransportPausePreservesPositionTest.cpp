// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// TransportPausePreservesPositionTest — regression for #590.
//
// Pause must stop transport without rewinding. TrackManager::pause() historically
// committed m_position (the UI-cached playhead, synced only at UI-frame cadence)
// into the SetTransportState command. Under rapid pause/play toggling that cache
// lags the audio thread by several buffers, so applying it moved the transport
// backward and made the result depend on UI/audio timing.
//
// The fix: pause sends the kTransportPreservePosition sentinel; the audio thread
// resolves it to its own authoritative m_globalSamplePos. This test pins both
// halves — the producer emits the sentinel (not the stale UI position), and the
// engine preserves the advanced playhead across a pause that follows several
// audio callbacks of drift.

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

// --- Producer side: pause() must not commit the UI-cached position ------------
//
// This is the direct encoding of the reported bug: with a command sink capturing
// the emitted transport command, a pause after the UI cache has fallen behind the
// audio thread must carry the preserve sentinel, never the stale sample position.
void producerPauseSendsPreserveSentinel() {
    constexpr double kSampleRate = 48000.0;

    TrackManager tm;
    tm.setOutputSampleRate(kSampleRate);

    AudioQueueCommand lastCmd{};
    bool sawCommand = false;
    tm.setCommandSink([&](const AudioQueueCommand& cmd) {
        lastCmd = cmd;
        sawCommand = true;
    });

    // Model UI lag: the audio thread has advanced well past the UI cache, but the
    // UI-synced m_position still reflects an older sample position (2.0 s here).
    // Pre-fix, pause() would send exactly this stale position.
    const double stalePositionSeconds = 2.0;
    tm.setPosition(stalePositionSeconds);
    const uint64_t stalePositionSamples =
        static_cast<uint64_t>(stalePositionSeconds * kSampleRate);

    tm.pause();

    check(sawCommand, "pause emits a transport command");
    check(lastCmd.type == AudioQueueCommandType::SetTransportState,
          "pause command is SetTransportState");
    check(lastCmd.value1 == 0.0f, "pause command requests stop (playing = 0)");
    check(lastCmd.samplePos == kTransportPreservePosition,
          "pause carries the preserve-position sentinel");
    // The heart of #590: the sentinel is NOT the stale UI-cached position.
    check(lastCmd.samplePos != stalePositionSamples,
          "pause does NOT commit the stale UI-cached sample position");

    // The UI cache itself is left untouched by the pause echo — it is refreshed
    // from the engine by syncPositionFromEngine, not clobbered by the sentinel.
    tm.onTransportStateApplied(false, kTransportPreservePosition, kSampleRate);
    check(tm.getPosition() == stalePositionSeconds,
          "sentinel echo leaves the cached UI position unchanged");
}

// --- Consumer side: engine preserves the playhead across a paused block --------
//
// Drives real audio callbacks so the engine's authoritative playhead advances,
// then applies the pause sentinel and asserts the playhead is preserved — the
// end-to-end "no rewind under UI lag across multiple callbacks" guarantee.
void enginePreservesPlayheadOnPause() {
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

    // Start playing from 0, then run several callbacks so the playhead advances.
    pushTransport(1.0f, 0);
    for (int i = 0; i < kBlocksWhilePlaying; ++i) {
        engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    }
    const uint64_t advancedPos = engine.getGlobalSamplePos();
    check(advancedPos >= static_cast<uint64_t>(kFrames) * kBlocksWhilePlaying,
          "playhead advanced across multiple audio callbacks");

    // Pause using the preserve sentinel — as TrackManager::pause() now does. The
    // engine must keep its own advanced playhead rather than seek anywhere.
    pushTransport(0.0f, kTransportPreservePosition);
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    check(engine.getGlobalSamplePos() == advancedPos,
          "pause with the sentinel preserves the engine playhead (no rewind)");
    check(!engine.isTransportPlaying(), "pause stops transport");

    // A paused engine does not advance on subsequent callbacks.
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    check(engine.getGlobalSamplePos() == advancedPos,
          "playhead stays put while paused");

    // A genuine seek (a real absolute position, not the sentinel) still applies —
    // the sentinel path must not swallow ordinary stop/seek commands.
    const uint64_t seekPos = 1000;
    pushTransport(0.0f, seekPos);
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    check(engine.getGlobalSamplePos() == seekPos,
          "an explicit stop/seek to a real position still moves the playhead");

    // A seek immediately followed by a pause in the SAME command block must honor
    // the seek: the sentinel resolves against the in-block accumulator, not the
    // pre-block playhead, so the preceding seek is not reverted.
    const uint64_t sameBlockSeek = seekPos + 500;
    pushTransport(1.0f, sameBlockSeek);
    pushTransport(0.0f, kTransportPreservePosition);
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    check(engine.getGlobalSamplePos() == sameBlockSeek,
          "pause following a seek in the same block preserves the seek position");
}

} // namespace

int main() {
    std::cout << "=== Transport Pause Preserves Position (#590) ===\n";
    producerPauseSendsPreserveSentinel();
    enginePreservesPlayheadOnPause();

    std::cout << (g_failures == 0 ? "ALL PASSED\n"
                                  : "FAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}
