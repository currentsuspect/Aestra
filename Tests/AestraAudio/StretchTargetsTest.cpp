// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// StretchTargetsTest — V8-W1 (SPEC-001): the generator and ranker behind "fit this clip to a phrase".
//
// The expected rankings below were worked out BY HAND from the written rules (families, bonuses,
// window, score = |L - T| - bonus), not read back from the code. The spec's own worked examples
// could not be produced by its rules (L=37 listed 40, which is in no family), so the owner ruled
// that the rules win and the examples are regenerated; these are the regenerated examples.

#include "Models/StretchTargets.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << "  " << (cond ? "PASS " : "FAIL ") << what << "\n";
    if (!cond) {
        ++g_failures;
    }
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

bool contains(const std::vector<StretchTarget>& v, int bars) {
    for (const auto& t : v) {
        if (t.bars == bars) return true;
    }
    return false;
}

const StretchTarget* find(const std::vector<StretchTarget>& v, int bars) {
    for (const auto& t : v) {
        if (t.bars == bars) return &t;
    }
    return nullptr;
}

void expectRanked(double L, const std::vector<std::pair<int, double>>& expected) {
    const auto list = rankStretchTargets(L);
    bool ok = list.ranked.size() == expected.size();
    std::string got;
    for (const auto& t : list.ranked) got += std::to_string(t.bars) + " (" + std::to_string(t.score) + ") ";
    for (size_t i = 0; ok && i < expected.size(); ++i) {
        ok = list.ranked[i].bars == expected[i].first && near(list.ranked[i].score, expected[i].second);
    }
    check(ok, "L=" + std::to_string(static_cast<int>(L)) + " ranks as the rules say: " + got);
}

} // namespace

