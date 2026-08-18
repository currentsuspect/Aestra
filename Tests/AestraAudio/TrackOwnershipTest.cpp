// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// TrackOwnershipTest — FD-14 PR-1: the Track model, lane ownership, and the
// load-time migration. Ownership is by STABLE ID only; the positional
// pairings (lane index → channel index, etc.) are structurally absent.
//
//   Track ──── owns ────> Lane(s)
//   Track ──── routes ──> Channel
//   Lane  ──── belongs ──> Track

#include "Models/TrackManager.h"
#include "Core/MixerChannel.h"
#include "../../Source/Core/ProjectSerializer.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    } else {
        std::cout << "  PASS: " << label << "\n";
    }
}

void testCreateTrackOwnsLane() {
    std::cout << "[model] createTrack owns the lane\n";
    TrackManager tm;
    PlaylistLaneID laneId = tm.getPlaylistModel().createLane("Track 1");
    assert(laneId.isValid());

    uint64_t trackId = tm.createTrack(laneId, "Track 1");
    check(trackId != 0, "track created");
    Track* track = tm.getTrack(trackId);
    check(track != nullptr, "track resolvable by id");
    if (!track) {
        return;
    }
    check(track->laneIds.size() == 1 && track->laneIds[0] == laneId, "track owns the lane");
    check(track->activeLaneId == laneId, "first lane is the active lane");
    auto* lane = tm.getPlaylistModel().getLane(laneId);
    check(lane && lane->trackId == trackId, "lane's trackId points at the track");
    check(tm.getTrackForLane(laneId) == track, "lane resolves to its track");
    check(track->laneNumber(laneId) == 1, "track-local lane number is 1");
    check(tm.getTrackForLane(PlaylistLaneID::generate()) == nullptr, "unknown lane resolves to null");
}

void testAttachLaneTrackLocalNumbering() {
    std::cout << "[model] attachLaneToTrack + track-local numbering\n";
    TrackManager tm;
    PlaylistLaneID lane1 = tm.getPlaylistModel().createLane("T1 Lane 1");
    PlaylistLaneID lane2 = tm.getPlaylistModel().createLane("T1 Lane 2");
    uint64_t trackId = tm.createTrack(lane1, "Track 1");
    check(tm.attachLaneToTrack(trackId, lane2), "second lane attached");
    Track* track = tm.getTrack(trackId);
    check(track && track->laneIds.size() == 2, "track owns both lanes");
    check(track && track->laneNumber(lane1) == 1, "lane 1 keeps number 1");
    check(track && track->laneNumber(lane2) == 2, "lane 2 gets number 2");
    check(track && track->activeLaneId == lane1, "active lane unchanged by attach");

    PlaylistLaneID other = tm.getPlaylistModel().createLane("Other");
    uint64_t track2 = tm.createTrack(other, "Track 2");
    check(tm.getTrack(track2)->laneNumber(other) == 1, "track 2 numbering starts at 1 independently");
}

void testTrackArmState() {
    std::cout << "[model] track arm state (FD-14 #6)\n";
    TrackManager tm;
    PlaylistLaneID lane = tm.getPlaylistModel().createLane("Armed");
    uint64_t trackId = tm.createTrack(lane, "Armed");
    check(tm.getTrackArmedCount() == 0, "no armed tracks initially");
    tm.setTrackArmed(trackId, true);
    check(tm.getTrackArmedCount() == 1, "track arm counted");
    check(tm.getTrack(trackId)->armed, "track armed flag set");
    tm.setTrackArmed(trackId, false);
    check(tm.getTrackArmedCount() == 0, "disarm clears");
}

void testRestoreTrackExactIds() {
    std::cout << "[model] restoreTrack keeps stable ids\n";
    TrackManager tm;
    PlaylistLaneID lane1 = tm.getPlaylistModel().createLane("L1");
    PlaylistLaneID lane2 = tm.getPlaylistModel().createLane("L2");
    Track restored;
    restored.trackId = 42;
    restored.name = "Restored";
    restored.channelId = 7;
    restored.armed = true;
    restored.laneIds = {lane1, lane2};
    restored.activeLaneId = lane2;
    check(tm.restoreTrack(restored), "track restored");
    Track* track = tm.getTrack(42);
    check(track != nullptr, "restored track resolvable by exact id");
    check(track && track->laneIds.size() == 2 && track->laneIds[0] == lane1, "restored ownership");
    check(track && track->armed && track->channelId == 7, "restored state");
    check(tm.getPlaylistModel().getLane(lane1)->trackId == 42, "lane re-attached to restored track");
    check(track && track->activeLaneId == lane2, "active lane restored");
}

