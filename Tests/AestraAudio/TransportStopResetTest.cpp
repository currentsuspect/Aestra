// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// TransportStopResetTest — single stop returns to the cue; hard stop lands at 0.
//
// A single STOP returns the playhead to the cue: where playback started, or
// wherever the user last dragged the playhead (every seek path stores it). That
// is what lets a producer loop a section: drag, play, stop, play again. A hard
// stop (double stop) zeroes the cue before calling stop(), so it lands at 0.
//
// Kept from v0.7.1 T-8: the destination rides INSIDE the transport command.
// The audio-thread drain is the single authority, and a UI-side rewind after
// the fact races it (8714ede9 rule). Stop also clears any display override
// (a count-in leftover would pin the UI to a stale position).
//
// Pause is deliberately NOT touched: pause keeps the #590 preserve-sentinel
// path so pause/resume never rewinds under UI lag.
//
// These assertions fail on the T-8 producer, which zeroed the cue and pushed
// 0 on every stop, so a dragged-to position was forgotten.

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

// --- Producer side: stop() carries the cue in the command, clears overrides --

struct Producer {
    TrackManager tm;
    AudioQueueCommand lastCmd{};
    bool sawCommand = false;

    Producer() {
        tm.setOutputSampleRate(48000.0);
        tm.setCommandSink([this](const AudioQueueCommand& cmd) {
            lastCmd = cmd;
            sawCommand = true;
            return true;
        });
    }
};

// Drag to 2 s, play, let the playhead run on, stop: back to 2 s.
void producerStopReturnsToCue() {
    Producer p;
    p.tm.setPosition(2.0);
    p.tm.setPlayStartPosition(2.0);
    p.tm.play();
    check(p.tm.getPlayStartPosition() == 2.0, "play stores the dragged position as the cue");

    // Playback advanced (the UI syncs this from the engine every frame) and a
    // count-in leftover pinned the display.
    p.tm.setPosition(7.5);
    p.tm.setDisplayPositionOverride(7.5);

    p.sawCommand = false;
    p.tm.stop();

    check(p.sawCommand, "stop emits a transport command");
    check(p.lastCmd.type == AudioQueueCommandType::SetTransportState, "stop command is SetTransportState");
    check(p.lastCmd.value1 == 0.0f, "stop command requests stop (playing = 0)");
    check(p.lastCmd.samplePos == 96000,
          "stop carries the cue in the command (authoritative drain lands on it), got " +
              std::to_string(p.lastCmd.samplePos));
    check(p.tm.getPosition() == 2.0, "stop returns the cached UI position to the cue");
    check(p.tm.getPlayStartPosition() == 2.0, "stop keeps the cue, so the next play starts there again");
    check(p.tm.getUIPosition() == 2.0, "stop clears the display override (no stale pin)");
}

// Dragging the playhead DURING playback moves the return point with it.
void producerDragDuringPlaybackMovesTheCue() {
    Producer p;
    p.tm.setPosition(1.0);
    p.tm.play();

    // What the timeline drag does (TrackManagerUIRender playhead drag).
    p.tm.setPosition(4.0);
    p.tm.setPlayStartPosition(4.0);
    p.tm.setPosition(6.0); // playback runs on from the new spot

    p.tm.stop();
    check(p.lastCmd.samplePos == 192000, "stop returns to the spot dragged to mid-playback, got " +
                                             std::to_string(p.lastCmd.samplePos));
    check(p.tm.getPosition() == 4.0, "UI position lands on the dragged-to spot");
}

// Hard stop: the UI (AestraContent::stopFromCurrentFocus) zeroes the cue
// BEFORE stop(), so the command itself carries 0 — no after-the-fact rewind.
void producerHardStopLandsAtZero() {
    Producer p;
    p.tm.setPosition(2.0);
    p.tm.play();
    p.tm.setPosition(5.0);

    p.tm.stop();
    check(p.tm.getPosition() == 2.0, "first stop returns to the cue");

    p.tm.setPlayStartPosition(0.0);
    p.tm.stop();
    check(p.lastCmd.samplePos == 0, "hard stop carries 0 in the command");
    check(p.tm.getPosition() == 0.0, "hard stop resets the UI position to 0");
    check(p.tm.getPlayStartPosition() == 0.0, "hard stop clears the cue");
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
        return true;
    });

    tm.setPosition(2.0);
    tm.pause();

    check(sawCommand, "pause emits a transport command");
    check(lastCmd.samplePos == kTransportPreservePosition,
          "pause still carries the preserve-position sentinel (#590 unchanged)");
}

// --- Consumer side: engine lands exactly on the command, stays put after stop ----

