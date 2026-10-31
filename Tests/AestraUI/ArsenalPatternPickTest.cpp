// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ArsenalPatternPickTest — #999: when nothing is on show (a project just opened, or the pattern on show was deleted),
// the Arsenal and the piano roll show a pattern with notes, not an empty "Pattern 1" the arrangement never uses.

#include "Panels/ArsenalPatternPick.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << what << "\n";
    if (!cond)
        ++g_failures;
}
} // namespace

int main() {
    using Aestra::ArsenalPatternPick::Candidate;
    using Aestra::ArsenalPatternPick::patternToShow;

    // The owner's project in #999: pattern 1 is empty, the arrangement uses pattern 2 (45 notes). Unordered, as the
    // pattern store is.
    check(patternToShow({{2, 45}, {1, 0}, {5, 3}}) == 2, "the first pattern with notes, not the empty pattern 1");
    check(patternToShow({{4, 0}, {3, 0}}) == 3, "all empty: the first one, so something is on show");
    check(patternToShow({{7, 12}}) == 7, "a single pattern is shown");
    check(patternToShow({}) == 0, "no patterns: nothing");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
