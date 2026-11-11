// © 2026 Aestra Studios — All Rights Reserved.
//
// SetCompRangeCommandTest — comping (SPEC-022 CT-2, FD-70): a swipe is one undoable step.
//
// The first swipe makes the track's master ("Comp") lane; every swipe rebuilds the master's clips from the takes and
// silences the takes. Undo gives back the clips, the comp, the mute flags and (first swipe) removes the lane; redo
// brings back the SAME lane and clip ids.

#include "Commands/SetCompRangeCommand.h"

#include "Commands/CommandRegistry.h"
#include "Commands/CreateLaneCommand.h"
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

bool closeTo(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

struct Rig {
    std::unique_ptr<TrackManager> owner = std::make_unique<TrackManager>();
    TrackManager& tm = *owner;
    PlaylistLaneID take1, take2;
    uint64_t trackId = 0;

    Rig() {
        auto& pl = tm.getPlaylistModel();
        auto a = std::make_shared<CreateLaneCommand>(pl, "Take 1");
        a->execute();
        take1 = a->getLaneId();
        trackId = tm.createTrack(take1, "Vox");
        auto b = std::make_shared<CreateLaneCommand>(pl, "Take 2");
        b->execute();
        take2 = b->getLaneId();
        tm.attachLaneToTrack(trackId, take2);
        for (const PlaylistLaneID& id : {take1, take2}) {
            ClipInstance c;
            c.id = ClipInstanceID::generate();
            c.startBeat = 0;
            c.durationBeats = 16;
            c.sourceId = (id == take1) ? 1 : 2;
            pl.addClip(id, c);
        }
    }
    const PlaylistLane* lane(PlaylistLaneID id) { return tm.getPlaylistModel().getLane(id); }
    const Track& track() { return *tm.getTrack(trackId); }
    SetCompRangeCommand swipe(double a, double b, PlaylistLaneID from) {
        return SetCompRangeCommand(tm, trackId, a, b, from);
    }
};

void testFirstSwipeMakesTheMasterLane() {
    Rig r;
    auto cmd = r.swipe(4, 8, r.take2);
    cmd.execute();
    const PlaylistLaneID master = r.track().compLaneId;
    check(master.isValid() && r.lane(master) && r.lane(master)->trackId == r.trackId,
          "FirstSwipeCreatesAnOwnedMasterLane");
    check(r.track().comp.ranges().size() == 1 && r.track().comp.laneAt(5) == r.take2, "CompRecordsTheSwipe");
    check(r.lane(master)->clips.size() == 1 && closeTo(r.lane(master)->clips[0].startBeat, 4) &&
              r.lane(master)->clips[0].sourceId == 2,
          "MasterPlaysTheChosenTakeAcrossTheSwipe");
    check(r.lane(r.take1)->muted && r.lane(r.take2)->muted && !r.lane(master)->muted, "TakesGoSilentMasterPlays");
    check(r.track().activeLaneId == r.take1, "TheArmedLaneStaysTheArmedLane");
    check(r.lane(master)->clips[0].name == "Take 2", "MasterPieceIsNamedForItsTake");
    check(r.track().primaryLaneId() == master && r.track().takeLanesNewestFirst() == std::vector<PlaylistLaneID>({r.take2, r.take1}),
          "TheMasterLeadsAndTakesFollowNewestFirst");
}

void testSecondSwipeReusesTheMaster() {
    Rig r;
    r.swipe(0, 16, r.take1).execute();
    const PlaylistLaneID master = r.track().compLaneId;
    const size_t lanes = r.track().laneIds.size();
    r.swipe(4, 8, r.take2).execute();
    check(r.track().compLaneId == master && r.track().laneIds.size() == lanes, "SecondSwipeDoesNotMakeAnotherLane");
    const auto& clips = r.lane(master)->clips;
    check(clips.size() == 3 && clips[0].sourceId == 1 && clips[1].sourceId == 2 && clips[2].sourceId == 1 &&
              closeTo(clips[2].startBeat, 8),
          "MasterNowReadsTake1Take2Take1");
}

void testUndoAndRedo() {
    Rig r;
    auto first = r.swipe(0, 16, r.take1);
    first.execute();
    const PlaylistLaneID master = r.track().compLaneId;
    auto second = r.swipe(4, 8, r.take2);
    second.execute();
    const ClipInstanceID idMid = r.lane(master)->clips[1].id;

    second.undo();
    check(r.lane(master)->clips.size() == 1 && r.track().comp.ranges().size() == 1,
          "UndoSecondRestoresThePreviousComp");
    first.undo();
    check(!r.track().compLaneId.isValid() && !r.lane(master) && r.track().laneIds.size() == 2,
          "UndoFirstRemovesTheMasterLane");
    check(!r.lane(r.take1)->muted && !r.lane(r.take2)->muted && r.track().comp.empty(),
          "UndoGivesTheTakesTheirSoundBack");

    first.redo();
    check(r.track().compLaneId == master && r.lane(master) && r.lane(master)->clips.size() == 1,
          "RedoBringsBackTheSameMasterLane");
    second.redo();
    check(r.lane(master)->clips.size() == 3 && r.lane(master)->clips[1].id == idMid, "RedoKeepsClipIdentity");
}

void testRefusals() {
    Rig r;
    auto stranger = std::make_shared<CreateLaneCommand>(r.tm.getPlaylistModel(), "Elsewhere");
    stranger->execute();
    auto bad = r.swipe(0, 4, stranger->getLaneId()); // a lane this track does not own
    bad.execute();
    check(!bad.isUndoable() && !r.track().compLaneId.isValid(), "ATakeFromAnotherTrackIsRefused");
    auto empty = r.swipe(4, 4, r.take1);
    empty.execute();
    check(!empty.isUndoable() && r.track().comp.empty(), "ZeroLengthSwipeIsRefused");
    check(!r.lane(r.take1)->muted, "RefusalLeavesTheTakesAlone");
}

void testMuseVerb() {
    CommandRegistry::initialize();
    Rig r;
    CommandContext ctx;
    ctx.trackManager = &r.tm;
    auto& reg = CommandRegistry::instance();
    auto cmd = reg.build(
        "set_comp", {{"track", std::to_string(r.trackId)}, {"lane", r.take2.toString()}, {"start", "4"}, {"end", "8"}},
        ctx);
    check(cmd != nullptr, "SetCompVerbBuilds");
    if (cmd) {
        cmd->execute();
        check(r.track().comp.laneAt(5) == r.take2, "SetCompVerbSwipes");
    }
    check(reg.build("set_comp", {{"track", "999"}, {"lane", r.take2.toString()}, {"start", "4"}, {"end", "8"}}, ctx) ==
              nullptr,
          "SetCompVerbRefusesAnUnknownTrack");
    check(reg.build("set_comp",
                    {{"track", std::to_string(r.trackId)}, {"lane", r.take2.toString()}, {"start", "8"}, {"end", "4"}},
                    ctx) == nullptr,
          "SetCompVerbRefusesABackwardsRange");
}

} // namespace

int main() {
    testFirstSwipeMakesTheMasterLane();
    testSecondSwipeReusesTheMaster();
    testUndoAndRedo();
    testRefusals();
    testMuseVerb();
    if (g_failures) {
        std::printf("%d SetCompRange test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All SetCompRange tests passed\n");
    return 0;
}
