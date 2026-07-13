// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "ReverbDecayMetrics.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    using Aestra::Audio::Tests::computeReverbDecayMetrics;

    // A flat 64-window render falls by less than 20 dB before it ends. None of
    // the thresholds may be fabricated from the render duration or copied from
    // another threshold.
    const auto truncated = computeReverbDecayMetrics(std::vector<float>(64, 1.0f), 48000.0f);
    require(!truncated.t20Reached, "truncated render falsely completed T20");
    require(!truncated.t40Reached, "truncated render falsely completed T40");
    require(!truncated.t60Reached, "truncated render falsely completed T60");
    require(truncated.t20 == 0.0f && truncated.t40 == 0.0f && truncated.t60 == 0.0f,
            "incomplete thresholds must not contain fabricated times");

    // An impulse-energy sequence reaches every threshold on the first silent
    // window, proving the normal completed-threshold path remains active.
    std::vector<float> completedEnergy(8, 0.0f);
    completedEnergy[0] = 1.0f;
    const auto completed = computeReverbDecayMetrics(completedEnergy, 48000.0f);
    require(completed.t20Reached && completed.t40Reached && completed.t60Reached,
            "completed render did not report reached thresholds");
    const float firstWindowMs = 256.0f / 48000.0f * 1000.0f;
    require(std::abs(completed.t60 - firstWindowMs) < 1.0e-5f,
            "completed threshold time does not match the first silent window");

    if (failures != 0) {
        return 1;
    }
    std::cout << "[PASS] ReverbDecayMetricTest\n";
    return 0;
}
