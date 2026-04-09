// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MasterSafetyLimiter — final output safety limiter for the master bus.
// Uses a light soft-knee region ahead of a hard safety ceiling so accidental
// spikes get caught before they turn into speaker- or ear-hostile blasts.

#pragma once

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

class MasterSafetyLimiter {
public:
    MasterSafetyLimiter() = default;

    // Process a stereo frame in-place. Returns true if limiting occurred.
    bool process(double& L, double& R) {
        bool limited = false;

        // DC blocking
        {
            double y = L - m_dcBlockerL.x1 + m_dcCoeff * m_dcBlockerL.y1;
            m_dcBlockerL.x1 = L;
            m_dcBlockerL.y1 = y;
            L = y;
        }
        {
            double y = R - m_dcBlockerR.x1 + m_dcCoeff * m_dcBlockerR.y1;
            m_dcBlockerR.x1 = R;
            m_dcBlockerR.y1 = y;
            R = y;
        }

        L = limitSample(L, limited);
        R = limitSample(R, limited);

        return limited;
    }

    void reset() {
        m_dcBlockerL = {};
        m_dcBlockerR = {};
    }

    void setDcCoeff(double coeff) { m_dcCoeff = coeff; }
    double getDcCoeff() const { return m_dcCoeff; }

    // Default DC blocker coefficient (~30Hz cutoff at 48kHz)
    static constexpr double kDefaultDcCoeff = 0.997;
    static constexpr double kSoftKneeStart = 0.85;
    static constexpr double kOutputCeiling = 0.95;
    static constexpr double kHardClamp = 1.25;

    static double limitSample(double x, bool& limited) {
        if (x > kHardClamp) {
            limited = true;
            return kOutputCeiling;
        }
        if (x < -kHardClamp) {
            limited = true;
            return -kOutputCeiling;
        }

        const double ax = std::abs(x);
        if (ax <= kSoftKneeStart) {
            return x;
        }

        limited = true;
        const double sign = (x >= 0.0) ? 1.0 : -1.0;
        const double t = std::clamp((ax - kSoftKneeStart) / (kHardClamp - kSoftKneeStart), 0.0, 1.0);
        const double shaped = kSoftKneeStart + (kOutputCeiling - kSoftKneeStart) * (1.0 - std::exp(-4.0 * t));
        return sign * std::min(shaped, kOutputCeiling);
    }

private:
    struct DCBlocker {
        double x1 = 0.0;
        double y1 = 0.0;
    };

    DCBlocker m_dcBlockerL;
    DCBlocker m_dcBlockerR;
    double m_dcCoeff = kDefaultDcCoeff;
};

} // namespace Audio
} // namespace Aestra
