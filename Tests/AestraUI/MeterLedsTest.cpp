// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Mixer rework slice 2: the stepped LED meter's geometry (p-mixer2, locked). Pure headers, no libraries.

#include "MeterLeds.h"
#include "MeterZones.h"

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
} // namespace

int main() {
    std::cout << "[1] the well is 18 wide: padding 2 + 6 + gap 2 + 6 + padding 2\n";
    check(MeterLeds::kWellW == 18.0f, "kWellW = 18");
    check(MeterLeds::kPitch == MeterLeds::kLedH + 1.0f, "pitch = 3px on + 1px off");

    std::cout << "[2] how many LEDs fit a lane\n";
    {
        struct Row { float lane; int count; };
        const Row rows[] = {{0.0f, 0}, {2.0f, 0}, {3.0f, 1}, {6.0f, 1}, {7.0f, 2}, {11.0f, 3}, {100.0f, 25}, {256.0f, 64}};
        for (const auto& r : rows)
            check(MeterLeds::ledCount(r.lane) == r.count, "lane " + std::to_string(r.lane) + " holds " + std::to_string(r.count));
        check(MeterLeds::ledCount(-5.0f) == 0, "a negative lane holds none");
        check(MeterLeds::ledCount(std::numeric_limits<float>::quiet_NaN()) == 0, "NaN lane holds none");
    }

    std::cout << "[3] how many LEDs a level lights (count 20)\n";
    {
        struct Row { float level; int lit; };
        const Row rows[] = {{0.0f, 0}, {-1.0f, 0}, {0.02f, 0}, {0.03f, 1}, {0.5f, 10}, {0.974f, 19}, {0.976f, 20}, {1.0f, 20}, {7.0f, 20}};
        for (const auto& r : rows)
            check(MeterLeds::litCount(r.level, 20) == r.lit, "level " + std::to_string(r.level) + " lights " + std::to_string(r.lit));
        check(MeterLeds::litCount(std::numeric_limits<float>::quiet_NaN(), 20) == 0, "NaN lights none");
        check(MeterLeds::litCount(0.5f, 0) == 0, "no LEDs, none lit");
    }

    std::cout << "[4] LED positions: 4px pitch, bottom one flush on the lane\n";
    {
        const float lane = 100.0f;
        check(near(MeterLeds::ledTop(0, lane), 97.0f), "LED 0 spans 97..100");
        check(near(MeterLeds::ledTop(1, lane), 93.0f), "LED 1 spans 93..96 (1px of well between)");
        const int n = MeterLeds::ledCount(lane);
        check(MeterLeds::ledTop(n - 1, lane) >= 0.0f, "the top LED stays inside the lane");
        check(MeterLeds::ledTop(n, lane) < 0.0f, "one more would not fit");
    }

    std::cout << "[5] LED colours come from the zone of the segment's scale fraction\n";
    {
        const int n = 20;
        int green = 0, amber = 0, red = 0;
        for (int i = 0; i < n; ++i) {
            switch (MeterZones::meterZone(MeterLeds::ledFraction(i, n))) {
            case MeterZones::Zone::Green: ++green; break;
            case MeterZones::Zone::Amber: ++amber; break;
            case MeterZones::Zone::Red: ++red; break;
            }
        }
        // centres 0.025..0.975 in steps of 0.05: green below 0.70 (14), amber 0.70..0.88 (4: .725 .775 .825 .875), red above (2)
        check(green == 14 && amber == 4 && red == 2, "20 LEDs split 14 green / 4 amber / 2 red");
        check(MeterZones::meterZone(MeterLeds::ledFraction(0, n)) == MeterZones::Zone::Green, "bottom LED is green");
        check(MeterZones::meterZone(MeterLeds::ledFraction(n - 1, n)) == MeterZones::Zone::Red, "top LED is red");
        check(near(MeterLeds::ledFraction(0, 0), 0.0f), "no LEDs, fraction 0");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
