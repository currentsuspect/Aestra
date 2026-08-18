// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// CountInRecordingTest — Count-in → recording contract.
//
// Regression for the count-in/recording sweep: "Record → Count-in → Recording
// starts". The count-in state machine lives on TrackManager (the canonical
// transport contract) so this runs headless, no audio hardware required.
//
// Covered:
//   - beginCountIn() is a universal lead-in (no record arm required).
//   - Record (arm) → count-in pending → completeCountIn() starts playback.
//   - The deferred recording start aligns capture to the post-count-in beat:
//     samples fed before the target beat are not captured; the committed take
//     starts exactly on it.
//   - cancelCountIn() drops the pending count-in and the deferred alignment.
//   - A non-armed count-in must not leave a stale deferred start that would
//     misalign a later, non-count-in recording.

#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"
#include "../Support/TestTempDirectory.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr double kBpm = 120.0; // 2 beats/second

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "  FAIL: " << label << "\n";
        ++g_failures;
    } else {
        std::cout << "  PASS: " << label << "\n";
    }
}

std::shared_ptr<TrackManager> makeRecorder() {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);
    tm->setInputChannelCount(1);
    tm->setMaxRecordingSeconds(5.0);
    return tm;
}

/** Create a lane, a channel, and a Track owning the lane and routing to the
 *  channel. Returns 0 on failure (caller decides whether to arm). */
uint64_t makeTrack(TrackManager& tm, const std::string& name) {
    const PlaylistLaneID laneId = tm.getPlaylistModel().createLane(name);
    MixerChannel* ch = tm.addChannel(name);
    if (!ch) {
        return 0;
    }
    ch->setInputChannelIndex(0);
    return tm.createTrack(laneId, name, ch->getChannelId());
}

/** Feed a block of frames at a given transport position (simulates the engine
 *  advancing to that position before the block). */
void feedAt(TrackManager& tm, double positionSeconds, double blockSeconds) {
    tm.setPosition(positionSeconds);
    const uint32_t frames = static_cast<uint32_t>(blockSeconds * static_cast<double>(kSampleRate));
    std::vector<float> input(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        input[i] = 0.25f * std::sin(2.0 * 3.14159265 * 440.0 * static_cast<double>(i) / static_cast<double>(kSampleRate));
    }
    tm.processInput(input.data(), frames);
}

/** The lane holding a recorded take clip, or an invalid id. */
PlaylistLaneID takeLaneOf(TrackManager& tm, uint64_t trackId) {
    auto* track = tm.getTrack(trackId);
    if (!track) {
        return {};
    }
    for (const auto& laneId : track->laneIds) {
        auto* lane = tm.getPlaylistModel().getLane(laneId);
        if (lane && !lane->clips.empty()) {
            return laneId;
        }
    }
    return {};
}

// Test 1: Count-in is a universal lead-in (requires no record arm), but is
// refused while a count-in is already pending or the transport is rolling.
bool testCountInLeadInWithoutArm() {
    std::cout << "  [1/4] Count-in runs without record arm... ";
    auto tm = makeRecorder();

    // No record arm, not even a track: count-in still begins (lead-in before
    // playback). With no capture armed it degrades to a plain count-in + play.
    check(tm->beginCountIn(4, 0.0), "accepted with no record arm");
    check(tm->isCountInPending(), "count-in pending after begin");
    tm->cancelCountIn();
    check(!tm->isCountInPending(), "no longer pending after cancel");

    const uint64_t trackId = makeTrack(*tm, "Track");
    if (trackId == 0) {
        std::cerr << "FAILED: track creation failed\n";
        return false;
    }
    check(tm->beginCountIn(4, 0.0), "accepted with an unarmed track");
    check(!tm->beginCountIn(4, 0.0), "refused while already pending");
    tm->completeCountIn();
    check(tm->isPlaying(), "transport started after the count-in completed");
    check(!tm->isCountInPending(), "pending cleared after completion");
    check(!tm->beginCountIn(4, 0.0), "refused while transport is rolling");

    std::cout << "PASSED\n";
    return true;
}

