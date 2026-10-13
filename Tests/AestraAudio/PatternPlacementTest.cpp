// © 2026 Aestra Studios — All Rights Reserved.
// PatternPlacementTest: where "Place in timeline" puts a pattern.

#include "Models/PatternPlacement.h"

#include <iostream>
#include <string>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
} // namespace

int main() {
    constexpr uint64_t A = 1, B = 2;
    std::cout << "[1] the first copy goes to the bar the cursor is in\n";
    auto t = planPlacement({}, 0, A, 8.0, 5.5, 4.0);
    check(t.startBeat == 4.0, "cursor at beat 5.5 starts at bar 2 (beat 4)");
    check(t.lane == -1, "no lanes at all: make one");
    t = planPlacement({}, 3, A, 8.0, 0.0, 4.0);
    check(t.startBeat == 0.0 && t.lane == 0, "an empty arrangement: first lane, from the start");
    t = planPlacement({}, 2, A, 8.0, -3.0, 4.0);
    check(t.startBeat == 0.0, "a negative cursor is treated as the start");

    std::cout << "[2] a repeat press stacks after the last copy\n";
    std::vector<PlacedClip> clips{{0, 0.0, 8.0, A}};
    t = planPlacement(clips, 1, A, 8.0, 0.0, 4.0);
    check(t.startBeat == 8.0 && t.lane == 0, "the next copy follows the last on the same lane");
    clips.push_back({0, 8.0, 16.0, A});
    t = planPlacement(clips, 1, A, 8.0, 0.0, 4.0);
    check(t.startBeat == 16.0, "and again after that");
    t = planPlacement(clips, 1, A, 8.0, 100.0, 4.0);
    check(t.startBeat == 16.0, "the cursor is ignored once a copy exists");

    std::cout << "[3] a clip is never placed over another\n";
    std::vector<PlacedClip> mixed{{0, 0.0, 8.0, A}, {0, 8.0, 12.0, B}};
    t = planPlacement(mixed, 2, A, 8.0, 0.0, 4.0);
    check(t.startBeat == 8.0 && t.lane == 1, "another pattern in the way: the first free lane instead");
    t = planPlacement(mixed, 1, A, 8.0, 0.0, 4.0);
    check(t.lane == -1 && t.startBeat == 8.0, "no free lane: a new one, at the same beat");
    std::vector<PlacedClip> other{{0, 0.0, 4.0, B}};
    t = planPlacement(other, 2, A, 8.0, 2.0, 4.0);
    check(t.startBeat == 0.0 && t.lane == 1, "the cursor's bar is taken on lane 0, so lane 1 is used");
    std::vector<PlacedClip> touching{{0, 0.0, 4.0, B}};
    t = planPlacement(touching, 1, A, 4.0, 4.0, 4.0);
    check(t.lane == 0 && t.startBeat == 4.0, "a clip that only touches the edge does not overlap");

    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
