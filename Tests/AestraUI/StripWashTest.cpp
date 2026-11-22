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
    std::cout << "[1] the tint ends exactly where the lane starts (from the layout, not a percentage)\n";
    {
        struct Row { const char* what; float laneTop, height, expect; };
        // lane top as laid out by the strip: Wide (taller header, more insert rows), Normal, Narrow (header only
        // name + keys), and a strip with extra sends pushing the lane lower.
        const Row rows[] = {
            {"Normal 452px, lane at 232", 232.0f, 452.0f, 232.0f},
            {"Normal, one send pushes the lane to 252", 252.0f, 452.0f, 252.0f},
            {"Wide 560px, lane at 330", 330.0f, 560.0f, 330.0f},
            {"Narrow 452px, lane at 118", 118.0f, 452.0f, 118.0f},
            {"short 300px strip, lane at 190", 190.0f, 300.0f, 190.0f},
            {"fractional lane top rounds to whole px", 231.6f, 452.0f, 232.0f},
        };
        for (const auto& r : rows) {
            check(StripWash::tintHeight(r.laneTop, r.height) == r.expect, std::string(r.what) + ": tint is exactly the lane top");
            check(near(StripWash::alphaAt(r.expect - 1.0f, r.laneTop, r.height), StripWash::kAlpha), std::string(r.what) + ": tinted just above the lane");
            check(StripWash::alphaAt(r.expect, r.laneTop, r.height) == 0.0f, std::string(r.what) + ": plain from the lane's top on");
            check(StripWash::alphaAt(r.height - 1.0f, r.laneTop, r.height) == 0.0f, std::string(r.what) + ": the whole lane is plain");
        }
        check(StripWash::tintHeight(232.0f, 452.0f) != 226.0f, "no longer the old 50% edge (226 of 452)");
    }
    std::cout << "[2] it is one value: no gradient\n";
    {
        bool flat = true;
        for (float y = 0.0f; y < StripWash::tintHeight(232.0f, 452.0f); y += 3.0f)
            if (!near(StripWash::alphaAt(y, 232.0f, 452.0f), StripWash::kAlpha)) flat = false;
        check(flat, "every row of the header zone has the same opacity");
    }

    std::cout << "[3] bad input is no tint, never a stray one\n";
    check(StripWash::tintHeight(100.0f, 0.0f) == 0.0f && StripWash::tintHeight(100.0f, -3.0f) == 0.0f, "no height, no tint");
    check(StripWash::tintHeight(-20.0f, 452.0f) == 0.0f, "a lane above the strip: no tint");
    check(StripWash::tintHeight(900.0f, 452.0f) == 452.0f, "a lane below the strip: capped at the strip");
    check(StripWash::tintHeight(std::numeric_limits<float>::quiet_NaN(), 452.0f) == 0.0f, "NaN lane top is nothing");
    check(StripWash::alphaAt(std::numeric_limits<float>::quiet_NaN(), 232.0f, 452.0f) == 0.0f, "NaN row is nothing");

    std::cout << "[4] strength: a tint, not a fill\n";
    check(StripWash::kAlpha > 0.05f && StripWash::kAlpha <= 0.30f, "between 5% and 30%");

    std::cout << "[5] the top rows follow the corners\n";
    check(StripWash::cornerInset(0, 8.0f) >= 2.0f, "row 0 of a radius 8 corner is pulled in");
    check(StripWash::cornerInset(8, 8.0f) == 0.0f && StripWash::cornerInset(20, 8.0f) == 0.0f, "rows past the radius are full width");
    check(StripWash::cornerInset(0, 8.0f) >= StripWash::cornerInset(3, 8.0f), "insets shrink downward");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
