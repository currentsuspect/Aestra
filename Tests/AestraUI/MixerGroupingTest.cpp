// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-28 point 1: the mixer groups strips by where they go. The rule is pure, so it is checked here without a
// window: a bus closes the group it feeds, loose channels group under the master (last), the project's
// channel order holds inside a group, and routes to nothing or in a loop count as going to the master.

#include "MixerGrouping.h"

#include <iostream>
#include <string>

using namespace Aestra;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

std::string names(const std::vector<MixerGroupInput>& in, const MixerGroupedOrder& g) {
    std::string out;
    for (size_t slot : g.order)
        out += (out.empty() ? "" : " ") + in[slot].name;
    return out;
}

} // namespace

int main() {
    std::cout << "[1] a flat project stays flat\n";
    {
        std::vector<MixerGroupInput> in{{1, 0, "Pad", 0}, {2, 0, "Pluck", 1}, {3, 0, "Lead", 2}};
        const auto g = groupMixerStripsByOutput(in);
        check(names(in, g) == "Pad Pluck Lead", "order is the project's: '" + names(in, g) + "'");
        check(g.groups.size() == 1 && !g.grouped(), "one group, so no headers");
        check(g.groups[0].label == "Master" && g.groups[0].targetId == 0, "and it is the master's");
    }

    std::cout << "[2] a bus closes the group it feeds; master channels come last\n";
    {
        std::vector<MixerGroupInput> in{{1, 0, "Pad", 0},     {2, 10, "Kick", 1}, {3, 10, "Snare", 2},
                                        {4, 0, "Lead", 3},    {10, 0, "Drum Bus", 4}, {5, 10, "Hat", 5}};
        const auto g = groupMixerStripsByOutput(in);
        check(names(in, g) == "Kick Snare Hat Drum Bus Pad Lead", "got '" + names(in, g) + "'");
        check(g.grouped() && g.groups.size() == 2, "two groups");
        check(g.groups[0].label == "Drum Bus" && g.groups[0].targetId == 10 && g.groups[0].colorIndex == 4,
              "the first is the Drum Bus group, in the bus's colour");
        check(g.groups[0].first == 0 && g.groups[0].count == 4, "it holds the three feeders and the bus");
        check(g.groups[1].label == "Master" && g.groups[1].first == 4 && g.groups[1].count == 2,
              "the master group holds the two loose channels, after it");
    }

    std::cout << "[3] nested buses: the inner one closes its own group\n";
    {
        std::vector<MixerGroupInput> in{{1, 20, "Kick", 0}, {2, 20, "Snare", 1}, {20, 30, "Drums", 2},
                                        {3, 30, "Bass", 3}, {30, 0, "Rhythm Bus", 4}};
        const auto g = groupMixerStripsByOutput(in);
        check(names(in, g) == "Kick Snare Drums Bass Rhythm Bus", "got '" + names(in, g) + "'");
        check(g.groups.size() == 2 && g.groups[0].label == "Drums" && g.groups[1].label == "Rhythm Bus",
              "inner group first, then the outer one");
        check(g.groups[1].count == 2, "the outer group is Bass and the Rhythm Bus that closes it");
    }

    std::cout << "[4] damaged routing counts as the master\n";
    {
        std::vector<MixerGroupInput> in{{1, 99, "Ghost", 0}, {2, 3, "A", 1}, {3, 2, "B", 2}, {4, 4, "Self", 3}};
        const auto g = groupMixerStripsByOutput(in);
        check(g.order.size() == 4, "every channel is still shown once");
        bool masterOnly = true;
        for (const auto& group : g.groups)
            masterOnly = masterOnly && group.targetId != 99;
        check(masterOnly, "no group is made for a bus that does not exist");
    }

    std::cout << "[5] every channel appears exactly once\n";
    {
        std::vector<MixerGroupInput> in;
        for (uint32_t i = 1; i <= 12; ++i)
            in.push_back({i, (i % 4 == 0) ? 0u : (i < 9 ? 100u + i % 3 : 0u), "c" + std::to_string(i), 0});
        for (uint32_t b = 100; b <= 102; ++b)
            in.push_back({b, 0, "bus" + std::to_string(b), 0});
        const auto g = groupMixerStripsByOutput(in);
        std::vector<int> seen(in.size(), 0);
        for (size_t slot : g.order)
            ++seen[slot];
        bool once = g.order.size() == in.size();
        for (int s : seen)
            once = once && s == 1;
        check(once, "a mixed project of 15 channels loses and doubles none");
        size_t counted = 0;
        for (const auto& group : g.groups)
            counted += group.count;
        check(counted == in.size(), "and the groups add up to all of them");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
