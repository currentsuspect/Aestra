// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The transport bar's DSP load: how much of each audio callback's time budget the engine used (the last callback's
// time over its buffer's duration). It rises at once, so a spike is seen, and falls at 30 points a second, so the
// number is readable instead of flickering. Amber from 70 %, red from 95 %: past 100 % the callback missed its
// deadline and the audio dropped out.

#include <algorithm>
#include <cstdint>

namespace Aestra {

struct DspLoadReadout {
    enum class Level { Normal, Busy, Overload };

    static constexpr float kFallPerSecond = 30.0f;
    static constexpr float kBusyAt = 70.0f;
    static constexpr float kOverloadAt = 95.0f;

    float shown = 0.0f;

    /// Percent of the budget the last callback used; 0 when either is unknown.
    static float percentOf(uint64_t lastCallbackNs, uint64_t budgetNs) {
        if (budgetNs == 0)
            return 0.0f;
        return std::min(999.0f, 100.0f * static_cast<float>(lastCallbackNs) / static_cast<float>(budgetNs));
    }

    float update(float percent, double dtSeconds) {
        if (percent >= shown) {
            shown = percent;
        } else {
            shown = std::max(percent, shown - static_cast<float>(dtSeconds) * kFallPerSecond);
        }
        return shown;
    }

    static Level levelFor(float percent) {
        return percent >= kOverloadAt ? Level::Overload : percent >= kBusyAt ? Level::Busy : Level::Normal;
    }
};

} // namespace Aestra
