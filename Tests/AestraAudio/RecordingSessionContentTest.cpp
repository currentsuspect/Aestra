// © 2026 Aestra Studios — All Rights Reserved.
// RecordingSessionContentTest — #845 regression: recording must arm, roll,
// and land a take (with working undo) in a session that already contains
// clips and patterns, including under the pattern-mode loop override.
//
// The 2026-08-22 production repro: a project with existing clips and patterns
// refused to record, while empty projects recorded fine. The transport-state
// suspects are the pattern-mode loop override and the timeline loop region —
// both clamp the deferred capture start (#845 machinery on TrackManager).

#include "../Support/TestTempDirectory.h"
#include "Commands/AddClipCommand.h"
#include "Commands/CommandHistory.h"
#include "Models/ClipInstance.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <iostream>
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

uint64_t makeTrack(TrackManager& tm, const std::string& name) {
    const PlaylistLaneID laneId = tm.getPlaylistModel().createLane(name);
    MixerChannel* ch = tm.addChannel(name);
    if (!ch) {
        return 0;
    }
    ch->setInputChannelIndex(0);
    return tm.createTrack(laneId, name, ch->getChannelId());
}

// Seed the session with the content the #845 repro had: an existing lane
// holding a clip, and a pattern in the session.
void seedSessionContent(TrackManager& tm, PlaylistLaneID& existingLaneOut) {
    existingLaneOut = tm.getPlaylistModel().createLane("Existing");
    const PatternID patternId = tm.getPatternManager().createAudioPattern("existing-pattern", 4.0, {});
    ClipInstance existingClip;
    existingClip.id = ClipInstanceID::generate();
    existingClip.name = "existing";
    existingClip.patternId = patternId;
    existingClip.sourceId = patternId.value;
    existingClip.startBeat = 0.0;
    existingClip.durationBeats = 4.0;
    auto addClip = std::make_shared<AddClipCommand>(tm.getPlaylistModel(), existingLaneOut, existingClip);
    addClip->execute();
}

void feedAt(TrackManager& tm, double positionSeconds, double blockSeconds) {
    tm.setPosition(positionSeconds);
    const uint32_t frames = static_cast<uint32_t>(blockSeconds * static_cast<double>(kSampleRate));
    std::vector<float> input(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        input[i] =
            0.25f * std::sin(2.0 * 3.14159265 * 440.0 * static_cast<double>(i) / static_cast<double>(kSampleRate));
    }
    tm.processInput(input.data(), frames);
}

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

size_t totalLaneCount(TrackManager& tm) {
    return tm.getPlaylistModel().getLaneCount();
}

