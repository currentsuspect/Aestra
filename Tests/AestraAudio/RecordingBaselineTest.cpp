// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RecordingBaselineTest — Phase 2 baseline + Phase 3 acceptance of the
// Track/Lane/Channel migration (vault: "Track Lane Channel Ownership Contract").
//
// Phase 2 (pre-migration) captured CURRENT behavior as a regression baseline:
//
//   SECTION A — surviving invariants: behavior that MUST hold after the
//               migration (recorded audio routes to the armed channel; one
//               undoable transaction; dirty state; multi-arm independence;
//               one lane per take).
//
//   SECTION B — expected-to-change behavior, intentionally RED against the
//               future architecture (channel-based recording identity; lanes
//               without ownership; no Track entity).
//
// Phase 3 landed the migration. Section A is unchanged in its assertions and
// arms TRACKS (the arming surface moved, the invariants did not). Each track
// is created with one lane, so a recorded take is the track's NEXT lane
// (track-local numbering). Section B was replaced by the new acceptance:
// recording identity is track-based, lanes are owned and track-local
// numbered, and the Track entity carries the arm. Nothing in Section A may be
// weakened — if an assertion fails here, the migration broke an invariant.

#include "../../Source/Core/ProjectSerializer.h"
#include "AestraJSON.h"
#include "../Support/TestTempDirectory.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr double kBpm = 120.0;

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    } else {
        std::cout << "  PASS: " << label << "\n";
    }
}

std::shared_ptr<TrackManager> makeRecorder() {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);
    tm->setInputChannelCount(2);
    tm->setMaxRecordingSeconds(5.0);
    return tm;
}

void feedInput(TrackManager& tm, double seconds) {
    // processInput() expects interleaved frames * inputChannelCount samples.
    const uint32_t frames = static_cast<uint32_t>(seconds * kSampleRate);
    const uint32_t channels = std::max(1u, static_cast<uint32_t>(tm.getInputChannelCount()));
    std::vector<float> input(static_cast<size_t>(frames) * channels);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = 0.25f * std::sin(2.0 * 3.14159265 * 440.0 * static_cast<double>(i / channels) / kSampleRate);
    }
    constexpr uint32_t kBlock = 512;
    for (size_t off = 0; off < frames; off += kBlock) {
        const size_t n = std::min<size_t>(kBlock, frames - off);
        tm.processInput(input.data() + off * channels, static_cast<uint32_t>(n));
    }
}

/** Create a lane, a channel, a Track owning the lane and routing to the
 *  channel, and arm the Track (FD-14: the Track's arm is authoritative).
 *  Mirrors the app: a track is born with one lane. */
