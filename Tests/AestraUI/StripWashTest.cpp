// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Mixer rework slice 4: the Wash top's opacity curve (p-mixer2, locked). Pure header.

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
    const float h = 452.0f;
    std::cout << "[1] the board's stops on a 452px strip\n";
    check(near(StripWash::alphaAt(0.0f, h), 0.26f), "26% at the top");
    check(near(StripWash::alphaAt(60.0f, h), 0.13f), "13% at 60px");
    check(near(StripWash::alphaAt(170.0f, h), 0.07f), "7% at 170px");
    check(near(StripWash::alphaAt(300.0f, h), 0.03f), "3% at 300px");
    check(near(StripWash::alphaAt(h, h), 0.0f), "nothing at the foot");

    std::cout << "[2] between the stops it is linear\n";
    check(near(StripWash::alphaAt(30.0f, h), 0.195f), "halfway to 60px: 19.5%");
    check(near(StripWash::alphaAt(115.0f, h), 0.10f), "halfway between 60 and 170: 10%");
    check(near(StripWash::alphaAt(376.0f, h), 0.015f), "halfway between 300 and the foot: 1.5%");
    {
        bool falling = true;
        float last = 1.0f;
        for (float y = 0.0f; y <= h; y += 4.0f) {
            const float a = StripWash::alphaAt(y, h);
            if (a > last + 1e-6f) falling = false;
            last = a;
        }
        check(falling, "it never brightens going down");
    }

    std::cout << "[3] strips of other heights\n";
    check(near(StripWash::alphaAt(0.0f, 200.0f), 0.26f) && near(StripWash::alphaAt(200.0f, 200.0f), 0.0f),
          "a 200px strip still runs from 26% to nothing");
    check(StripWash::alphaAt(60.0f, 200.0f) > StripWash::alphaAt(150.0f, 200.0f), "and falls");
    check(StripWash::alphaAt(10.0f, 0.0f) == 0.0f && StripWash::alphaAt(10.0f, -5.0f) == 0.0f, "no height, no wash");
    check(StripWash::alphaAt(std::numeric_limits<float>::quiet_NaN(), h) == 0.0f, "NaN is nothing");
    check(near(StripWash::alphaAt(-5.0f, h), 0.26f), "above the strip clamps to the top value");

    std::cout << "[4] the top rows follow the corners\n";
    check(StripWash::cornerInset(0, 8.0f) >= 2.0f, "row 0 of a radius 8 corner is pulled in");
    check(StripWash::cornerInset(8, 8.0f) == 0.0f && StripWash::cornerInset(20, 8.0f) == 0.0f, "rows past the radius are full width");
    check(StripWash::cornerInset(0, 8.0f) >= StripWash::cornerInset(3, 8.0f), "insets shrink downward");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