// The #845 repro: session has clips + patterns, pattern-mode loop override is
// active, and the count-in cue sits beyond the pattern loop end. Capture must
// clamp into the reachable region, start, and land a take.
bool testRecordLandsTakeInPatternModeSession() {
    std::cout << "  [1/4] Take lands with pattern override active and a cue beyond the pattern end... ";
    auto tm = makeRecorder();
    Aestra::Tests::ScopedTempDirectory dir{"RecSessionContent"};
    tm->setRecordingProjectPath((dir.path() / "session.aes").string());

    PlaylistLaneID existingLane{};
    seedSessionContent(*tm, existingLane);
    check(existingLane.isValid(), "existing content lane created");

    const uint64_t trackId = makeTrack(*tm, "Take");
    if (trackId == 0) {
        std::cerr << "FAILED: track creation failed\n";
        return false;
    }
    tm->setTrackArmed(trackId, true);

    // Pattern-mode playback force-loops [0, patternLength] on the engine.
    tm->setPatternLoopOverride(0.0, 4.0, true);

    // Record press arms the transport (the capture engages once rolling).
    tm->record();

    // Cue at 3.0 s (beat 6.0) — beyond the pattern loop end (beat 4.0). The
    // pre-#845 bug pinned capture there, where the engine never plays.
    check(tm->beginCountIn(4, 3.0), "count-in began at 3.0s");
    check(tm->isCountInPending(), "count-in pending");
    tm->completeCountIn();
    check(tm->isPlaying(), "transport started after count-in");

    tm->setPosition(0.0);
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    check(tm->isRecording(), "capture session began in the clamped region");

    feedAt(*tm, 0.0, 0.5); // beats 0.0 → 1.0, inside the pattern loop
    feedAt(*tm, 0.5, 0.5); // beats 1.0 → 2.0

    tm->onTransportStateApplied(false, static_cast<uint64_t>(1.0 * kSampleRate), static_cast<double>(kSampleRate));
    check(!tm->isRecording(), "capture finalized on stop");

    const PlaylistLaneID takeLane = takeLaneOf(*tm, trackId);
    check(takeLane.isValid(), "recorded take landed on a lane");
    if (!takeLane.isValid()) {
        return false;
    }
    auto* lane = tm->getPlaylistModel().getLane(takeLane);
    const ClipInstance& clip = lane->clips.front();
    check(std::abs(clip.startBeat - 0.0) < 0.001, "take starts at the clamped loop start (beat 0.0)");
    check(std::abs(clip.durationBeats - 2.0) < 0.001, "take holds the captured 2.0 beats");

    std::cout << "PASSED\n";
    return true;
}

// Take-lane creation must not collide with existing content: the take gets its
// own lane, the pre-existing lane keeps its clip untouched.
bool testExistingLaneUntouchedByTake() {
    std::cout << "  [2/4] Existing content lane untouched; take gets a new lane... ";
    auto tm = makeRecorder();
    Aestra::Tests::ScopedTempDirectory dir{"RecSessionLane"};
    tm->setRecordingProjectPath((dir.path() / "lane.aes").string());

    PlaylistLaneID existingLane{};
    seedSessionContent(*tm, existingLane);

    const uint64_t trackId = makeTrack(*tm, "Take");
    if (trackId == 0) {
        std::cerr << "FAILED: track creation failed\n";
        return false;
    }
    // The track's own lane exists before the take; the take must add exactly
    // one more.
    const size_t lanesBefore = totalLaneCount(*tm);
    tm->setTrackArmed(trackId, true);
    tm->record();

    tm->setPosition(0.0);
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    feedAt(*tm, 0.0, 0.5);
    tm->onTransportStateApplied(false, static_cast<uint64_t>(0.5 * kSampleRate), static_cast<double>(kSampleRate));

    const PlaylistLaneID takeLane = takeLaneOf(*tm, trackId);
    check(takeLane.isValid(), "take landed on a lane");
    if (!takeLane.isValid()) {
        return false;
    }
    check(takeLane != existingLane, "take is NOT on the pre-existing lane");
    check(totalLaneCount(*tm) == lanesBefore + 1, "exactly one new lane was created");

    auto* existing = tm->getPlaylistModel().getLane(existingLane);
    check(existing != nullptr && existing->clips.size() == 1, "existing lane keeps its original clip");

    std::cout << "PASSED\n";
    return true;
}

