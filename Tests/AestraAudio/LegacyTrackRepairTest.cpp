// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// LegacyTrackRepairTest — #866. Projects saved by pre-#864 builds carry doubled track records in two shapes, both
// found in the owner's own files:
//   A ("1st Serious Piece"): every lane owner routes to the master, and a same-named record with no lanes holds the
//     real mixer channel. Restoring both lost every track's routing.
//   B ("Veralise"): every lane owner routes correctly, and a second record per track points only at lanes that no
//     longer exist.
// The repair merges A's pairs (the owner takes the channel) and drops B's dead records; anything else is left alone.

#include "Models/LegacyTrackRepair.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << what << "\n";
    if (!cond)
        ++g_failures;
}

PlaylistLaneID laneId(uint64_t n) {
    PlaylistLaneID id;
    id.high = 0xABCDEF;
    id.low = n;
    return id;
}

Track track(uint64_t id, const std::string& name, uint32_t channel, std::vector<PlaylistLaneID> lanes) {
    Track t;
    t.trackId = id;
    t.name = name;
    t.channelId = channel;
    t.laneIds = std::move(lanes);
    if (!t.laneIds.empty())
        t.activeLaneId = t.laneIds.front();
    return t;
}

const Track* byId(const std::vector<Track>& v, uint64_t id) {
    for (const auto& t : v)
        if (t.trackId == id)
            return &t;
    return nullptr;
}
} // namespace

int main() {
    std::set<uint64_t> live = {1, 2, 3};
    const auto exists = [&](const PlaylistLaneID& id) { return live.count(id.low) != 0; };

    std::cout << "[A] owners on the master, channels on lane-less twins\n";
    std::vector<Track> a = {
        track(1, "Track 1", 1, {}), track(2, "Track 2", 2, {}), track(3, "Track 3", 3, {}),
        track(51, "Track 1", MASTER_MIXER_CHANNEL_ID, {laneId(1)}),
        track(52, "Track 2", MASTER_MIXER_CHANNEL_ID, {laneId(2)}),
        track(53, "Track 3", MASTER_MIXER_CHANNEL_ID, {laneId(3)}),
    };
    auto r = repairLegacyTrackRecords(a, exists);
    check(a.size() == 3, "three tracks remain, one per lane (" + std::to_string(a.size()) + ")");
    check(byId(a, 51) && byId(a, 51)->channelId == 1 && byId(a, 53) && byId(a, 53)->channelId == 3,
          "each lane owner takes its twin's channel");
    check(!byId(a, 1) && !byId(a, 2), "the lane-less twins are gone");
    check(r.channelsMerged == 3 && r.deadDropped == 0, "the report counts three merges");

    std::cout << "[B] a second record per track pointing only at lanes that no longer exist\n";
    std::vector<Track> b = {
        track(1, "Track 1", 1, {laneId(101)}), track(2, "Track 2", 2, {laneId(102)}),
        track(51, "Track 1", 1, {laneId(1)}), track(52, "Track 2", 2, {laneId(2)}),
    };
    r = repairLegacyTrackRecords(b, exists);
    check(b.size() == 2 && byId(b, 51) && byId(b, 52), "the dead-lane records are dropped, the live ones kept");
    check(r.deadDropped == 2 && r.channelsMerged == 0, "the report counts two dropped");

    std::cout << "[C] a healthy project is untouched\n";
    std::vector<Track> c = {track(1, "Drums", 4, {laneId(1)}), track(2, "Bass", MASTER_MIXER_CHANNEL_ID, {laneId(2)})};
    r = repairLegacyTrackRecords(c, exists);
    check(c.size() == 2 && byId(c, 2)->channelId == MASTER_MIXER_CHANNEL_ID && r.channelsMerged + r.deadDropped == 0,
          "nothing changes, and a track the user routed to the master stays on the master");
    check(byId(c, 1)->name == "Drums" && byId(c, 1)->laneIds.size() == 1 && byId(c, 2)->name == "Bass",
          "kept records keep their names and lanes");
    check(byId(a, 52) && byId(a, 52)->name == "Track 2" && byId(a, 52)->laneIds.size() == 1,
          "a merged owner keeps its name and lane");

    std::cout << "[D] ambiguous names are left alone\n";
    std::vector<Track> d = {
        track(1, "Vox", 7, {}), track(2, "Vox", 8, {}), // two twins with one name: which channel?
        track(51, "Vox", MASTER_MIXER_CHANNEL_ID, {laneId(1)}),
    };
    r = repairLegacyTrackRecords(d, exists);
    check(d.size() == 3 && byId(d, 51)->channelId == MASTER_MIXER_CHANNEL_ID && r.channelsMerged == 0,
          "with two candidate channels nothing is guessed");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