uint64_t armTrack(TrackManager& tm, const std::string& channelName, int inputIndex) {
    const PlaylistLaneID laneId = tm.getPlaylistModel().createLane(channelName);
    MixerChannel* ch = tm.addChannel(channelName);
    if (!ch) {
        return 0;
    }
    ch->setInputChannelIndex(inputIndex);
    const uint64_t trackId = tm.createTrack(laneId, channelName, ch->getChannelId());
    if (trackId != 0) {
        tm.setTrackArmed(trackId, true);
    }
    return trackId;
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

void recordTake(TrackManager& tm, double seconds) {
    tm.onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    feedInput(tm, seconds);
    tm.onTransportStateApplied(false, static_cast<uint64_t>(seconds * kSampleRate), static_cast<double>(kSampleRate));
}

size_t laneCount(TrackManager& tm) {
    return tm.getPlaylistModel().getLaneIDs().size();
}

// ===========================================================================
// SECTION A — surviving invariants (armed via Tracks; assertions unchanged)
// ===========================================================================

void testTakeRoutesToArmedTrackChannel() {
    std::cout << "[A] take routes to the armed track's channel\n";
    auto tm = makeRecorder();
    const uint64_t trackId = armTrack(*tm, "Guitar", 0);
    check(trackId != 0, "track created and armed");
    if (trackId == 0) {
        return;
    }
    auto* track = tm->getTrack(trackId);
    check(track != nullptr, "track resolvable by id");

    tm->record();
    recordTake(*tm, 1.0);

    const PlaylistLaneID takeLaneId = takeLaneOf(*tm, trackId);
    check(takeLaneId.isValid(), "one take creates one take lane");
    if (!takeLaneId.isValid()) {
        return;
    }
    auto* lane = tm->getPlaylistModel().getLane(takeLaneId);
    check(lane != nullptr, "take lane is on the playlist");
    check(lane && !lane->clips.empty(), "take clip is on the lane");
    if (!lane || lane->clips.empty()) {
        return;
    }
    auto& patternManager = tm->getPatternManager();
    auto* pattern = patternManager.getPattern(lane->clips[0].patternId);
    check(pattern != nullptr, "take clip references a pattern");
    if (pattern) {
        check(pattern->getMixerChannelId() == track->channelId,
              "take pattern routes to the armed track's channel (id " + std::to_string(track->channelId) + ")");
    }
}

void testRecordingIsOneUndoableTransaction() {
    std::cout << "[A] recording is one undoable transaction\n";
    auto tm = makeRecorder();
    const uint64_t trackId = armTrack(*tm, "Ch 1", 0);
    check(trackId != 0, "track created and armed");
    if (trackId == 0) {
        return;
    }

    tm->record();
    recordTake(*tm, 1.0);
    check(tm->getCommandHistory().canUndo(), "take committed before undo");
    check(laneCount(*tm) == 2, "setup lane plus one take lane");
    check(tm->getCommandHistory().undo(), "undo of the take succeeds");
    check(laneCount(*tm) == 1, "undo removes the take's lane");
    check(!takeLaneOf(*tm, trackId).isValid(), "undo leaves no take lane on the track");
    check(tm->getCommandHistory().redo(), "redo restores the take");
    check(laneCount(*tm) == 2, "redo restores the take's lane");
    check(takeLaneOf(*tm, trackId).isValid(), "redo restores the take lane on the track");
}

void testDirtyStateUpdated() {
    std::cout << "[A] dirty state updated by recording\n";
    auto tm = makeRecorder();
    const uint64_t trackId = armTrack(*tm, "Ch 1", 0);
    check(trackId != 0, "track created and armed");
    if (trackId == 0) {
        return;
    }

    tm->record();
    recordTake(*tm, 1.0);
    check(tm->isModified(), "project is dirty after a recorded take");
}

void testMultipleArmedTracksCaptureIndependently() {
    std::cout << "[A] multiple armed tracks capture independently\n";
    auto tm = makeRecorder();
    const uint64_t track1 = armTrack(*tm, "Ch 1", 0);
    const uint64_t track2 = armTrack(*tm, "Ch 2", 1);
    check(track1 != 0 && track2 != 0, "two tracks created and armed");

    tm->record();
    recordTake(*tm, 1.0);

    auto* t1 = tm->getTrack(track1);
    auto* t2 = tm->getTrack(track2);
    check(t1 && t2, "both tracks resolvable");
    check(t1 && t1->laneIds.size() == 2, "track 1 owns its setup lane plus one take lane");
    check(t2 && t2->laneIds.size() == 2, "track 2 owns its setup lane plus one take lane");
    if (t1 && t2 && t1->laneIds.size() == 2 && t2->laneIds.size() == 2) {
        const PlaylistLaneID take1 = takeLaneOf(*tm, track1);
        const PlaylistLaneID take2 = takeLaneOf(*tm, track2);
        check(take1.isValid() && take2.isValid(), "each armed track captured a take");
        check(take1 != take2, "each take lane is distinct");
        check(t1->laneIds[1] == take1 && t2->laneIds[1] == take2, "each take landed on its own track's lane");
    }
}

void testTwoArmedTracksSharingChannelCaptureIndependently() {
    std::cout << "[A] two armed tracks sharing one channel capture independently\n";
    auto tm = makeRecorder();
    const PlaylistLaneID lane1 = tm->getPlaylistModel().createLane("Shared A");
    const PlaylistLaneID lane2 = tm->getPlaylistModel().createLane("Shared B");
    MixerChannel* ch = tm->addChannel("Shared");
    if (!ch) {
        check(false, "shared channel created");
        return;
    }
    ch->setInputChannelIndex(0);
    const uint64_t track1 = tm->createTrack(lane1, "Shared A", ch->getChannelId());
    const uint64_t track2 = tm->createTrack(lane2, "Shared B", ch->getChannelId());
    tm->setTrackArmed(track1, true);
    tm->setTrackArmed(track2, true);

    tm->record();
    recordTake(*tm, 1.0);

    auto* t1 = tm->getTrack(track1);
    auto* t2 = tm->getTrack(track2);
    check(t1 && t2, "both tracks resolvable");
    check(t1 && t1->laneIds.size() == 2, "track 1 owns setup lane plus its take");
    check(t2 && t2->laneIds.size() == 2, "track 2 owns setup lane plus its take");
    if (t1 && t2 && t1->laneIds.size() == 2 && t2->laneIds.size() == 2) {
        const PlaylistLaneID take1 = takeLaneOf(*tm, track1);
        const PlaylistLaneID take2 = takeLaneOf(*tm, track2);
        check(take1.isValid() && take2.isValid(), "both tracks got their own take despite the shared channel");
        check(take1 != take2, "takes are on distinct lanes");
    }
}

void testEachTakeCreatesALane() {
    std::cout << "[A] each take creates a track-local lane\n";
    auto tm = makeRecorder();
    const uint64_t trackId = armTrack(*tm, "Ch 1", 0);
    check(trackId != 0, "track created and armed");
    if (trackId == 0) {
        return;
    }

    // Arm ONCE: the app keeps record-armed across takes; each play/stop
    // pass captures and commits its own take.
    tm->record();
    for (int pass = 1; pass <= 2; ++pass) {
        recordTake(*tm, 1.0);
    }
    auto* track = tm->getTrack(trackId);
    check(track != nullptr, "track still resolvable");
    if (track) {
        check(track->laneIds.size() == 3, "two recording passes create two take lanes");
        const PlaylistLaneID take1 = takeLaneOf(*tm, trackId);
        check(take1.isValid(), "first take lane exists");
        if (take1.isValid()) {
            check(track->laneNumber(take1) == 2, "first take lane is track-local lane 2");
            check(track->laneIds.size() == 3, "take lanes accumulate track-locally");
        }
    }
}

// ===========================================================================
// SECTION B — post-migration acceptance (the Phase 2 RED assertions now hold)
// ===========================================================================

void testRecordingIdentityIsTrackBased() {
    std::cout << "[B] recording identity is track-based\n";
    auto tm = makeRecorder();
    const uint64_t trackId = armTrack(*tm, "Guitar", 0);
    check(trackId != 0, "track created and armed");
    if (trackId == 0) {
        return;
    }

    tm->record();
    recordTake(*tm, 1.0);

    const PlaylistLaneID takeLaneId = takeLaneOf(*tm, trackId);
    check(takeLaneId.isValid(), "take landed on a lane");
    if (!takeLaneId.isValid()) {
        return;
    }
    auto* lane = tm->getPlaylistModel().getLane(takeLaneId);
    check(lane != nullptr, "lane exists");
    if (!lane) {
        return;
    }
    check(lane->trackId == trackId, "lane is owned by the recording track");
    auto* track = tm->getTrack(trackId);
    check(track != nullptr, "recording track resolvable");
    if (track) {
        check(track->laneNumber(lane->id) == 2, "take lane is track-local lane 2 (after the born-with lane)");
    }
    check(tm->getTrackForLane(lane->id) == track, "lane resolves to its track");
}

void testLanesOwnedAndGrouped() {
    std::cout << "[B] lanes are owned and grouped by track\n";
    auto tm = makeRecorder();
    const uint64_t trackId = armTrack(*tm, "Ch 1", 0);
    check(trackId != 0, "track created and armed");
    if (trackId == 0) {
        return;
    }

    tm->record();
    for (int pass = 1; pass <= 2; ++pass) {
        recordTake(*tm, 1.0);
    }

    auto* track = tm->getTrack(trackId);
    check(track != nullptr, "track resolvable");
    if (!track) {
        return;
    }
    check(track->laneIds.size() == 3, "all three lanes group under the same track");
    const auto laneIds = tm->getPlaylistModel().getLaneIDs();
    check(laneIds.size() == 3, "playlist holds the same three lanes");
    for (const auto& laneId : laneIds) {
        auto* lane = tm->getPlaylistModel().getLane(laneId);
        check(lane && lane->trackId == trackId, "every lane carries the track's stable id");
    }
    check(track->laneNumber(track->laneIds[0]) == 1 && track->laneNumber(track->laneIds[1]) == 2 &&
              track->laneNumber(track->laneIds[2]) == 3,
          "lanes have consecutive track-local numbers");
}

void testTrackEntityHoldsArm() {
    std::cout << "[B] the Track entity holds the recording arm\n";
    auto tm = makeRecorder();
    check(tm->getTrackArmedCount() == 0, "no armed tracks initially");
    check(tm->getTracks().empty(), "no tracks exist before arming");

    const uint64_t trackId = armTrack(*tm, "Ch 1", 0);
    check(trackId != 0, "track created");
    if (trackId == 0) {
        return;
    }
    check(tm->getTrackArmedCount() == 1, "arm is model state on the track");
    auto* track = tm->getTrack(trackId);
    check(track != nullptr && track->armed, "track carries the armed flag");
    check(tm->getTracks().size() == 1, "track listed by the manager");

    // Channel arming alone no longer starts a capture: the track is the only
    // recording control. With the track disarmed, no take may be committed.
    tm->setTrackArmed(trackId, false);
    if (auto* ch = tm->getChannelById(track->channelId)) {
        ch->setArmed(true);
    }
    tm->record();
    recordTake(*tm, 1.0);
    check(laneCount(*tm) == 1, "channel arm alone produces no take");
    check(!takeLaneOf(*tm, trackId).isValid(), "disarmed track captures nothing");
}

void testTakeLaneOwnershipRoundTrips() {
    std::cout << "[B] take lane ownership survives project round-trip\n";
    auto tm = makeRecorder();
    const uint64_t trackId = armTrack(*tm, "Guitar", 0);
    check(trackId != 0, "track created and armed");
    if (trackId == 0) {
        return;
    }

    tm->record();
    recordTake(*tm, 1.0);
    auto* track = tm->getTrack(trackId);
    check(track != nullptr, "track resolvable before save");
    if (!track) {
        return;
    }
    check(track->laneIds.size() == 2, "setup lane plus one take lane before save");
    const PlaylistLaneID takeLaneId = takeLaneOf(*tm, trackId);
    check(takeLaneId.isValid(), "take lane exists before save");
    if (!takeLaneId.isValid()) {
        return;
    }
    check(track->laneNumber(takeLaneId) == 2, "take lane is track-local lane 2 before save");

    Aestra::Tests::ScopedTempDirectory dir{"RecordingBaselineRoundTrip"};
    const std::string path = (dir.path() / "roundtrip.aes").string();
    tm->setRecordingProjectPath(path);
    const bool saved = ProjectSerializer::save(path, tm, kBpm, 0.0);
    check(saved, "project saved");
    if (!saved) {
        return;
    }

    auto tm2 = makeRecorder();
    const auto result = ProjectSerializer::load(path, tm2);
    check(result.ok, "project loaded");
    if (!result.ok) {
        return;
    }

    auto* loadedTrack = tm2->getTrack(trackId);
    check(loadedTrack != nullptr, "loaded track keeps the same track id");
    if (!loadedTrack) {
        return;
    }
    check(loadedTrack->laneIds.size() == 2, "loaded track owns setup lane plus take lane");
    const PlaylistLaneID loadedTakeLaneId = takeLaneOf(*tm2, trackId);
    check(loadedTakeLaneId.isValid(), "take lane resolves after load");
    if (loadedTakeLaneId.isValid()) {
        check(loadedTrack->laneNumber(loadedTakeLaneId) == 2, "take lane keeps track-local lane 2 after load");
        check(tm2->getTrackForLane(loadedTakeLaneId) == loadedTrack, "loaded lane resolves to its track");
    }
    const auto loadedLaneIds = tm2->getPlaylistModel().getLaneIDs();
    check(loadedLaneIds.size() == 2, "playlist holds the same two lanes after load");
}

void testLaneChannelStateFollowsTrackIdentity() {
    std::cout << "[B] lane channel state resolves through track identity, not position\n";
    auto tm = makeRecorder();
    // Lane creation order differs from channel creation order: track B is
    // created first but lives on the playlist's second lane. A positional
    // lane->channel pairing would write B's state into lane A's JSON.
    const PlaylistLaneID laneA = tm->getPlaylistModel().createLane("A");
    const PlaylistLaneID laneB = tm->getPlaylistModel().createLane("B");
    MixerChannel* chB = tm->addChannel("ChB");
    MixerChannel* chA = tm->addChannel("ChA");
    check(chB != nullptr && chA != nullptr, "channels created");
    if (!chB || !chA) {
        return;
    }
    chB->setInputChannelIndex(1);
    chA->setInputChannelIndex(0);
    const uint64_t trackB = tm->createTrack(laneB, "Track B", chB->getChannelId());
    const uint64_t trackA = tm->createTrack(laneA, "Track A", chA->getChannelId());
    check(trackB != 0 && trackA != 0, "tracks created");
    if (trackB == 0 || trackA == 0) {
        return;
    }
    chA->setVolume(0.25f);
    chB->setVolume(0.75f);
    chA->setArmed(true);
    chB->setArmed(false);

    Aestra::Tests::ScopedTempDirectory dir{"RecordingBaselineRoundTrip"};
    const std::string path = (dir.path() / "identity.aes").string();
    tm->setRecordingProjectPath(path);
    const auto ser = ProjectSerializer::serialize(tm, kBpm, 0.0, 2);
    check(ser.ok && !ser.contents.empty(), "project serialized");
    if (!ser.ok) {
        return;
    }

    // Wire pin: each lane JSON carries ITS OWN channel's id and state.
    const Aestra::JSON root = Aestra::JSON::parse(ser.contents);
    check(root.has("lanes") && root["lanes"].isArray() && root["lanes"].size() == 2, "lanes array on the wire");
    if (root.has("lanes") && root["lanes"].isArray()) {
        const Aestra::JSON& lanesJson = root["lanes"];
        for (size_t i = 0; i < lanesJson.size(); ++i) {
            if (!lanesJson[i].has("name") || !lanesJson[i].has("mixerChannelId") || !lanesJson[i].has("armed")) {
                continue;
            }
            const std::string laneName = lanesJson[i]["name"].asString();
            if (laneName == "A") {
                check(lanesJson[i]["mixerChannelId"].asNumber() == static_cast<double>(chA->getChannelId()),
                      "lane A carries channel A's id on the wire");
                check(lanesJson[i]["armed"].asBool(), "lane A carries channel A's armed state");
            } else if (laneName == "B") {
                check(lanesJson[i]["mixerChannelId"].asNumber() == static_cast<double>(chB->getChannelId()),
                      "lane B carries channel B's id on the wire");
                check(!lanesJson[i]["armed"].asBool(), "lane B carries channel B's armed state");
            }
        }
    }

    // Round-trip: after load, each lane's channel is its OWN channel.
    check(ProjectSerializer::writeAtomically(path, ser.contents), "project written");
    auto tm2 = makeRecorder();
    const auto result = ProjectSerializer::load(path, tm2);
    check(result.ok, "project loaded");
    if (!result.ok) {
        return;
    }
    auto* loadedLaneA = tm2->getPlaylistModel().getLane(laneA);
    auto* loadedTrackA = tm2->getTrackForLane(laneA);
    check(loadedLaneA != nullptr && loadedTrackA != nullptr, "lane A and its track loaded");
    if (loadedTrackA) {
        auto* loadedChA = tm2->getChannelById(static_cast<uint32_t>(loadedTrackA->channelId));
        check(loadedChA != nullptr && loadedChA->getChannelId() == chA->getChannelId(),
              "lane A resolves to channel A after load");
        check(loadedChA != nullptr && loadedChA->getVolume() == 0.25f, "lane A keeps channel A's volume");
        check(loadedChA != nullptr && loadedChA->isArmed(), "lane A keeps channel A's armed state");
    }
}

} // namespace

int main() {
    std::cout << "=== Recording Baseline (Phase 2 + Phase 3 acceptance) ===\n";
    std::cout << "Section A: surviving invariants\n";
    testTakeRoutesToArmedTrackChannel();
    testRecordingIsOneUndoableTransaction();
    testDirtyStateUpdated();
    testMultipleArmedTracksCaptureIndependently();
    testTwoArmedTracksSharingChannelCaptureIndependently();
    testEachTakeCreatesALane();

    std::cout << "Section B: post-migration acceptance\n";
    testRecordingIdentityIsTrackBased();
    testLanesOwnedAndGrouped();
    testTrackEntityHoldsArm();
    testTakeLaneOwnershipRoundTrips();
    testLaneChannelStateFollowsTrackIdentity();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "Track/Lane/Channel migration: all green.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}