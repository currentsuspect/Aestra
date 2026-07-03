// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Oversampler — per-sample polyphase halfband FIR oversampling for nonlinear DSP.
//
// Designed for plugins that process sample-by-sample (AestraComp, AestraLimit):
// upsample one input sample into 2/4 subsamples, run the nonlinear stage at the
// higher rate, then downsample back. Linear-phase Kaiser-windowed-sinc halfband
// kernels are computed once in prepare() (non-RT); processing does no allocation,
// no locks, and bounded work per sample.
//
// Latency (input-rate samples, integer by construction):
//   Off = 0, oversampled = 30 (47-tap stage at 2fs + 29-tap stage at 4fs; the
//   2x path is padded so 2x and 4x report identical latency).
// Tap counts chosen by measurement: 47 taps at 2fs keeps 18 kHz round-trip
// error below -79 dB (31 taps: -30 dB, audible ~0.3 dB droop).

#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace Aestra {
namespace Audio {
namespace DSP {

/// One 2x halfband stage: polyphase interpolator + decimating FIR sharing the
/// same linear-phase kernel. Mono; instantiate per channel.
class HalfbandStage {
public:
    static constexpr uint32_t kMaxTaps = 47;

    /// Compute the Kaiser-windowed-sinc halfband kernel. Non-RT (called from
    /// plugin initialize/prepare). numTaps must be odd and <= kMaxTaps.
    void design(uint32_t numTaps, double kaiserBeta) {
        if (numTaps > kMaxTaps || (numTaps & 1u) == 0u) {
            numTaps = kMaxTaps;
        }
        m_numTaps = numTaps;
        const int32_t center = static_cast<int32_t>(numTaps - 1) / 2;
        const double i0Beta = besselI0(kaiserBeta);
        for (uint32_t n = 0; n < numTaps; ++n) {
            const int32_t k = static_cast<int32_t>(n) - center;
            const double window =
                besselI0(kaiserBeta * std::sqrt(std::max(0.0, 1.0 - sq((2.0 * n) / (numTaps - 1) - 1.0)))) / i0Beta;
            m_coeffs[n] = static_cast<float>(0.5 * sinc(0.5 * k) * window);
        }
        for (uint32_t n = numTaps; n < kMaxTaps; ++n) {
            m_coeffs[n] = 0.0f;
        }
        reset();
    }

    void reset() {
        m_upHist.fill(0.0f);
        m_downHist.fill(0.0f);
        m_downPos = 0;
    }

    uint32_t numTaps() const { return m_numTaps; }

    /// One input sample -> two output samples at 2x rate.
    /// out[p] = 2 * sum_j h[2j + p] * x[n - j]  (zero-stuff + FIR, gain 2)
    void upsample(float in, float* out2) {
        for (uint32_t j = kUpHistLen - 1; j > 0; --j) {
            m_upHist[j] = m_upHist[j - 1];
        }
        m_upHist[0] = in;

        float even = 0.0f;
        float odd = 0.0f;
        for (uint32_t j = 0; 2 * j < m_numTaps; ++j) {
            even += m_coeffs[2 * j] * m_upHist[j];
        }
        for (uint32_t j = 0; 2 * j + 1 < m_numTaps; ++j) {
            odd += m_coeffs[2 * j + 1] * m_upHist[j];
        }
        out2[0] = 2.0f * even;
        out2[1] = 2.0f * odd;
    }

    /// Two input samples at 2x rate -> one output sample (decimating FIR).
    /// The dot product is taken after the even subsample so the round-trip
    /// group delay stays integer at the input rate: y[n] = sum_k h[k]*x2[2n-k].
    float downsample(const float* in2) {
        pushDown(in2[0]);
        float acc = 0.0f;
        uint32_t idx = m_downPos;
        for (uint32_t k = 0; k < m_numTaps; ++k) {
            acc += m_coeffs[k] * m_downHist[idx];
            idx = (idx + 1u) % kMaxTaps;
        }
        pushDown(in2[1]);
        return acc;
    }

private:
    static constexpr uint32_t kUpHistLen = (kMaxTaps + 1) / 2;

    void pushDown(float value) {
        m_downPos = (m_downPos + kMaxTaps - 1u) % kMaxTaps;
        m_downHist[m_downPos] = value;
    }

