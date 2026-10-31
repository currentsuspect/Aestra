// © 2026 Aestra Studios — All Rights Reserved.
//
// CompRangesTest — comping (SPEC-022) slice 1: the comp model and the clips it turns into.
//
// A comp says which take plays where. These tests hold the arithmetic: a swipe replaces what it covers and nothing
// else, neighbours on the same take join, and the clips handed to playback read the right audio at the right offset.

#include "Models/CompRanges.h"

#include <cmath>
#include <cstdio>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++g_failures;
}

bool near(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

const PlaylistLaneID A = PlaylistLaneID::generate();
const PlaylistLaneID B = PlaylistLaneID::generate();
const PlaylistLaneID C = PlaylistLaneID::generate();

void testSwipeInsideReplacesOnlyItsSpan() {
    CompRanges comp;
    comp.set(0, 16, A);
    comp.set(4, 8, B);
    const auto& r = comp.ranges();
    check(r.size() == 3 && r[0].laneId == A && near(r[0].startBeat, 0) && near(r[0].endBeat, 4) && r[1].laneId == B &&
              near(r[1].startBeat, 4) && near(r[1].endBeat, 8) && r[2].laneId == A && near(r[2].startBeat, 8) &&
              near(r[2].endBeat, 16),
          "SwipeInsideSplitsTheSurroundingRange");
}

void testSwipeOverManyReplacesAll() {
    CompRanges comp;
    comp.set(0, 4, A);
    comp.set(4, 8, B);
    comp.set(8, 12, A);
    comp.set(2, 10, C);
    const auto& r = comp.ranges();
    check(r.size() == 3 && r[0].laneId == A && near(r[0].endBeat, 2) && r[1].laneId == C && near(r[1].startBeat, 2) &&
              near(r[1].endBeat, 10) && r[2].laneId == A && near(r[2].startBeat, 10) && near(r[2].endBeat, 12),
          "SwipeAcrossSeveralRangesTrimsTheEndsAndDropsTheMiddle");
}

void testNeighboursOnTheSameTakeJoin() {
    CompRanges comp;
    comp.set(0, 4, A);
    comp.set(8, 12, A);
    comp.set(4, 8, A);
    check(comp.ranges().size() == 1 && near(comp.ranges()[0].startBeat, 0) && near(comp.ranges()[0].endBeat, 12),
          "TouchingRangesOnOneTakeBecomeOne");
    comp.set(4, 8, B);
    comp.set(4, 8, A);
    check(comp.ranges().size() == 1, "SwipingBackTheSameTakeRejoinsIt");
}

void testGapsAndLookup() {
    CompRanges comp;
    comp.set(4, 8, B);
    check(!comp.laneAt(2).isValid(), "NothingPlaysBeforeTheFirstRange");
    check(comp.laneAt(4) == B && comp.laneAt(7.999) == B, "RangeStartIsInsideAndEndIsOutside");
    check(!comp.laneAt(8).isValid(), "RangeEndIsExclusive");
    comp.set(5, 5, A);
    check(comp.ranges().size() == 1, "ZeroLengthSwipeChangesNothing");
}

void testRemovedTakeBecomesAGap() {
    CompRanges comp;
    comp.set(0, 8, A);
    comp.set(4, 6, B);
    comp.removeLane(B);
    check(comp.ranges().size() == 2 && !comp.laneAt(5).isValid() && comp.laneAt(1) == A && comp.laneAt(7) == A,
          "DeletedTakeLeavesAGapNotAGuess");
}

ClipInstance audioClip(double start, double dur, double offsetSec) {
    ClipInstance c;
    c.id = ClipInstanceID::generate();
    c.startBeat = start;
    c.durationBeats = dur;
    c.sourceOffsetSeconds = offsetSec;
    c.sourceId = 77;
    return c;
}

void testClipsReadTheRightAudio() {
    // 120 bpm: one beat = 0.5 s. Take A's clip spans beats 0..16 and starts 1 s into its file.
    const std::vector<ClipInstance> takeA = {audioClip(0, 16, 1.0)};
    const std::vector<ClipInstance> takeB = {audioClip(0, 16, 0.0)};
    CompRanges comp;
    comp.set(0, 16, A);
    comp.set(4, 8, B);
    const auto clips = compClipsFor(comp, {{A, &takeA}, {B, &takeB}}, 120.0);
    check(clips.size() == 3, "OneClipPerRange");
    if (clips.size() != 3)
        return;
    check(near(clips[0].startBeat, 0) && near(clips[0].durationBeats, 4) && near(clips[0].sourceOffsetSeconds, 1.0),
          "FirstPieceKeepsItsOffset");
    // B's piece starts 4 beats = 2 s into B's clip, which itself starts at offset 0.
    check(near(clips[1].startBeat, 4) && near(clips[1].durationBeats, 4) && near(clips[1].sourceOffsetSeconds, 2.0) &&
              near(clips[1].durationSeconds, 2.0),
          "CutPieceAdvancesTheAudioOffsetByTheCut");
    // A's last piece starts at beat 8: 8 beats = 4 s later than A's own offset of 1 s.
    check(near(clips[2].startBeat, 8) && near(clips[2].sourceOffsetSeconds, 5.0),
          "ResumedTakeLandsWhereItWouldHaveBeen");
    check(clips[0].id != clips[1].id && clips[1].id != clips[2].id && clips[0].id != takeA[0].id, "PiecesGetFreshIds");
    check(clips[1].sourceId == 77, "PiecesReadTheSameSource");
}

void testRangeOutsideTheTakesClipsYieldsNothing() {
    const std::vector<ClipInstance> take = {audioClip(8, 4, 0.0)}; // plays beats 8..12 only
    CompRanges comp;
    comp.set(0, 16, A);
    const auto clips = compClipsFor(comp, {{A, &take}}, 120.0);
    check(clips.size() == 1 && near(clips[0].startBeat, 8) && near(clips[0].durationBeats, 4),
          "OnlyTheOverlapIsPlayed");
}

} // namespace

int main() {
    testSwipeInsideReplacesOnlyItsSpan();
    testSwipeOverManyReplacesAll();
    testNeighboursOnTheSameTakeJoin();
    testGapsAndLookup();
    testRemovedTakeBecomesAGap();
    testClipsReadTheRightAudio();
    testRangeOutsideTheTakesClipsYieldsNothing();
    if (g_failures) {
        std::printf("%d CompRanges test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All CompRanges tests passed\n");
    return 0;
}
