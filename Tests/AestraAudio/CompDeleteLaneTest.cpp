// © 2026 Aestra Studios — All Rights Reserved.
//
// CompDeleteLaneTest — deleting a take or the master keeps the comp true (SPEC-022, FD-70/FD-71).
//
// A deleted take the comp used leaves a GAP where it played (silence, never a guess). Deleting the master ends the comp
// and the takes play again. Undo gives back the lane, the comp, the master's pieces (same ids) and the mute flags.

#include "Commands/CreateLaneCommand.h"
#include "Commands/DeleteLaneCommand.h"
#include "Commands/SetCompRangeCommand.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++g_failures;
}

struct Rig {
    std::unique_ptr<TrackManager> owner = std::make_unique<TrackManager>();
    TrackManager& tm = *owner;
    PlaylistLaneID take1, take2, take3;
    uint64_t trackId = 0;

    Rig() {
        auto& pl = tm.getPlaylistModel();
        PlaylistLaneID* ids[3] = {&take1, &take2, &take3};
        for (int i = 0; i < 3; ++i) {
            auto c = std::make_shared<CreateLaneCommand>(pl, "Take " + std::to_string(i + 1));
            c->execute();
            *ids[i] = c->getLaneId();
            if (i == 0)
                trackId = tm.createTrack(*ids[0], "Vox");
            else
                tm.attachLaneToTrack(trackId, *ids[i]);
            ClipInstance clip;
            clip.id = ClipInstanceID::generate();
            clip.durationBeats = 16;
            clip.sourceId = static_cast<uint64_t>(i + 1);
            pl.addClip(*ids[i], clip);
        }
        SetCompRangeCommand(tm, trackId, 0, 16, take1).execute();
        SetCompRangeCommand(tm, trackId, 4, 8, take2).execute(); // comp: take1 | take2 | take1
    }
    const PlaylistLane* lane(PlaylistLaneID id) { return tm.getPlaylistModel().getLane(id); }
    const Track& track() { return *tm.getTrack(trackId); }
};

void testDeletingAUsedTakeLeavesAGap() {
    Rig r;
    const PlaylistLaneID master = r.track().compLaneId;
    std::vector<ClipInstanceID> before;
    for (const ClipInstance& c : r.lane(master)->clips)
        before.push_back(c.id);
    DeleteLaneCommand del(r.tm, r.take2);
    del.execute();
    check(!r.lane(r.take2), "TheTakeIsGone");
    check(!r.track().comp.laneAt(5).isValid() && r.track().comp.laneAt(1) == r.take1 &&
              r.track().comp.laneAt(10) == r.take1,
          "ItsSpanBecomesAGapNotAGuess");
    check(r.lane(master)->clips.size() == 2, "TheMasterNoLongerPlaysIt");
    check(r.lane(r.take1)->muted, "TheOtherTakesStaySilentUnderTheComp");

    del.undo();
    check(r.lane(r.take2) && r.track().comp.laneAt(5) == r.take2, "UndoBringsTheTakeAndItsPieceBack");
    bool sameIds = r.lane(master)->clips.size() == before.size();
    for (size_t i = 0; sameIds && i < before.size(); ++i)
        sameIds = r.lane(master)->clips[i].id == before[i];
    check(sameIds, "UndoKeepsTheMastersPieceIdentity");
    check(r.lane(r.take2)->muted && r.lane(r.take1)->muted, "UndoRestoresTheMuteFlags");

    del.redo();
    check(!r.track().comp.laneAt(5).isValid() && r.lane(master)->clips.size() == 2, "RedoLeavesTheGapAgain");
}

void testDeletingAnUnusedTakeChangesNothing() {
    Rig r;
    const PlaylistLaneID master = r.track().compLaneId;
    const size_t pieces = r.lane(master)->clips.size();
    DeleteLaneCommand del(r.tm, r.take3);
    del.execute();
    check(!r.lane(r.take3) && r.lane(master)->clips.size() == pieces && r.track().comp.ranges().size() == 3,
          "AnUnusedTakeGoesAndTheCompIsUntouched");
    del.undo();
    check(r.lane(r.take3) && r.lane(master)->clips.size() == pieces, "UndoRestoresIt");
}

void testDeletingTheMasterEndsTheComp() {
    Rig r;
    const PlaylistLaneID master = r.track().compLaneId;
    DeleteLaneCommand del(r.tm, master);
    del.execute();
    check(!r.lane(master) && !r.track().compLaneId.isValid() && r.track().comp.empty(), "DeletingTheMasterEndsTheComp");
    check(!r.lane(r.take1)->muted && !r.lane(r.take2)->muted && !r.lane(r.take3)->muted, "TheTakesPlayAgain");
    check(r.track().primaryLaneId() == r.take1, "TheFirstTakeLeadsTheTrackAgain");

    del.undo();
    check(r.lane(master) && r.track().compLaneId == master && r.track().comp.ranges().size() == 3 &&
              r.lane(master)->clips.size() == 3,
          "UndoBringsTheWholeCompBack");
    check(r.lane(r.take1)->muted && r.lane(r.take2)->muted && r.lane(r.take3)->muted, "UndoSilencesTheTakesAgain");
}

} // namespace

int main() {
    testDeletingAUsedTakeLeavesAGap();
    testDeletingAnUnusedTakeChangesNothing();
    testDeletingTheMasterEndsTheComp();
    if (g_failures) {
        std::printf("%d CompDeleteLane test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All CompDeleteLane tests passed\n");
    return 0;
}