// Test 2: Record → Count-in → Recording starts — the P0 regression.
bool testCountInRecordingFlow() {
    std::cout << "  [2/4] Record → count-in → recording starts... ";
    auto tm = makeRecorder();
    Aestra::Tests::ScopedTempDirectory dir{"CountInRecording"};
    tm->setRecordingProjectPath((dir.path() / "countin.aes").string());

    const uint64_t trackId = makeTrack(*tm, "Take");
    if (trackId == 0) {
        std::cerr << "FAILED: track creation failed\n";
        return false;
    }
    tm->setTrackArmed(trackId, true);
    tm->record(); // Record press arms the transport.

    // Pressing Play (with count-in) pins the position and starts the 4-beat
    // count-in; the transport must NOT be rolling yet.
    check(tm->beginCountIn(4, 1.0), "count-in began at 1.0s (beat 2.0)");
    check(tm->isCountInPending(), "transport is in the count-in phase");
    check(!tm->isPlaying(), "transport not rolling during count-in");

    // Engine count-in metronome finished → app calls completeCountIn().
    tm->completeCountIn();
    check(tm->isPlaying(), "transport starts after count-in");
    check(!tm->isCountInPending(), "count-in cleared after completion");

    // Engine confirms transport rolling; capture begins. To prove the
    // deferred alignment contract, the capture starts BEFORE the transport has
    // reached the target beat — the block fed there must not be recorded.
    tm->setPosition(0.75);
    tm->onTransportStateApplied(true, static_cast<uint64_t>(0.75 * kSampleRate),
                                static_cast<double>(kSampleRate));
    check(tm->isRecording(), "capture session began when transport applied");

    feedAt(*tm, 0.75, 0.25);  // beat 1.5 → below deferred 2.0, must be skipped
    feedAt(*tm, 1.0, 0.25);   // beat 2.0 → capture starts
    feedAt(*tm, 1.25, 0.25);  // beat 2.5 → captured

    tm->onTransportStateApplied(false, static_cast<uint64_t>(1.5 * kSampleRate),
                                static_cast<double>(kSampleRate));
    check(!tm->isRecording(), "capture finalized on stop");

    const PlaylistLaneID takeLane = takeLaneOf(*tm, trackId);
    check(takeLane.isValid(), "recorded take landed on a lane");
    if (!takeLane.isValid()) {
        return false;
    }
    auto* lane = tm->getPlaylistModel().getLane(takeLane);
    const ClipInstance& clip = lane->clips.front();
    // 0.5s captured (beats 2.0→3.0) — the 0.25s fed before the target was cut.
    check(std::abs(clip.startBeat - 2.0) < 0.001, "take aligned to the post-count-in beat (start 2.0)");
    check(std::abs(clip.durationBeats - 1.0) < 0.001, "take length excludes pre-count-in audio (1.0 beat)");

    std::cout << "PASSED\n";
    return true;
}

// Test 3: Cancelling the count-in drops the pending state and the alignment.
bool testCountInCancelDropsDeferral() {
    std::cout << "  [3/4] Cancel drops pending state and deferral... ";
    auto tm = makeRecorder();
    Aestra::Tests::ScopedTempDirectory dir{"CountInCancel"};
    tm->setRecordingProjectPath((dir.path() / "cancel.aes").string());

    const uint64_t trackId = makeTrack(*tm, "Take");
    if (trackId == 0) {
        std::cerr << "FAILED: track creation failed\n";
        return false;
    }
    tm->setTrackArmed(trackId, true);
    tm->record();
    check(tm->beginCountIn(4, 1.0), "count-in began");
    tm->cancelCountIn();
    check(!tm->isCountInPending(), "pending state cleared");

    // A subsequent plain play must not defer recording.
    tm->setPosition(0.5);
    tm->onTransportStateApplied(true, static_cast<uint64_t>(0.5 * kSampleRate),
                                static_cast<double>(kSampleRate));
    feedAt(*tm, 0.5, 0.25);
    feedAt(*tm, 0.75, 0.25);
    tm->onTransportStateApplied(false, static_cast<uint64_t>(1.0 * kSampleRate),
                                static_cast<double>(kSampleRate));

    const PlaylistLaneID takeLane = takeLaneOf(*tm, trackId);
    check(takeLane.isValid(), "take committed after cancel + play");
    if (!takeLane.isValid()) {
        return false;
    }
    auto* lane = tm->getPlaylistModel().getLane(takeLane);
    const ClipInstance& clip = lane->clips.front();
    // 0.5s from beat 1.0 → 2.0, no deferral involved.
    check(std::abs(clip.startBeat - 1.0) < 0.001, "no deleted deferral — take starts at play position");
    check(std::abs(clip.durationBeats - 1.0) < 0.001, "full 0.5s captured");

    std::cout << "PASSED\n";
    return true;
}

