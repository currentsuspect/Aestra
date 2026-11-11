// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The transport bar's DSP load readout: percent of the callback budget, rising at once and falling slowly, with the
// amber and red thresholds a producer reads it by.

#include "Panels/DspLoadReadout.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using Aestra::DspLoadReadout;

int main() {
    // 2.9 ms of work in a 5.33 ms (256 @ 48 kHz) callback is 54 %.
    const float p = DspLoadReadout::percentOf(2'900'000, 5'333'333);
    assert(std::fabs(p - 54.4f) < 0.2f);
    assert(DspLoadReadout::percentOf(1, 0) == 0.0f && "an unknown budget reads 0, not infinity");
    assert(DspLoadReadout::percentOf(100'000'000, 1'000'000) == 999.0f && "clamped for display");
    std::printf("[PASS] percent of the callback budget\n");

    DspLoadReadout r;
    assert(r.update(80.0f, 0.016) == 80.0f && "a spike shows at once");
    const float after = r.update(20.0f, 1.0); // one second later, the load fell to 20
    assert(std::fabs(after - 50.0f) < 0.01f && "falls 30 points a second, readable");
    assert(r.update(20.0f, 2.0) == 20.0f && "and settles on the real value");
    std::printf("[PASS] rises at once, falls at 30 points a second\n");

    assert(DspLoadReadout::levelFor(69.9f) == DspLoadReadout::Level::Normal);
    assert(DspLoadReadout::levelFor(70.0f) == DspLoadReadout::Level::Busy);
    assert(DspLoadReadout::levelFor(95.0f) == DspLoadReadout::Level::Overload);
    std::printf("[PASS] amber from 70 %%, red from 95 %%\n");
    return 0;
}
