// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The EQ's filter building blocks: the biquad it runs and the RBJ/first-order coefficient designs. Split out of
// AestraEQ.h (which is pinned by the file-size ratchet) when the band crossfade landed; unchanged in behaviour.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Aestra {
namespace Audio {
namespace Plugins {

// ============================================================================
// Biquad Filter — Direct Form II Transposed
// ============================================================================

class BiquadFilter {
public:
    BiquadFilter() { reset(); }

    void reset() {
        m_z1 = 0.0f;
        m_z2 = 0.0f;
    }

    void setCoeffs(double b0, double b1, double b2, double a0, double a1, double a2) {
        if (std::abs(a0) < 1.0e-24) {
            m_b0 = 1.0; m_b1 = 0.0; m_b2 = 0.0;
            m_a1 = 0.0; m_a2 = 0.0;
            return;
        }
        const double invA0 = 1.0 / a0;
        m_b0 = b0 * invA0;
        m_b1 = b1 * invA0;
        m_b2 = b2 * invA0;
        m_a1 = a1 * invA0;
        m_a2 = a2 * invA0;

        if (!std::isfinite(m_b0) || !std::isfinite(m_a1) || !std::isfinite(m_a2)) {
            m_b0 = 1.0; m_b1 = 0.0; m_b2 = 0.0;
            m_a1 = 0.0; m_a2 = 0.0;
        }
    }

    float process(float in) {
        if (!std::isfinite(in)) {
            m_z1 = m_z2 = 0.0f;
            return 0.0f;
        }
        const double inSample = static_cast<double>(in);
        const double out = m_b0 * inSample + m_z1;
        m_z1 = m_b1 * inSample - m_a1 * out + m_z2;
        m_z2 = m_b2 * inSample - m_a2 * out;
        if (!std::isfinite(out)) {
            m_z1 = m_z2 = 0.0;
            return 0.0f;
        }
        m_z1 = zapDenormal(m_z1);
        m_z2 = zapDenormal(m_z2);
        return static_cast<float>(zapDenormal(out));
    }

    void process(float* buffer, uint32_t numFrames) {
        const double b0 = m_b0, b1 = m_b1, b2 = m_b2;
        const double a1 = m_a1, a2 = m_a2;
        double z1 = m_z1, z2 = m_z2;
        for (uint32_t i = 0; i < numFrames; ++i) {
            const float in = buffer[i];
            if (!std::isfinite(in)) {
                z1 = z2 = 0.0;
                buffer[i] = 0.0f;
                continue;
            }
            const double inSample = static_cast<double>(in);
            const double out = b0 * inSample + z1;
            z1 = b1 * inSample - a1 * out + z2;
            z2 = b2 * inSample - a2 * out;
            buffer[i] = static_cast<float>(zapDenormal(out));
        }
        m_z1 = zapDenormal(z1);
        m_z2 = zapDenormal(z2);
    }

private:
    static double zapDenormal(double value) {
        return std::abs(value) < 1.0e-24 ? 0.0 : value;
    }

