// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Mixer rework slice 6: the strip's solid top tint (owner, 22 Nov). Pure header.

#include "StripWash.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace AestraUI;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}
bool near(float a, float b, float tol = 1e-5f) { return std::fabs(a - b) <= tol; }
} // namespace

int main() {
    std::cout << "[1] the tint covers the top half and stops hard\n";
    check(StripWash::kTopFraction == 0.5f, "the top half");
    check(StripWash::topHeight(452.0f) == 226.0f, "226px of a 452px strip");
    check(StripWash::topHeight(0.0f) == 0.0f && StripWash::topHeight(-3.0f) == 0.0f, "no height, no tint");
    check(near(StripWash::alphaAt(0.0f, 452.0f), StripWash::kAlpha), "full tint at the top");
    check(near(StripWash::alphaAt(100.0f, 452.0f), StripWash::kAlpha), "the same tint mid-way (flat, no fade)");
    check(near(StripWash::alphaAt(225.5f, 452.0f), StripWash::kAlpha), "still tinted just above the edge");
    check(StripWash::alphaAt(226.0f, 452.0f) == 0.0f, "nothing from the edge down");
    check(StripWash::alphaAt(300.0f, 452.0f) == 0.0f && StripWash::alphaAt(452.0f, 452.0f) == 0.0f, "the lower half is the plain strip");

    std::cout << "[2] it is one value: no gradient\n";
    {
        bool flat = true;
        for (float y = 0.0f; y < StripWash::topHeight(452.0f); y += 3.0f)
            if (!near(StripWash::alphaAt(y, 452.0f), StripWash::kAlpha)) flat = false;
        check(flat, "every row of the top half has the same opacity");
    }

    std::cout << "[3] other heights\n";
    check(StripWash::alphaAt(99.0f, 200.0f) == StripWash::kAlpha && StripWash::alphaAt(100.0f, 200.0f) == 0.0f, "a 200px strip ends at 100");
    check(StripWash::alphaAt(std::numeric_limits<float>::quiet_NaN(), 452.0f) == 0.0f, "NaN is nothing");
    check(StripWash::alphaAt(10.0f, 0.0f) == 0.0f, "no height, no tint");

    std::cout << "[4] strength: a tint, not a fill\n";
    check(StripWash::kAlpha > 0.05f && StripWash::kAlpha <= 0.30f, "between 5% and 30%");

    std::cout << "[5] the top rows follow the corners\n";
    check(StripWash::cornerInset(0, 8.0f) >= 2.0f, "row 0 of a radius 8 corner is pulled in");
    check(StripWash::cornerInset(8, 8.0f) == 0.0f && StripWash::cornerInset(20, 8.0f) == 0.0f, "rows past the radius are full width");
    check(StripWash::cornerInset(0, 8.0f) >= StripWash::cornerInset(3, 8.0f), "insets shrink downward");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
