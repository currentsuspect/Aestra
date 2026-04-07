// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MasterSafetyLimiter — Soft clipper for the master bus.
// Uses a 3rd-order rational waveshaper: x * (27 + x²) / (27 + 9x²)
// with a hard ceiling at ±1.5.

#pragma once

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

        // Soft clip with hard ceiling
        if (L > kCeiling) { L = 1.0; limited = true; }
        else if (L < -kCeiling) { L = -1.0; limited = true; }
        else if (L > 1.0 || L < -1.0) {
            const double x2 = L * L;
            L = L * (27.0 + x2) / (27.0 + 9.0 * x2);
            limited = true;
        }

        if (R > kCeiling) { R = 1.0; limited = true; }
        else if (R < -kCeiling) { R = -1.0; limited = true; }
        else if (R > 1.0 || R < -1.0) {
            const double x2 = R * R;
            R = R * (27.0 + x2) / (27.0 + 9.0 * x2);
            limited = true;
        }

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
    static constexpr double kCeiling = 1.5;

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