    double m_b0 = 1.0, m_b1 = 0.0, m_b2 = 0.0;
    double m_a1 = 0.0, m_a2 = 0.0;
    double m_z1 = 0.0, m_z2 = 0.0;
};

// ============================================================================
// Filter Type Enum (internal, not all exposed in V1)
// ============================================================================

enum class FilterType : uint32_t {
    Bell = 0,
    LowCut = 1,
    HighCut = 2,
    LowShelf = 3,
    HighShelf = 4,
    Notch = 5,
    BandPass = 6,
    Tilt = 7
};

struct FilterCoeffs {
    double b0, b1, b2, a0, a1, a2;
};

static FilterCoeffs designBiquad(FilterType type, float frequency, float gainDb, float q, float sampleRate) {
    constexpr double pi = 3.14159265358979323846;
    const double w0 = 2.0 * pi * static_cast<double>(frequency) / static_cast<double>(sampleRate);
    const double cos_w0 = std::cos(w0);
    const double sin_w0 = std::sin(w0);
    const double A = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
    const double alpha = sin_w0 / (2.0 * static_cast<double>(q));

    double b0 = 0.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (type) {
    case FilterType::Bell: {
        b0 = 1 + alpha * A;
        b1 = -2 * cos_w0;
        b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;
        a1 = -2 * cos_w0;
        a2 = 1 - alpha / A;
        break;
    }
    case FilterType::LowCut: {
        b0 =  (1 + cos_w0) / 2;
        b1 = -(1 + cos_w0);
        b2 =  (1 + cos_w0) / 2;
        a0 =   1 + alpha;
        a1 =  -2 * cos_w0;
        a2 =   1 - alpha;
        break;
    }
    case FilterType::HighCut: {
        b0 =  (1 - cos_w0) / 2;
        b1 =   1 - cos_w0;
        b2 =  (1 - cos_w0) / 2;
        a0 =   1 + alpha;
        a1 =  -2 * cos_w0;
        a2 =   1 - alpha;
        break;
    }
    case FilterType::LowShelf: {
        b0 = A * ((A + 1) - (A - 1) * cos_w0 + 2 * std::sqrt(A) * alpha);
        b1 = 2 * A * ((A - 1) - (A + 1) * cos_w0);
        b2 = A * ((A + 1) - (A - 1) * cos_w0 - 2 * std::sqrt(A) * alpha);
        a0 = (A + 1) + (A - 1) * cos_w0 + 2 * std::sqrt(A) * alpha;
        a1 = -2 * ((A - 1) + (A + 1) * cos_w0);
        a2 = (A + 1) + (A - 1) * cos_w0 - 2 * std::sqrt(A) * alpha;
        break;
    }
    case FilterType::HighShelf: {
        b0 = A * ((A + 1) + (A - 1) * cos_w0 + 2 * std::sqrt(A) * alpha);
        b1 = -2 * A * ((A - 1) + (A + 1) * cos_w0);
        b2 = A * ((A + 1) + (A - 1) * cos_w0 - 2 * std::sqrt(A) * alpha);
        a0 = (A + 1) - (A - 1) * cos_w0 + 2 * std::sqrt(A) * alpha;
        a1 = 2 * ((A - 1) - (A + 1) * cos_w0);
        a2 = (A + 1) - (A - 1) * cos_w0 - 2 * std::sqrt(A) * alpha;
        break;
    }
    case FilterType::Notch: {
        b0 = 1;
        b1 = -2 * cos_w0;
        b2 = 1;
        a0 = 1 + alpha;
        a1 = -2 * cos_w0;
        a2 = 1 - alpha;
        break;
    }
    case FilterType::BandPass: {
        b0 = alpha;
        b1 = 0;
        b2 = -alpha;
        a0 = 1 + alpha;
        a1 = -2 * cos_w0;
        a2 = 1 - alpha;
        break;
    }
    case FilterType::Tilt:
        // Tilt is implemented as staged low/high shelf filters by AestraEQ.
        b0 = 1;
        b1 = 0;
        b2 = 0;
        a0 = 1;
        a1 = 0;
        a2 = 0;
        break;
    }

    return { b0, b1, b2, a0, a1, a2 };
}

static FilterCoeffs designFirstOrderCut(FilterType type, float frequency, float sampleRate) {
    constexpr double pi = 3.14159265358979323846;
    const double k = std::tan(pi * static_cast<double>(frequency) / static_cast<double>(sampleRate));
    const double norm = 1.0 / std::max(1.0 + k, 1.0e-24);

    if (type == FilterType::LowCut) {
        return {norm, -norm, 0.0, 1.0, (k - 1.0) * norm, 0.0};
    }
    if (type == FilterType::HighCut) {
        return {k * norm, k * norm, 0.0, 1.0, (k - 1.0) * norm, 0.0};
    }
    return {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
}

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
