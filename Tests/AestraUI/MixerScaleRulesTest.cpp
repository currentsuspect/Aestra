// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Mixer rework slice 1: the board's dB scale (p-mixer2, locked) and the meter's height-fraction colour zones.
// Pure headers, no libraries.

#include "MeterZones.h"
#include "MixerScale.h"

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
    std::cout << "[1] every board knot, dB -> position\n";
    {
        struct Knot { float db, norm; };
        const Knot knots[] = {{6.0f, 1.0f},  {0.0f, 0.75f},  {-6.0f, 0.60f}, {-12.0f, 0.47f},
                              {-24.0f, 0.32f}, {-48.0f, 0.12f}, {-80.0f, 0.02f}};
        for (const auto& k : knots)
            check(near(MixerScale::norm(k.db), k.norm), "norm(" + std::to_string(k.db) + ") = " + std::to_string(k.norm));
        // The board's scale has no -18 / -36 knot: those sit on the straight line between their neighbours.
        check(near(MixerScale::norm(-18.0f), (0.47f + 0.32f) / 2.0f), "-18 dB is the midpoint of -12 and -24 (no extra knot)");
        check(near(MixerScale::norm(-36.0f), (0.32f + 0.12f) / 2.0f), "-36 dB is the midpoint of -24 and -48 (no extra knot)");
    }

    std::cout << "[2] every board knot, position -> dB\n";
    {
        struct Knot { float db, norm; };
        const Knot knots[] = {{6.0f, 1.0f},  {0.0f, 0.75f},  {-6.0f, 0.60f}, {-12.0f, 0.47f},
                              {-24.0f, 0.32f}, {-48.0f, 0.12f}};
        for (const auto& k : knots)
            check(near(MixerScale::db(k.norm), k.db, 1e-3f), "db(" + std::to_string(k.norm) + ") = " + std::to_string(k.db));
    }

    std::cout << "[3] floor and minus infinity\n";
    {
        check(MixerScale::kFloorDb == -80.0f, "the floor is -80 dB");
        check(MixerScale::norm(-81.0f) == 0.0f && MixerScale::norm(-200.0f) == 0.0f, "below the floor reads as nothing");
        check(MixerScale::norm(-std::numeric_limits<float>::infinity()) == 0.0f, "-inf is nothing");
        check(MixerScale::norm(std::numeric_limits<float>::quiet_NaN()) == 0.0f, "NaN is nothing");
        check(MixerScale::norm(20.0f) == 1.0f, "above +6 clamps to the top");
        check(std::isinf(MixerScale::db(0.02f)) && MixerScale::db(0.02f) < 0.0f, "db(0.02) is -inf");
        check(std::isinf(MixerScale::db(0.0f)) && MixerScale::db(0.0f) < 0.0f, "db(0) is -inf");
        check(std::isinf(MixerScale::db(0.019f)), "below 0.02 is -inf");
        check(!std::isinf(MixerScale::db(0.021f)) && MixerScale::db(0.021f) < -79.0f, "just above 0.02 is just above -80");
        check(MixerScale::db(2.0f) == 6.0f, "above the top clamps to +6");
    }

    std::cout << "[4] monotonic and round trip\n";
    {
        bool monotone = true;
        float last = -1.0f;
        for (float d = -100.0f; d <= 10.0f; d += 0.25f) {
            const float n = MixerScale::norm(d);
            monotone = monotone && n >= last;
            last = n;
        }
        check(monotone, "louder is never lower");
        bool monotoneDb = true;
        float lastDb = -std::numeric_limits<float>::infinity();
        for (float n = 0.0f; n <= 1.0f; n += 0.005f) {
            const float d = MixerScale::db(n);
            monotoneDb = monotoneDb && d >= lastDb;
            lastDb = d;
        }
        check(monotoneDb, "db() never falls as position rises");
        bool roundTrip = true;
        float worst = 0.0f;
        for (float d = -79.0f; d <= 6.0f; d += 0.37f) {
            const float err = std::fabs(MixerScale::db(MixerScale::norm(d)) - d);
            worst = std::max(worst, err);
            roundTrip = roundTrip && err < 0.02f;
        }
        check(roundTrip, "db(norm(x)) == x across the sweep (worst " + std::to_string(worst) + " dB)");
    }

    std::cout << "[5] meter zones from height fractions\n";
    {
        using MeterZones::Zone;
        check(MeterZones::meterZone(0.0f) == Zone::Green, "0 is green");
        check(MeterZones::meterZone(0.699f) == Zone::Green, "0.699 is green");
        check(MeterZones::meterZone(0.70f) == Zone::Amber, "0.70 is amber");
        check(MeterZones::meterZone(0.879f) == Zone::Amber, "0.879 is amber");
        check(MeterZones::meterZone(0.88f) == Zone::Red, "0.88 is red");
        check(MeterZones::meterZone(1.0f) == Zone::Red, "the top is red");
        // On the board scale the boundaries land near -2 dB and +3 dB.
        check(near(MixerScale::db(0.70f), -2.0f, 0.05f), "0.70 of the lane is -2 dB");
        check(near(MixerScale::db(0.88f), 3.12f, 0.05f), "0.88 of the lane is +3.1 dB");
        check(MeterZones::meterZone(MixerScale::norm(-3.0f)) == Zone::Green, "-3 dB is no longer red");
        check(MeterZones::meterZone(MixerScale::norm(-12.0f)) == Zone::Green, "-12 dB is no longer amber");
        check(MeterZones::meterZone(MixerScale::norm(0.0f)) == Zone::Amber, "0 dB is amber");
        check(MeterZones::meterZone(MixerScale::norm(4.0f)) == Zone::Red, "+4 dB is red");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
