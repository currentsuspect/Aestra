// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PatternTransportPositionTest — regression for the piano-roll playhead bug.
//
// The piano roll's ruler scrub cues a playhead (setPosition + setPlayStartPosition +
// setGlobalSamplePos) and playPatternInArsenal() pushes that exact position into the
// transport command. The engine, however, unconditionally zeroed m_globalSamplePos on
// every stop/restart edge while pattern mode was active:
//
//     if (patternModeNow && (transportStop || transportRestart)) {
//         m_globalSamplePos.store(0, ...);
//     }
//
// Live consequence: "cursor at beat 2 → play starts at beat 1". The scrubbed cue was
// wiped on the restart edge, so playback always began at the loop top. The same wipe
// hit pause: pause preserves the playhead via kTransportPreservePosition, but the stop
// edge zeroed it anyway, so "pause at 2 → play resumes at 1".
//
// The fix: honor the pushed position on START. Only cues at or past the loop
// end (a stale timeline position entering pattern mode) are wrapped back into
// the loop, matching the render path's existing wrap logic. STOP follows the
// T-8 single-stop contract: TrackManager::stop() pushes 0, so the playhead
// resets to the loop top authoritatively — the scrubbed cue is honored by
// playback start, never resurrected by stop.
//
// These assertions fail on the pre-fix engine: pattern-mode play/pause/resume
// lands the playhead at 0 (or one block past it) instead of the cued position.
// The stop scenario documents the T-8 contract the model now emits (stop push
// carries 0); the producer-side reset pin lives in TransportStopResetTest.

#include "Core/AudioCommandQueue.h"
#include "Core/AudioEngine.h"
#include "Models/PatternManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"
#include "Playback/TimelineClock.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using Aestra::Audio::AudioEngine;
using Aestra::Audio::AudioQueueCommand;
using Aestra::Audio::AudioQueueCommandType;
using Aestra::Audio::kTransportPreservePosition;
using Aestra::Audio::MidiBuffer;
using Aestra::Audio::MidiNote;
using Aestra::Audio::MidiPayload;
using Aestra::Audio::PatternID;
using Aestra::Audio::PatternManager;
using Aestra::Audio::PatternPlaybackEngine;
using Aestra::Audio::TimelineClock;
using Aestra::Audio::UnitManager;

namespace {

constexpr uint32_t kFrames = 512;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kSampleRate = 48000;
// 4-beat pattern at 120 BPM and 48 kHz: one beat = 0.5 s = 24000 samples.
constexpr uint64_t kBeatSamples = 24000;
constexpr uint64_t kLoopSamples = 4 * kBeatSamples; // 96000
constexpr double kCueBeat = 2.0;                    // beat 2 of the loop
constexpr uint64_t kCueSamples = static_cast<uint64_t>(kCueBeat * kBeatSamples); // 48000

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) {
        ++g_failures;
    }
}

AudioQueueCommand makeTransportCmd(float playing, uint64_t samplePos) {
    AudioQueueCommand cmd{};
    cmd.type = AudioQueueCommandType::SetTransportState;
    cmd.value1 = playing;
    cmd.samplePos = samplePos;
    return cmd;
}

// --- Consumer side: pattern-mode transport must honor a cued playhead. ----------
//
// Drives real audio callbacks with pattern mode active, exactly like the piano-roll
// play → pause → resume flow does: the panel scrubs the ruler, which pushes the cue
// position; playPatternInArsenal()/TrackManager then push SetTransportState with the
// same position.
void patternPlayHonorsScrubbedCue() {
    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);
    engine.setMetronomeEnabled(false);
    if (!engine.initialize()) {
        check(false, "[play] engine initializes");
        return;
    }
    engine.setPatternPlaybackMode(true, 4.0);
    engine.setGlobalSamplePos(kCueSamples); // the ruler scrub

    std::vector<float> out(static_cast<size_t>(kFrames) * kChannels, 0.0f);

    // Play — the transport command carries the scrubbed cue, as playPatternInArsenal()
    // does (startSeconds = m_position). Pre-fix, the restart edge zeroed it.
    engine.commandQueue().push(makeTransportCmd(1.0f, kCueSamples));
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);

    const uint64_t posAfterPlay = engine.getGlobalSamplePos();
    check(posAfterPlay >= kCueSamples,
          "pattern-mode play from a scrubbed cue starts at the cue, not the loop top"
          " (got " + std::to_string(posAfterPlay) + ", expected >= " + std::to_string(kCueSamples) + ")");
    check(posAfterPlay <= kCueSamples + kFrames,
          "playhead advances from the cue by at most one block (no wrap or reset)");

    engine.setTransportPlaying(false);
}