int main() {
    std::cout << "[the regenerated worked examples]\n";
    expectRanked(60, {{64, 1.5}, {48, 10.0}, {32, 25.5}, {30, 28.5}, {90, 29.0}});
    expectRanked(37, {{32, 2.5}, {30, 5.5}, {48, 9.0}, {24, 11.0}, {20, 15.5}});
    expectRanked(14, {{16, -0.5}, {12, 0.0}, {10, 2.5}, {8, 3.5}, {20, 4.5}});
    expectRanked(7, {{8, -1.5}, {6, -0.5}, {4, 0.5}, {10, 1.5}, {12, 3.0}});
    expectRanked(3, {{4, -1.5}, {1, 1.0}, {6, 1.5}});

    std::cout << "[generator rules]\n";
    {
        const auto t60 = generateStretchTargets(60);
        check(find(t60, 24) && find(t60, 24)->cls == StretchTargetClass::Phrase3, "24 is the 12-bar family (bonus 2), not the phrase grid");
        check(find(t60, 64) && find(t60, 64)->cls == StretchTargetClass::Phrase4, "64 is the phrase grid");
        check(find(t60, 20) && find(t60, 20)->cls == StretchTargetClass::Structural,
              "20 is both structural (1.5) and L/3 (1.0): it takes the stronger family");
        check(find(t60, 15) && find(t60, 15)->cls == StretchTargetClass::Fractional, "L/4 = 15 is a same-material relative");
        check(find(t60, 90) && find(t60, 90)->cls == StretchTargetClass::Fractional, "L*3/2 = 90 is a same-material relative");
        check(!contains(t60, 60), "T == L is excluded: a no-op suggestion is noise");
        check(!contains(t60, 8) && !contains(t60, 4), "the window drops anything under L/4");
        check(!contains(t60, 128), "the window drops anything over L*2 (120)");

        const auto t61 = generateStretchTargets(61);
        check(!contains(t61, 31) && !contains(t61, 61), "fractions that are not whole bars are not offered (61/2 = 30.5), nor L itself");
        check(find(t61, 20) && find(t61, 20)->cls == StretchTargetClass::Structural,
              "20 is offered, but as a structural length (61/3 is not a whole bar, so it is not a relative)");
        check(contains(t61, 64), "and the phrase grid still is");

        const auto t3 = generateStretchTargets(3);
        check(contains(t3, 4) && contains(t3, 6) && contains(t3, 1), "a 3-bar clip is offered 1, 4 and 6");
        const auto t1 = generateStretchTargets(1);
        check(t1.size() == 1 && t1[0].bars == 2, "a 1-bar clip still gets a target (2): the menu works for the shortest clip");

        const auto big = generateStretchTargets(300);
        bool capped = true;
        for (const auto& t : big) capped = capped && t.bars <= 256;
        check(capped, "nothing beyond 256 bars is offered");

        check(generateStretchTargets(0).empty() && generateStretchTargets(-4).empty() &&
                  generateStretchTargets(std::nan("")).empty(),
              "a zero, negative or NaN length yields nothing");
    }

    std::cout << "[ranking]\n";
    {
        const auto list = rankStretchTargets(60);
        check(list.ranked.size() == 5, "the menu is the top five");
        bool sorted = true;
        for (size_t i = 1; i < list.ranked.size(); ++i) sorted = sorted && list.ranked[i - 1].score <= list.ranked[i].score;
        check(sorted, "best first");
        check(list.ranked[0].bars == 64, "the primary action, the top candidate, is 64 for a 60-bar clip");
        check(near(list.ranked[0].speed, 60.0 / 64.0), "a target's speed is L / T, the rate that fills it");

        const auto again = rankStretchTargets(60);
        bool same = again.ranked.size() == list.ranked.size();
        for (size_t i = 0; same && i < list.ranked.size(); ++i) same = again.ranked[i].bars == list.ranked[i].bars;
        check(same, "ranking is deterministic");

        const auto negative = rankStretchTargets(14);
        check(negative.ranked[0].score < 0.0, "a negative score means 'musical enough to be worth a nudge' and is the top pick");
    }

    std::cout << "[match arrangement]\n";
    {
        const auto list = rankStretchTargets(60, {27, 64, 60, 27, 200, 1, 36});
        check(list.arrangement.size() == 3, "echo lengths outside the window (200, 1), equal to L (60) or repeated (27) are not shown");
        check(list.arrangement[0].bars == 64 && list.arrangement[1].bars == 36 && list.arrangement[2].bars == 27,
              "the arrangement row is ordered by distance: 64, 36, 27");
        check(list.arrangement[0].cls == StretchTargetClass::Echo, "and marked as echo");
        bool leaked = false;
        for (const auto& t : list.ranked) leaked = leaked || t.cls == StretchTargetClass::Echo;
        check(!leaked, "echo targets never enter the ranking: a random 27-bar clip is not a musical suggestion");
        check(rankStretchTargets(60, {10}).arrangement.empty(), "an echo under L/4 (10 < 15) is outside the window and is not shown");
    }

    std::cout << "[vocabulary]\n";
    {
        StretchTarget t;
        t.bars = 64;
        t.speed = 60.0 / 64.0;
        check(formatStretchTarget(t) == "64 bars \xC2\xB7 \xC3\x97" "0.94 speed \xC2\xB7 pitch follows",
              "a varispeed fit says speed and 'pitch follows', never 'stretch'");
        t.bars = 1;
        t.speed = 2.0;
        check(formatStretchTarget(t).rfind("1 bar \xC2\xB7", 0) == 0, "one bar is singular");
        t.approximate = true;
        check(formatStretchTarget(t).find("approximate") != std::string::npos, "outside the engine's envelope it says so");
        check(formatStretchTarget(t).find("stretch") == std::string::npos, "the word 'stretch' is reserved for a real stretch engine");
    }

    std::cout << (g_failures == 0 ? "[PASS] StretchTargetsTest\n" : "[FAIL] StretchTargetsTest\n");
    return g_failures == 0 ? 0 : 1;
}