// Undo must remove the take atomically (lane + clip + track attachment), redo
// must restore it — on top of pre-existing session content.
bool testTakeUndoRedo() {
    std::cout << "  [3/4] Undo removes the take atomically; redo restores it... ";
    auto tm = makeRecorder();
    Aestra::Tests::ScopedTempDirectory dir{"RecSessionUndo"};
    tm->setRecordingProjectPath((dir.path() / "undo.aes").string());

    PlaylistLaneID existingLane{};
    seedSessionContent(*tm, existingLane);

    const uint64_t trackId = makeTrack(*tm, "Take");
    if (trackId == 0) {
        std::cerr << "FAILED: track creation failed\n";
        return false;
    }
    const size_t lanesBefore = totalLaneCount(*tm);
    tm->setTrackArmed(trackId, true);
    tm->record();

    tm->setPosition(0.0);
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    feedAt(*tm, 0.0, 0.5);
    tm->onTransportStateApplied(false, static_cast<uint64_t>(0.5 * kSampleRate), static_cast<double>(kSampleRate));

    const PlaylistLaneID takeLane = takeLaneOf(*tm, trackId);
    check(takeLane.isValid(), "take committed");
    if (!takeLane.isValid()) {
        return false;
    }
    check(totalLaneCount(*tm) == lanesBefore + 1, "take lane present after commit");

    check(tm->getCommandHistory().undo(), "undo succeeded");
    check(totalLaneCount(*tm) == lanesBefore, "take lane removed by undo");
    check(!takeLaneOf(*tm, trackId).isValid(), "no take clip remains after undo");
    auto* existing = tm->getPlaylistModel().getLane(existingLane);
    check(existing != nullptr && existing->clips.size() == 1, "existing content survives undo");

    check(tm->getCommandHistory().redo(), "redo succeeded");
    check(totalLaneCount(*tm) == lanesBefore + 1, "take lane restored by redo");
    check(takeLaneOf(*tm, trackId).isValid(), "take clip restored by redo");

    std::cout << "PASSED\n";
    return true;
}

// Timeline loop region variant: with session content present and a timeline
// loop active, recording inside the region lands the take at the cue.
bool testRecordInsideTimelineLoopWithContent() {
    std::cout << "  [4/4] Take lands inside a timeline loop with session content... ";
    auto tm = makeRecorder();
    Aestra::Tests::ScopedTempDirectory dir{"RecSessionLoop"};
    tm->setRecordingProjectPath((dir.path() / "loop.aes").string());

    PlaylistLaneID existingLane{};
    seedSessionContent(*tm, existingLane);

    const uint64_t trackId = makeTrack(*tm, "Take");
    if (trackId == 0) {
        std::cerr << "FAILED: track creation failed\n";
        return false;
    }
    tm->setTrackArmed(trackId, true);
    tm->setTransportLoopRegion(0.0, 4.0, true); // beats 0..4
    tm->record();

    check(tm->beginCountIn(4, 1.0), "count-in began at 1.0s (beat 2.0, inside loop)");
    tm->completeCountIn();

    tm->setPosition(1.0);
    tm->onTransportStateApplied(true, static_cast<uint64_t>(1.0 * kSampleRate), static_cast<double>(kSampleRate));
    check(tm->isRecording(), "capture began at the cue");

    feedAt(*tm, 1.0, 0.5); // beats 2.0 → 3.0
    tm->onTransportStateApplied(false, static_cast<uint64_t>(1.5 * kSampleRate), static_cast<double>(kSampleRate));

    const PlaylistLaneID takeLane = takeLaneOf(*tm, trackId);
    check(takeLane.isValid(), "take landed on a lane");
    if (!takeLane.isValid()) {
        return false;
    }
    auto* lane = tm->getPlaylistModel().getLane(takeLane);
    const ClipInstance& clip = lane->clips.front();
    check(std::abs(clip.startBeat - 2.0) < 0.001, "take aligned to the post-count-in beat (2.0)");

    std::cout << "PASSED\n";
    return true;
}

} // namespace

int main() {
    std::cout << "RecordingSessionContentTest (#845)\n";
    struct NamedTest {
        const char* name;
        bool (*fn)();
    };
    const NamedTest tests[] = {
        {"pattern-mode session take", testRecordLandsTakeInPatternModeSession},
        {"existing lane untouched", testExistingLaneUntouchedByTake},
        {"take undo/redo", testTakeUndoRedo},
        {"timeline loop take", testRecordInsideTimelineLoopWithContent},
    };
    for (const auto& test : tests) {
        if (!test.fn()) {
            std::cerr << "TEST FAILED: " << test.name << "\n";
        }
    }
    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All recording-with-session-content tests passed\n";
    return 0;
}