void testSerializeRoundtrip() {
    std::cout << "[serializer] tracks roundtrip\n";
    const auto dir = std::filesystem::temp_directory_path() / "aestra_track_roundtrip";
    std::filesystem::create_directories(dir);
    const auto path = dir / "tracks.aes";

    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(48000.0);
    PlaylistLaneID lane1 = tm->getPlaylistModel().createLane("T1 Lane 1");
    PlaylistLaneID lane2 = tm->getPlaylistModel().createLane("T1 Lane 2");
    uint64_t t1 = tm->createTrack(lane1, "Track 1", 3);
    tm->attachLaneToTrack(t1, lane2);
    PlaylistLaneID lane3 = tm->getPlaylistModel().createLane("T2 Lane 1");
    uint64_t t2 = tm->createTrack(lane3, "Track 2");
    tm->setTrackArmed(t2, true);

    auto saved = ProjectSerializer::serialize(tm, 120.0, 0.0, 0);
    check(!saved.contents.empty(), "serialize produced output");
    std::ofstream out(path);
    out << saved.contents;
    out.close();

    auto loaded = std::make_shared<TrackManager>();
    loaded->setOutputSampleRate(48000.0);
    auto result = ProjectSerializer::load(path.string(), loaded);
    check(result.ok, "load succeeded");

    Track* l1 = loaded->getTrack(t1);
    check(l1 != nullptr, "track 1 restored by exact id");
    check(l1 && l1->name == "Track 1" && l1->channelId == 3, "track 1 state restored");
    check(l1 && l1->laneIds.size() == 2, "track 1 owns both lanes");
    check(l1 && l1->laneNumber(loaded->getPlaylistModel().getLaneIDs()[0]) == 1, "lane 1 numbering restored");
    check(l1 && l1->laneNumber(loaded->getPlaylistModel().getLaneIDs()[1]) == 2, "lane 2 numbering restored");
    Track* l2 = loaded->getTrack(t2);
    check(l2 != nullptr && l2->armed, "track 2 restored with arm state");
    check(l2 && l2->laneNumber(loaded->getPlaylistModel().getLaneIDs()[2]) == 1, "track 2 numbering independent");

    std::filesystem::remove_all(dir);
}

void testLegacyFileMigration() {
    std::cout << "[serializer] legacy file migration (one track per lane)\n";
    const auto dir = std::filesystem::temp_directory_path() / "aestra_track_migration";
    std::filesystem::create_directories(dir);
    const auto path = dir / "legacy.aes";

    // Old-format file: lanes with their own stored mixerChannelId, NO tracks
    // section. The migration must create one Track per lane deterministically
    // using each lane's OWN stored channel id — never positional.
    std::string projectJson = R"({
        "version": 1,
        "tempo": 120.0,
        "playhead": 0.0,
        "sources": [],
        "patterns": [],
        "lanes": [
            {
                "name": "Lane A",
                "color": "4294967295",
                "volume": 1.0,
                "pan": 0.0,
                "mixerChannelId": 5,
                "clips": [],
                "automation": []
            },
            {
                "name": "Lane B",
                "color": "4294967295",
                "volume": 1.0,
                "pan": 0.0,
                "mixerChannelId": 9,
                "clips": [],
                "automation": []
            }
        ],
        "arsenal": {"nextId": 1, "units": []}
    })";
    std::ofstream out(path);
    out << projectJson;
    out.close();

    auto loaded = std::make_shared<TrackManager>();
    loaded->setOutputSampleRate(48000.0);
    auto result = ProjectSerializer::load(path.string(), loaded);
    check(result.ok, "legacy load succeeded");

    const auto laneIds = loaded->getPlaylistModel().getLaneIDs();
    check(laneIds.size() == 2, "both lanes loaded");
    auto tracks = loaded->getTracks();
    check(tracks.size() == 2, "one track per legacy lane");

    Track* trackA = loaded->getTrackForLane(laneIds[0]);
    Track* trackB = loaded->getTrackForLane(laneIds[1]);
    check(trackA != nullptr && trackB != nullptr, "every lane resolves to a track");
    check(trackA != trackB, "lanes own separate tracks");
    check(trackA && trackA->channelId == 5, "track A routes to lane A's OWN stored channel (5)");
    check(trackB && trackB->channelId == 9, "track B routes to lane B's OWN stored channel (9)");
    check(trackA && trackA->laneNumber(laneIds[0]) == 1, "track A numbering starts at 1");
    check(trackB && trackB->laneNumber(laneIds[1]) == 1, "track B numbering starts at 1");

    std::filesystem::remove_all(dir);
}

void testResolveLaneChannelId() {
    std::cout << "[model] lane channel resolves through track identity, never position\n";
    TrackManager tm;
    PlaylistLaneID laneId = tm.getPlaylistModel().createLane("Lane 1");

    check(tm.resolveLaneChannelId(laneId) == 0, "unowned lane resolves to 0 (caller falls back to master)");

    MixerChannel* channel = tm.addChannelWithId("Insert 7", 7);
    check(channel != nullptr && channel->getChannelId() == 7, "test channel 7 exists");
    uint64_t trackId = tm.createTrack(laneId, "Track 1", 7);
    check(trackId != 0, "track created routing to channel 7");
    check(tm.resolveLaneChannelId(laneId) == 7, "owned lane resolves through track channelId — not position");

    // Track exists but its channelId does not resolve to a MixerChannel:
    // the resolver must return 0, never hand a dead id to the caller as if
    // it were live (branch shared with the serializer's channel state save).
    PlaylistLaneID deadLaneId = tm.getPlaylistModel().createLane("Lane 2");
    uint64_t deadTrackId = tm.createTrack(deadLaneId, "Track 2", 99);
    check(deadTrackId != 0, "track with nonexistent channel created");
    check(tm.resolveLaneChannelId(deadLaneId) == 0, "track with dead channel resolves to 0");

    tm.removeTrack(trackId);
    check(tm.resolveLaneChannelId(laneId) == 0, "lane outliving its removed track resolves to 0");

    check(tm.resolveLaneChannelId(PlaylistLaneID::generate()) == 0, "missing lane resolves to 0");
}

} // namespace

int main() {
    std::cout << "=== Track Ownership (FD-14 PR-1) ===\n";
    testCreateTrackOwnsLane();
    testAttachLaneTrackLocalNumbering();
    testTrackArmState();
    testRestoreTrackExactIds();
    testSerializeRoundtrip();
    testLegacyFileMigration();
    testResolveLaneChannelId();

    if (g_failures == 0) {
        std::cout << "Track ownership: all green.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
