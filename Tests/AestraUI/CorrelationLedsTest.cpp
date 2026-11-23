// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Mixer rework slice 5: the correlation meter as dotted LEDs. Pure header, no libraries.

#include "CorrelationLeds.h"

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
bool near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }
bool none(const CorrelationLeds::Range& r) { return r.to < r.from; }
} // namespace

int main() {
    std::cout << "[1] the well and the pitch match the level meters\n";
    check(CorrelationLeds::kPitch == CorrelationLeds::kSegW + 1.0f, "3px on + 1px off");
    check(CorrelationLeds::kWellH == 7.0f, "well = 3 + padding 2 + 2");
    check(CorrelationLeds::kWellRadius == 3.0f, "radius 3");

    std::cout << "[2] segment count is odd, so zero has a segment\n";
    {
        struct Row { float w; int n; };
        const Row rows[] = {{0.0f, 0}, {2.0f, 0}, {3.0f, 1}, {7.0f, 1}, {11.0f, 3}, {14.0f, 3}, {15.0f, 3}, {19.0f, 5}, {74.0f, 17}, {76.0f, 19}};
        for (const auto& r : rows)
            check(CorrelationLeds::segmentCount(r.w) == r.n, "lane " + std::to_string(r.w) + " holds " + std::to_string(r.n));
        bool odd = true;
        for (float w = 3.0f; w < 120.0f; w += 1.0f)
            if (CorrelationLeds::segmentCount(w) % 2 == 0) odd = false;
        check(odd, "always odd");
        check(CorrelationLeds::segmentCount(std::numeric_limits<float>::quiet_NaN()) == 0, "NaN lane holds none");
    }

    std::cout << "[3] positions: centred row on a 4px pitch\n";
    {
        const float lane = 74.0f; // 17 segments: 67px of row
        check(near(CorrelationLeds::segmentX(0, 17, lane), 3.5f), "first segment starts at 3.5");
        check(near(CorrelationLeds::segmentX(1, 17, lane) - CorrelationLeds::segmentX(0, 17, lane), 4.0f), "4px pitch");
        const float mid = CorrelationLeds::segmentX(8, 17, lane) + CorrelationLeds::kSegW * 0.5f;
        check(near(mid, lane * 0.5f), "the centre segment sits in the middle of the lane");
        check(CorrelationLeds::centreIndex(17) == 8, "centre index");
    }

    std::cout << "[4] the reading lights segments from the centre outward (17 segments, 8 a side)\n";
    {
        const int n = 17, c = 8;
        auto r = CorrelationLeds::lit(0.0f, n);
        check(none(r), "0 lights nothing but the marker");
        r = CorrelationLeds::lit(1.0f, n);
        check(r.from == c + 1 && r.to == n - 1, "+1 lights every segment right of centre");
        r = CorrelationLeds::lit(-1.0f, n);
        check(r.from == 0 && r.to == c - 1, "-1 lights every segment left of centre");
        r = CorrelationLeds::lit(0.5f, n);
        check(r.from == c + 1 && r.to == c + 4, "+0.5 lights four to the right");
        r = CorrelationLeds::lit(-0.25f, n);
        check(r.from == c - 2 && r.to == c - 1, "-0.25 lights two to the left");
        check(none(CorrelationLeds::lit(0.05f, n)), "a hair off zero lights none (rounds to zero steps)");
        r = CorrelationLeds::lit(7.0f, n);
        check(r.to == n - 1, "above +1 clamps");
        check(none(CorrelationLeds::lit(std::numeric_limits<float>::quiet_NaN(), n)), "NaN reads as zero");
        check(none(CorrelationLeds::lit(0.8f, 1)), "no room, nothing lit");
        r = CorrelationLeds::lit(0.8f, 17);
        const auto l = CorrelationLeds::lit(-0.8f, 17);
        check(r.to - r.from == l.to - l.from, "positive and negative are mirror images");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
