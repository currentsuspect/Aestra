// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Mixer rework slice 2: the fader slot's groove geometry and colour (p-mixer2 variant F, locked). Pure header.

#include "SlotGroove.h"

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
    std::cout << "[1] the slot's fixed numbers\n";
    check(SlotGroove::kSlotW == 6.0f, "6px wide");
    check(SlotGroove::kSlotRadius == 1.0f, "radius 1");
    check(SlotGroove::kBandH == SlotGroove::kPlainH + 1.0f, "band = 3px plain + 1px line");

    std::cout << "[2] how many whole lines fit\n";
    {
        struct Row { float h; int n; };
        const Row rows[] = {{0.0f, 0}, {3.0f, 0}, {4.0f, 1}, {7.0f, 1}, {8.0f, 2}, {100.0f, 25}, {219.0f, 54}};
        for (const auto& r : rows)
            check(SlotGroove::lineCount(r.h) == r.n, "slot " + std::to_string(r.h) + " holds " + std::to_string(r.n) + " lines");
        check(SlotGroove::lineCount(-3.0f) == 0, "negative slot holds none");
        check(SlotGroove::lineCount(std::numeric_limits<float>::quiet_NaN()) == 0, "NaN slot holds none");
    }

    std::cout << "[3] line positions: 3, 7, 11 from the slot's top\n";
    check(near(SlotGroove::lineTop(0), 3.0f), "line 0 at 3");
    check(near(SlotGroove::lineTop(1), 7.0f), "line 1 at 7");
    check(near(SlotGroove::lineTop(2), 11.0f), "line 2 at 11");
    {
        const float h = 100.0f;
        const int n = SlotGroove::lineCount(h);
        check(SlotGroove::lineTop(n - 1) + 1.0f <= h, "the last counted line ends inside the slot");
        check(SlotGroove::lineTop(n) + 1.0f > h || n * 4 + 3 >= h, "the next line would be cut off or beyond");
    }

    std::cout << "[4] the lighter line is meterBackground 4% toward white\n";
    check(near(SlotGroove::liftChannel(0.0f), 0.04f), "black lifts to 0.04");
    check(near(SlotGroove::liftChannel(1.0f), 1.0f), "white stays white");
    check(near(SlotGroove::liftChannel(0.5f), 0.52f), "0.5 lifts to 0.52");
    check(SlotGroove::liftChannel(0.1f) > 0.1f, "never darker");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
