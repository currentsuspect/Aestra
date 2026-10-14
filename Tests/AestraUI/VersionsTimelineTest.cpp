// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The History timeline's layout (SPEC-017 criterion 1): 3 versions on 2 branches draw 2 lanes and a fork at the
// parent; time runs left to right; the open version is marked; a click finds the node under it.

#include "VersionsTimeline.h"

#include <iostream>

using Aestra::VersionsTimeline;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
TakeManager::TakeEntry v(const char* id, const char* parent, const char* branch, uint64_t at, bool active = false, bool autosave = false) {
    TakeManager::TakeEntry e;
    e.id = id; e.parentId = parent; e.branch = branch; e.createdAtMs = at; e.active = active;
    e.kind = autosave ? TakeManager::TakeEntry::Kind::Autosave : TakeManager::TakeEntry::Kind::Saved;
    return e;
}
} // namespace

int main() {
    const AestraUI::NUIRect area{0.0f, 0.0f, 1000.0f, 500.0f};

    // Save twice and branch once: 3 versions, 2 branches, a fork at the parent.
    const std::vector<TakeManager::TakeEntry> list{v("a", "", "Main", 100), v("b", "a", "Main", 200, true), v("c", "a", "Idea", 300)};
    const auto r = VersionsTimeline::layout(list, area);
    check(r.lanes.size() == 2 && r.lanes[0] == "Main" && r.lanes[1] == "Idea", "two lanes, Main first");
    check(r.nodes.size() == 3 && r.links.size() == 2, "three nodes, two links");
    check(r.nodes[0].x < r.nodes[1].x && r.nodes[1].x < r.nodes[2].x, "time runs left to right");
    check(r.nodes[2].y > r.nodes[0].y && r.nodes[1].y == r.nodes[0].y, "the branch sits on its own lane below Main");
    check(r.nodes[1].open && !r.nodes[0].open && !r.nodes[2].open, "only the open version is marked open");
    bool fork = false;
    for (const auto& l : r.links) if (l.from == 0 && l.to == 2) fork = true;
    check(fork, "the branch links back to its parent");
    check(r.nodes[0].x >= VersionsTimeline::kLabelColumn && r.nodes[2].x <= area.right() - VersionsTimeline::kTailRoom + 0.5f, "nodes stay between the labels and the tail");

    // Autosaves are flagged; a single version still lays out; an empty list gives nothing.
    const auto a = VersionsTimeline::layout({v("a", "", "Main", 100), v("s", "a", "Main", 150, false, true)}, area);
    check(!a.nodes[0].autosave && a.nodes[1].autosave, "autosaves are flagged");
    check(VersionsTimeline::layout({v("only", "", "", 1, true)}, area).lanes.size() == 1, "a version with no branch name is on Main");
    check(VersionsTimeline::layout({}, area).nodes.empty(), "nothing in, no nodes");

    // Ranks follow time, not the list order.
    const auto shuffled = VersionsTimeline::layout({v("late", "", "Main", 900), v("early", "", "Main", 100)}, area);
    check(shuffled.nodes[1].x < shuffled.nodes[0].x, "an earlier version sits left of a later one whatever the list order");

    // Hit-testing.
    check(VersionsTimeline::nodeAt(r, {r.nodes[1].x + 4.0f, r.nodes[1].y - 3.0f}) == 1, "a click near a node finds it");
    check(VersionsTimeline::nodeAt(r, {r.nodes[1].x + 60.0f, r.nodes[1].y}) == -1, "a click away from every node finds none");

    // Time labels.
    const uint64_t now = 1'760'000'000'000ull;
    check(VersionsTimeline::timeLabel(0, now).empty(), "an unset time has no label");
    check(VersionsTimeline::timeLabel(now - 60'000ull, now).rfind("Today", 0) == 0, "a minute ago is Today");
    check(VersionsTimeline::timeLabel(now - 5ull * 86'400'000ull, now).find("Today") == std::string::npos, "five days ago names a date");

    std::cout << (g_failures ? "FAILED\n" : "VersionsTimelineTest passed\n");
    return g_failures ? 1 : 0;
}
