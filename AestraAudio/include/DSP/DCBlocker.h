#pragma once

// DCBlocker — one-pole DC-blocking high-pass filter (double precision).
//
//   y[n] = x[n] - x[n-1] + R * y[n-1]
//
// R sits just below 1.0, giving a very low corner frequency: the filter removes
// a constant (DC) offset and sub-sonic drift while leaving the audible band
// essentially untouched. State is per-instance, so use one per channel.
//
// Real-time safe: no allocation, no locks, no branches in process().

namespace Aestra {
namespace Audio {

struct DCBlocker {
    double x1{0.0};
    double y1{0.0};
    static constexpr double R = 0.9997;

    inline double process(double x) {
        double y = x - x1 + (R * y1);
        x1 = x;
        y1 = y;
        return y;
    }

    inline void reset() {
        x1 = 0.0;
        y1 = 0.0;
    }
};

} // namespace Audio
} // namespace Aestra