// Test 4: A non-armed count-in must not poison a later recording — the stale
// deferred start (from the plain lead-in) must not skip frames of a subsequent,
// non-count-in capture.
bool testUnarmedCountInDoesNotPoisonLaterRecording() {
    std::cout << "  [4/5] Unarmed count-in does not misalign a later recording... ";
    auto tm = makeRecorder();
    Aestra::Tests::ScopedTempDirectory dir{"CountInNoPoison"};
    tm->setRecordingProjectPath((dir.path() / "nopoison.aes").string());

    // Plain lead-in at 1.0s (beat 2.0), with no record arm and no armed track.
    check(tm->beginCountIn(4, 1.0), "unarmed count-in began");
    tm->completeCountIn();
    check(!tm->isCountInPending(), "count-in completed");

    // Now arm a track and record from a DIFFERENT position, without count-in.
    const uint64_t trackId = makeTrack(*tm, "Take");
    check(trackId != 0, "track created");
    if (trackId == 0) {
        return false;
    }
    tm->setTrackArmed(trackId, true);
    tm->record(); // arm the transport

    tm->setPosition(0.5);
    tm->onTransportStateApplied(true, static_cast<uint64_t>(0.5 * kSampleRate),
                                static_cast<double>(kSampleRate));
    feedAt(*tm, 0.5, 0.25); // beat 1.0 → 1.5
    feedAt(*tm, 0.75, 0.25); // beat 1.5 → 2.0
    tm->onTransportStateApplied(false, static_cast<uint64_t>(1.0 * kSampleRate),
                                static_cast<double>(kSampleRate));

    const PlaylistLaneID takeLane = takeLaneOf(*tm, trackId);
    check(takeLane.isValid(), "take committed after the plain lead-in");
    if (!takeLane.isValid()) {
        return false;
    }
    auto* lane = tm->getPlaylistModel().getLane(takeLane);
    const ClipInstance& clip = lane->clips.front();
    // Full 0.5s at beat 1.0 — the stale beat-2.0 deferral must be gone.
    check(std::abs(clip.startBeat - 1.0) < 0.001, "capture starts at the new play position, not the old count-in beat");
    check(std::abs(clip.durationBeats - 1.0) < 0.001, "no frames skipped by a stale deferral");

    std::cout << "PASSED\n";
    return true;
}

// Test 5: CompleteCountIn is a no-op without a pending count-in.
bool testCompleteWithoutPendingIsNoop() {
    std::cout << "  [5/5] completeCountIn no-ops when idle... ";
    auto tm = makeRecorder();
    const uint64_t trackId = makeTrack(*tm, "Track");
    if (trackId == 0) {
        std::cerr << "FAILED: track creation failed\n";
        return false;
    }
    tm->setTrackArmed(trackId, true);
    tm->record();
    tm->completeCountIn();
    check(!tm->isCountInPending(), "no pending state appeared");
    check(!tm->isPlaying(), "transport did not start unexpectedly");

    std::cout << "PASSED\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=== Count-In Recording Tests ===\n";
    std::cout << "(Count-in → recording alignment, headless)\n\n";

    const bool ok = testCountInLeadInWithoutArm() & testCountInRecordingFlow() & testCountInCancelDropsDeferral() &
                    testUnarmedCountInDoesNotPoisonLaterRecording() & testCompleteWithoutPendingIsNoop();
    check(g_failures == 0, "no failures");

    std::cout << "\n";
    if (!ok || g_failures > 0) {
        std::cout << "FAILED: " << g_failures << " count-in recording assertions failed.\n";
        return 1;
    }
    std::cout << "All count-in recording tests passed.\n";
    return 0;
}