// Pause in pattern mode must preserve the engine's authoritative playhead (the #590
// sentinel contract), not rewind to the loop top via the stop edge.
void patternPausePreservesPosition() {
    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);
    engine.setMetronomeEnabled(false);
    if (!engine.initialize()) {
        check(false, "[pause] engine initializes");
        return;
    }
    engine.setPatternPlaybackMode(true, 4.0);
    engine.setGlobalSamplePos(0);

    std::vector<float> out(static_cast<size_t>(kFrames) * kChannels, 0.0f);

    engine.commandQueue().push(makeTransportCmd(1.0f, 0));
    for (int i = 0; i < 8; ++i) {
        engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    }
    const uint64_t advancedPos = engine.getGlobalSamplePos();
    check(advancedPos >= static_cast<uint64_t>(kFrames) * 8, "playhead advanced in pattern mode");

    engine.commandQueue().push(makeTransportCmd(0.0f, kTransportPreservePosition));
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    check(engine.getGlobalSamplePos() == advancedPos,
          "pattern-mode pause with the preserve sentinel keeps the playhead (no rewind to loop top)");
    check(!engine.isTransportPlaying(), "pause stops transport");

    engine.setTransportPlaying(false);
}

// Resume after a pattern-mode pause must continue from the paused position, not
// restart at the loop top (the restart edge used to zero the playhead).
void patternResumeFromPausedPosition() {
    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);
    engine.setMetronomeEnabled(false);
    if (!engine.initialize()) {
        check(false, "[resume] engine initializes");
        return;
    }
    engine.setPatternPlaybackMode(true, 4.0);
    engine.setGlobalSamplePos(0);

    std::vector<float> out(static_cast<size_t>(kFrames) * kChannels, 0.0f);

    engine.commandQueue().push(makeTransportCmd(1.0f, 0));
    for (int i = 0; i < 8; ++i) {
        engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    }
    const uint64_t advancedPos = engine.getGlobalSamplePos();

    engine.commandQueue().push(makeTransportCmd(0.0f, kTransportPreservePosition));
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);

    // Resume pushes the preserved position, as TrackManager::play() does after pause.
    engine.commandQueue().push(makeTransportCmd(1.0f, advancedPos));
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);

    const uint64_t posAfterResume = engine.getGlobalSamplePos();
    check(posAfterResume >= advancedPos,
          "pattern-mode resume continues from the paused playhead (got " + std::to_string(posAfterResume) +
              ", expected >= " + std::to_string(advancedPos) + ")");

    engine.setTransportPlaying(false);
}

// Stop in pattern mode must reset to the loop top: TrackManager::stop()
// pushes 0 (T-8 single-stop reset), and the engine lands exactly on the
// pushed position — the drain is authoritative. A scrubbed cue survives only
// until the stop; playback start honors it, stop does not resurrect it.
void patternStopResetsToTop() {
    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);
    engine.setMetronomeEnabled(false);
    if (!engine.initialize()) {
        check(false, "[stop] engine initializes");
        return;
    }
    engine.setPatternPlaybackMode(true, 4.0);
    engine.setGlobalSamplePos(kCueSamples);

    std::vector<float> out(static_cast<size_t>(kFrames) * kChannels, 0.0f);

    engine.commandQueue().push(makeTransportCmd(1.0f, kCueSamples));
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);

    // Stop pushes 0, as TrackManager::stop() does after T-8.
    engine.commandQueue().push(makeTransportCmd(0.0f, 0));
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);

    check(engine.getGlobalSamplePos() == 0,
          "pattern-mode stop resets the playhead to 0 (got " +
              std::to_string(engine.getGlobalSamplePos()) + ", expected 0)");

    // The stopped playhead must not creep while tails render (the
    // advance/fade interplay half of T-8).
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);
    check(engine.getGlobalSamplePos() == 0,
          "pattern-mode stopped playhead stays at 0 (no advance after stop)");

    engine.setTransportPlaying(false);
}