void engineStopsOnCueAndStays() {
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

    // stop() carries the cue — the drain must land on exactly the cue.
    pushTransport(0.0f, kCueSamples);
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    check(engine.getGlobalSamplePos() == kCueSamples,
          "stop lands the playhead on the cue authoritatively (got " +
              std::to_string(engine.getGlobalSamplePos()) + ")");
    check(!engine.isTransportPlaying(), "stop stops transport");

    // No advance-after-stop creep: stopped blocks with tails rendering must not
    // move the playhead (the advance/fade interplay half of T-8).
    for (int i = 0; i < 4; ++i) {
        engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    }
    check(engine.getGlobalSamplePos() == kCueSamples, "playhead stays on the cue while stopped (no creep)");
}

// --- Timeline loop: only a crossing from inside wraps ----------------------
//
// Dragging the playhead past the loop end and pressing play must play from
// there. The engine used to wrap ANY position past the loop end by modulo —
// a rule written for pattern mode — so the timeline folded the cue into the
// loop and the drag looked ignored (the return marker then pointed at a spot
// playback never visited).
void engineTimelineLoop() {
    constexpr uint32_t FRAMES = 512;
    constexpr uint32_t CHANNELS = 2;
    constexpr uint32_t SAMPLE_RATE = 48000;
    constexpr uint64_t LOOP_END_SAMPLES = 192000; // beat 8 @120 BPM, 48 kHz

    const auto makeEngine = [&](AudioEngine& engine) {
        engine.setSampleRate(SAMPLE_RATE);
        engine.setBufferConfig(FRAMES, CHANNELS);
        engine.setMetronomeEnabled(false);
        engine.setBPM(120.0f);
        engine.setLoopRegion(0.0, 8.0);
        engine.setLoopEnabled(true);
        return engine.initialize();
    };
    std::vector<float> out(static_cast<size_t>(FRAMES) * CHANNELS, 0.0f);
    const auto play = [&](AudioEngine& engine, uint64_t from, int blocks) {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTransportState;
        cmd.value1 = 1.0f;
        cmd.samplePos = from;
        engine.commandQueue().push(cmd);
        for (int i = 0; i < blocks; ++i) {
            engine.processBlock(out.data(), nullptr, FRAMES, 0.0);
        }
    };

    {
        AudioEngine engine;
        if (!makeEngine(engine)) {
            check(false, "engine initializes (past-end)");
            return;
        }
        constexpr uint64_t CUE_SAMPLES = 480000; // beat 20, well past the loop end
        play(engine, CUE_SAMPLES, 4);
        check(engine.getGlobalSamplePos() == CUE_SAMPLES + 4ull * FRAMES,
              "timeline play past the loop end runs on from the cue, not folded into the loop (got " +
                  std::to_string(engine.getGlobalSamplePos()) + ")");
    }
    {
        AudioEngine engine;
        if (!makeEngine(engine)) {
            check(false, "engine initializes (crossing)");
            return;
        }
        // Start two blocks before the loop end: the crossing must still wrap.
        // LOOP_END_SAMPLES is a whole number of blocks, so block 2 ends EXACTLY
        // on the loop end without wrapping (the wrap test is next > loopEnd),
        // and block 3 then starts AT the loop end. That arrival came from
        // advancing inside the loop, so it must wrap — a strict
        // `current < loopEnd` rule would run on past the loop here.
        play(engine, LOOP_END_SAMPLES - 2ull * FRAMES, 4);
        check(engine.getGlobalSamplePos() < LOOP_END_SAMPLES,
              "a playhead crossing the loop end from inside still wraps to the loop (got " +
                  std::to_string(engine.getGlobalSamplePos()) + ")");
    }
    {
        AudioEngine engine;
        if (!makeEngine(engine)) {
            check(false, "engine initializes (start at end)");
            return;
        }
        // Playback STARTED exactly on the loop end: a grid-snapped drag onto
        // the loop's end bar. It did not arrive from inside, so it runs on.
        play(engine, LOOP_END_SAMPLES, 4);
        check(engine.getGlobalSamplePos() == LOOP_END_SAMPLES + 4ull * FRAMES,
              "timeline play started exactly on the loop end runs on (got " +
                  std::to_string(engine.getGlobalSamplePos()) + ")");
    }
    {
        AudioEngine engine;
        if (!makeEngine(engine)) {
            check(false, "engine initializes (pattern)");
            return;
        }
        // Pattern mode keeps its wrap-from-anywhere rule: the pattern is the whole world.
        engine.setPatternPlaybackMode(true, 8.0);
        play(engine, 480000, 2);
        check(engine.getGlobalSamplePos() < LOOP_END_SAMPLES,
              "pattern mode still wraps a position past the loop end (got " +
                  std::to_string(engine.getGlobalSamplePos()) + ")");
    }
}

} // namespace

int main() {
    std::cout << "=== Transport Stop Reset (single stop -> cue, hard stop -> 0) ===\n";
    producerStopReturnsToCue();
    producerDragDuringPlaybackMovesTheCue();
    producerHardStopLandsAtZero();
    producerPauseStillPreserves();
    engineStopsOnCueAndStays();
    engineTimelineLoop();

    std::cout << (g_failures == 0 ? "ALL PASSED\n"
                                  : "FAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}