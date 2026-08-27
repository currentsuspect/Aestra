// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

// Recovery-load regression (FD-14 ownership layer): the project loader clears
// lanes/channels but previously left m_tracks populated, so every restored
// track collided with a surviving default track, restoreTrack rejected all of
// them, and the migration re-created each under a fresh id — doubling the
// table on every recovery load (50× "Failed to restore track", 2026-08-25).
// clearAllTracks() + loader ordering must make restore succeed cleanly.

#include "Models/TrackManager.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace Aestra;

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    Audio::TrackManager tm;

    // Default-project shape: N lanes, each owned by a Track (ids 1..N).
    constexpr int kDefaultTracks = 4;
    std::vector<uint64_t> defaultIds;
    for (int i = 0; i < kDefaultTracks; ++i) {
        const auto laneId = tm.getPlaylistModel().createLane("lane" + std::to_string(i));
        const auto trackId = tm.createTrack(laneId, "Track " + std::to_string(i + 1));
        expect(trackId != 0, "default track created");
        defaultIds.push_back(trackId);
    }
    expect(tm.getTracks().size() == static_cast<size_t>(kDefaultTracks),
           "default project seeded");

    // "Save": capture the file's track records the way the serializer would.
    std::vector<Audio::Track> savedTracks;
    for (const auto* track : tm.getTracks()) {
        if (track) {
            savedTracks.push_back(*track);
        }
    }
    expect(savedTracks.size() == static_cast<size_t>(kDefaultTracks),
           "file holds one record per track");

    // Lane records: the serializer persists lane UUIDs and the loader
    // re-creates them with the same ids BEFORE restoring tracks.
    struct SavedLane {
        Audio::PlaylistLaneID id;
        std::string name;
    };
    std::vector<SavedLane> savedLanes;
    for (int i = 0; i < kDefaultTracks; ++i) {
        savedLanes.push_back({savedTracks[static_cast<size_t>(i)].laneIds.front(),
                              "lane" + std::to_string(i)});
    }

    // Loader clear sequence (mirrors ProjectSerializer's clear block).
    tm.getPlaylistModel().clear();
    tm.clearAllChannels();
    tm.clearAllTracks();

    // Loader restore order: lanes first (stable UUIDs), then tracks.
    for (const auto& savedLane : savedLanes) {
        expect(tm.getPlaylistModel().createLaneWithId(savedLane.id, savedLane.name).isValid(),
               "lane re-created with stable id");
    }

    // Restore the file's tracks: every one must land — no duplicate rejections,
    // no migration re-creations, ownership rewired to the fresh lanes.
    for (const auto& restored : savedTracks) {
        expect(tm.restoreTrack(restored),
               "track " + std::to_string(restored.trackId) + " restores after clear");
    }
    expect(tm.getTracks().size() == static_cast<size_t>(kDefaultTracks),
           "track table holds exactly the file's tracks (no doubling)");

    for (const auto& restored : savedTracks) {
        for (const auto laneId : restored.laneIds) {
            const auto* lane = tm.getPlaylistModel().getLane(laneId);
            expect(lane != nullptr && lane->trackId == restored.trackId,
                   "lane ownership points at restored track " + std::to_string(restored.trackId));
        }
    }

    // Id counter must continue past restored ids, never reissue them.
    const auto laneId = tm.getPlaylistModel().createLane("post");
    const auto freshId = tm.createTrack(laneId, "post-load");
    expect(freshId != 0, "post-load track created");
    for (const auto& restored : savedTracks) {
        expect(freshId != restored.trackId, "fresh id never collides with restored ids");
    }

    if (g_failures == 0) {
        std::cout << "[PASS] TrackRestoreAfterClearTest\n";
        return 0;
    }
    std::cerr << "[FAIL] TrackRestoreAfterClearTest: " << g_failures << " failure(s)\n";
    return 1;
}
