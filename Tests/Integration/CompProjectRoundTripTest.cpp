// © 2026 Aestra Studios — All Rights Reserved.
//
// CompProjectRoundTripTest — a comp survives save and load (SPEC-022 CT-2).
//
// The master lane and its clips already ride in the lane list; what must be saved is which lane is the master and which
// take plays where. A comp naming a lane the file does not have is dropped, not guessed at.

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Commands/CreateLaneCommand.h"
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
bool closeTo(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}
} // namespace

int main() {
    const Aestra::Tests::ScopedTempDirectory dir{"CompProjectRoundTrip"};
    const auto path = dir.path() / "comp.aes";

    auto tm1 = std::make_shared<TrackManager>();
    tm1->getPlaylistModel().setPatternManager(&tm1->getPatternManager());
    auto& pl = tm1->getPlaylistModel();
    pl.setBPM(120.0);
    auto* channel = tm1->addChannel("Vox");
    auto c1 = std::make_shared<CreateLaneCommand>(pl, "Take 1");
    c1->execute();
    const uint64_t trackId = tm1->createTrack(c1->getLaneId(), "Vox", channel->getChannelId());
    auto c2 = std::make_shared<CreateLaneCommand>(pl, "Take 2");
    c2->execute();
    tm1->attachLaneToTrack(trackId, c2->getLaneId());
    for (const PlaylistLaneID& id : {c1->getLaneId(), c2->getLaneId()}) {
        ClipInstance clip;
        clip.id = ClipInstanceID::generate();
        clip.durationBeats = 16;
        clip.sourceId = 5;
        pl.addClip(id, clip);
    }
    SetCompRangeCommand(*tm1, trackId, 0, 16, c1->getLaneId()).execute();
    SetCompRangeCommand(*tm1, trackId, 4, 8, c2->getLaneId()).execute();
    const PlaylistLaneID master = tm1->getTrack(trackId)->compLaneId;
    check(master.isValid(), "setup: a comp exists");

    auto ser = ProjectSerializer::serialize(tm1, 120.0, 0.0, 2);
    check(ser.ok && ProjectSerializer::writeAtomically(path.string(), ser.contents), "project saves");

    auto tm2 = std::make_shared<TrackManager>();
    auto load = ProjectSerializer::load(path.string(), tm2);
    check(load.ok, "project loads");
    const Track* t = tm2->getTrack(trackId);
    check(t && t->compLaneId == master, "MasterLaneSurvives");
    check(t && t->comp.ranges().size() == 3 && t->comp.laneAt(1) == c1->getLaneId() &&
              t->comp.laneAt(5) == c2->getLaneId() && t->comp.laneAt(10) == c1->getLaneId() &&
              closeTo(t->comp.ranges()[1].startBeat, 4) && closeTo(t->comp.ranges()[1].endBeat, 8),
          "WhichTakePlaysWhereSurvives");
    const PlaylistLane* m = tm2->getPlaylistModel().getLane(master);
    check(m && m->clips.size() == 3, "MasterLaneClipsSurvive");
    const PlaylistLane* take = tm2->getPlaylistModel().getLane(c1->getLaneId());
    check(take && take->muted, "SilencedTakesStaySilent");

    if (g_failures) {
        std::printf("%d CompProjectRoundTrip test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All CompProjectRoundTrip tests passed\n");
    return 0;
}