// A cue past the loop end (a stale timeline position entering pattern mode) must be
// wrapped back into the loop, matching the render path's wrap behavior — not honored
// as a raw out-of-range position.
void patternCuePastLoopEndWrapsIntoLoop() {
    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);
    engine.setMetronomeEnabled(false);
    if (!engine.initialize()) {
        check(false, "[wrap] engine initializes");
        return;
    }
    engine.setPatternPlaybackMode(true, 4.0);

    // 5 beats = 120000 samples = 1.25 loop lengths past the start.
    constexpr uint64_t kPastLoopSamples = 5 * kBeatSamples;
    engine.setGlobalSamplePos(kPastLoopSamples);

    std::vector<float> out(static_cast<size_t>(kFrames) * kChannels, 0.0f);
    engine.commandQueue().push(makeTransportCmd(1.0f, kPastLoopSamples));
    engine.processBlock(out.data(), nullptr, kFrames, 0.0);

    const uint64_t posAfterPlay = engine.getGlobalSamplePos();
    check(posAfterPlay < kLoopSamples,
          "pattern-mode play from a cue past the loop end wraps into the loop (got " +
              std::to_string(posAfterPlay) + ", expected < " + std::to_string(kLoopSamples) + ")");

    engine.setTransportPlaying(false);
}

// --- Scheduler contract: refill from the cue, not from the loop top. -------------
//
// After a mid-pattern transport restart, the audio callback's m_patternMonotonicFrame
// is still 0 (it was reset while stopped) until the first playing block republishes it.
// If maintenance refilled from that stale 0, the loop-top events would be queued and a
// playhead at beat 2 would fire them at the buffer edge — "starts from beat 1" again.
// performNonRealtimeMaintenance() must refill from the cued m_globalSamplePos instead.
// This test pins the scheduler contract that guard relies on: refilling at the cue
// must not schedule events that started before the cue.
void schedulerRefillAtCueSkipsPastNotes() {
    TimelineClock clock(120.0);
    PatternManager patterns;
    UnitManager units;
    PatternPlaybackEngine scheduler(&clock, &patterns, &units);

    MidiPayload payload;
    MidiNote earlyNote;
    earlyNote.pitch = 60;
    earlyNote.startBeat = 0.0; // loop top — must NOT fire when cued at beat 2
    earlyNote.durationBeats = 1.0;
    earlyNote.velocity = 0.8f;
    earlyNote.unitId = 1;
    MidiNote cuedNote;
    cuedNote.pitch = 61;
    cuedNote.startBeat = kCueBeat; // the cued playhead
    cuedNote.durationBeats = 1.0;
    cuedNote.velocity = 0.8f;
    cuedNote.unitId = 1;
    payload.notes.push_back(earlyNote);
    payload.notes.push_back(cuedNote);

    const PatternID pid = patterns.createMidiPattern("cue-contract", 4.0, payload);
    scheduler.schedulePatternInstance(pid, 0.0, 1);

    MidiBuffer buf;
    PatternPlaybackEngine::UnitMidiRoute routes[] = {PatternPlaybackEngine::UnitMidiRoute(1, &buf)};

    // Refill at the cued frame (beat 2), as the fixed maintenance path does.
    scheduler.refillWindow(kCueSamples, static_cast<int>(kSampleRate), 4096, kLoopSamples);
    scheduler.processAudio(kCueSamples, static_cast<int>(kFrames), routes, 1);

    bool sawEarlyNote = false;
    bool sawCuedNote = false;
    for (size_t i = 0; i < buf.getEventCount(); ++i) {
        const auto& ev = buf.getEvent(i);
        if (ev.size < 2 || (ev.data[0] & 0xF0) != 0x90) {
            continue;
        }
        if (ev.data[1] == earlyNote.pitch) {
            sawEarlyNote = true;
        }
        if (ev.data[1] == cuedNote.pitch) {
            sawCuedNote = true;
        }
    }

    check(sawCuedNote, "refill at the cue schedules the note at the cued beat");
    check(!sawEarlyNote, "refill at the cue does NOT re-trigger notes that started before the cue");
}

} // namespace

int main() {
    std::cout << "=== Pattern Transport Position (piano-roll playhead) ===\n";
    patternPlayHonorsScrubbedCue();
    patternPausePreservesPosition();
    patternResumeFromPausedPosition();
    patternStopResetsToTop();
    patternCuePastLoopEndWrapsIntoLoop();
    schedulerRefillAtCueSkipsPastNotes();

    std::cout << (g_failures == 0 ? "ALL PASSED\n"
                                  : "FAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}