    static double sinc(double x) {
        if (std::abs(x) < 1.0e-9) {
            return 1.0;
        }
        const double px = 3.14159265358979323846 * x;
        return std::sin(px) / px;
    }

    static double sq(double x) { return x * x; }

    /// Zeroth-order modified Bessel function (series), for the Kaiser window.
    static double besselI0(double x) {
        double sum = 1.0;
        double term = 1.0;
        const double halfX = x * 0.5;
        for (uint32_t k = 1; k < 32; ++k) {
            term *= (halfX / k) * (halfX / k);
            sum += term;
            if (term < 1.0e-16 * sum) {
                break;
            }
        }
        return sum;
    }

    std::array<float, kMaxTaps> m_coeffs{};
    std::array<float, kUpHistLen> m_upHist{};
    std::array<float, kMaxTaps> m_downHist{};
    uint32_t m_downPos = 0;
    uint32_t m_numTaps = kMaxTaps;
};

/// Mono 1x/2x/4x oversampler built from cascaded halfband stages.
/// The 2x path is padded with extra dry delay so 2x and 4x report the same
/// total latency — switching between oversampled modes never changes plugin
/// latency; only Off <-> On does.
class Oversampler {
public:
    static constexpr uint32_t kStage1Taps = 47; // runs at 2x rate
    static constexpr uint32_t kStage2Taps = 29; // runs at 4x rate
    static constexpr double kKaiserBeta = 8.5;

    // (taps-1) at 2fs halves at fs; (taps-1) at 4fs quarters at fs.
    static constexpr uint32_t kLatency2x = (kStage1Taps - 1) / 2;                         // 23
    static constexpr uint32_t kLatency4x = (kStage1Taps - 1) / 2 + (kStage2Taps - 1) / 4; // 30
    static constexpr uint32_t kReportedLatency = kLatency4x;                              // both OS modes report this

    /// Non-RT. factor must be 1, 2, or 4 (anything else falls back to 1).
    void prepare(uint32_t factor) {
        m_factor = (factor == 2u || factor == 4u) ? factor : 1u;
        if (m_factor >= 2u) {
            m_stage1.design(kStage1Taps, kKaiserBeta);
        }
        if (m_factor == 4u) {
            m_stage2.design(kStage2Taps, kKaiserBeta);
        }
        reset();
    }

    void reset() {
        m_stage1.reset();
        m_stage2.reset();
        m_padHist.fill(0.0f);
        m_padPos = 0;
    }

    uint32_t factor() const { return m_factor; }

    uint32_t latencySamples() const { return m_factor >= 2u ? kReportedLatency : 0u; }

    /// One input sample -> factor() subsamples in out (out must hold >= 4).
    void upsample(float in, float* out) {
        switch (m_factor) {
        case 2u:
            m_stage1.upsample(in, out);
            break;
        case 4u: {
            float mid[2];
            m_stage1.upsample(in, mid);
            m_stage2.upsample(mid[0], out);
            m_stage2.upsample(mid[1], out + 2);
            break;
        }
        default:
            out[0] = in;
            break;
        }
    }

    /// factor() subsamples -> one output sample. The 2x path adds
    /// kLatency4x - kLatency2x samples of pure delay so latencySamples()
    /// matches the reported constant.
    float downsample(const float* in) {
        switch (m_factor) {
        case 2u:
            return pad(m_stage1.downsample(in));
        case 4u: {
            float mid[2];
            mid[0] = m_stage2.downsample(in);
            mid[1] = m_stage2.downsample(in + 2);
            return m_stage1.downsample(mid);
        }
        default:
            return in[0];
        }
    }

private:
    static constexpr uint32_t kPadLen = kLatency4x - kLatency2x; // 7

    float pad(float value) {
        const float delayed = m_padHist[m_padPos];
        m_padHist[m_padPos] = value;
        m_padPos = (m_padPos + 1u) % kPadLen;
        return delayed;
    }

    HalfbandStage m_stage1;
    HalfbandStage m_stage2;
    std::array<float, kPadLen> m_padHist{};
    uint32_t m_padPos = 0;
    uint32_t m_factor = 1;
};

} // namespace DSP
} // namespace Audio
} // namespace Aestra
