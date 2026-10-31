// © 2026 Aestra Studios — All Rights Reserved.
// Block-at-a-time 4x oversampler: the same kernels as DSP::Oversampler, laid out so the
// compiler can vectorise across samples instead of across taps.
#pragma once

#include "DSP/Oversampler.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace Aestra {
namespace Audio {
namespace DSP {

/// One halfband stage that works on a block.
///
/// DSP::HalfbandStage is a per-sample FIR: every sample pays a dot product, a
/// horizontal reduction and a store-then-load through its history ring, and the
/// cost is that fixed latency rather than the multiplies (dropping the zero taps
/// from the decimator measured no faster). Here each non-zero tap is applied to
/// the WHOLE block as an axpy -- y[i] += c * x[i - d] -- which needs no
/// reduction and vectorises across i.
///
/// The kernel is HalfbandStage's, tap for tap; only the order of the sums
/// differs, so the output matches the per-sample stage to float rounding.
class HalfbandBlockStage {
public:
    /// Most samples one upsample call may take, or one downsample call return.
    static constexpr uint32_t kMaxBlock = 256;

    void design(uint32_t numTaps, double kaiserBeta) {
        HalfbandStage kernel;
        kernel.design(numTaps, kaiserBeta);
        m_numTaps = kernel.numTaps();
        m_evenTaps = 0;
        m_oddTaps = 0;
        // Exactly-zero coefficients contribute exactly nothing; keep only the
        // taps that do work, split by the parity of their offset.
        for (uint32_t d = 0; d < m_numTaps; ++d) {
            const float c = kernel.coeff(d);
            if (c == 0.0f) {
                continue;
            }
            if ((d & 1u) == 0u) {
                m_even[m_evenTaps++] = Tap{d / 2u, c};
            } else {
                m_odd[m_oddTaps++] = Tap{(d + 1u) / 2u, c};
            }
        }
        reset();
    }

    void reset() {
        m_upHist.fill(0.0f);
        m_downHistEven.fill(0.0f);
        m_downHistOdd.fill(0.0f);
    }

    /// n input samples -> 2n output samples at twice the rate, interleaved
    /// (out[2i], out[2i+1]) exactly as HalfbandStage::upsample returns them.
    void upsample(const float* in, float* out, uint32_t n) {
        float window[kHist + kMaxBlock];
        std::memcpy(window, m_upHist.data(), kHist * sizeof(float));
        std::memcpy(window + kHist, in, n * sizeof(float));

        // out[2i]   = 2 * sum_j h[2j]   * x[i - j]
        // out[2i+1] = 2 * sum_j h[2j+1] * x[i - j]
        float even[kMaxBlock];
        float odd[kMaxBlock];
        std::memset(even, 0, n * sizeof(float));
        std::memset(odd, 0, n * sizeof(float));
        for (uint32_t t = 0; t < m_evenTaps; ++t) {
            axpy(even, window + kHist - m_even[t].offset, m_even[t].coeff, n);
        }
        for (uint32_t t = 0; t < m_oddTaps; ++t) {
            // Odd tap d = 2j + 1 reads x[i - j]; stored offset is (d + 1) / 2 = j + 1.
            axpy(odd, window + kHist - (m_odd[t].offset - 1u), m_odd[t].coeff, n);
        }
        for (uint32_t i = 0; i < n; ++i) {
            out[2u * i] = 2.0f * even[i];
            out[2u * i + 1u] = 2.0f * odd[i];
        }

        std::memcpy(m_upHist.data(), window + n, kHist * sizeof(float));
    }

    /// 2n input samples at twice the rate -> n output samples:
    /// y[k] = sum_d h[d] * x[2k - d], the decimating FIR HalfbandStage::downsample runs.
    void downsample(const float* in, float* out, uint32_t n) {
        float evenX[kHist + kMaxBlock];
        float oddX[kHist + kMaxBlock];
        std::memcpy(evenX, m_downHistEven.data(), kHist * sizeof(float));
        std::memcpy(oddX, m_downHistOdd.data(), kHist * sizeof(float));
        for (uint32_t i = 0; i < n; ++i) {
            evenX[kHist + i] = in[2u * i];
            oddX[kHist + i] = in[2u * i + 1u];
        }

        std::memset(out, 0, n * sizeof(float));
        for (uint32_t t = 0; t < m_evenTaps; ++t) {
            // d = 2j reads x[2k - 2j], the (k - j)th even-indexed sample.
            axpy(out, evenX + kHist - m_even[t].offset, m_even[t].coeff, n);
        }
        for (uint32_t t = 0; t < m_oddTaps; ++t) {
            // d = 2j + 1 reads x[2k - 2j - 1], the (k - j - 1)th odd-indexed sample.
            axpy(out, oddX + kHist - m_odd[t].offset, m_odd[t].coeff, n);
        }

        std::memcpy(m_downHistEven.data(), evenX + n, kHist * sizeof(float));
        std::memcpy(m_downHistOdd.data(), oddX + n, kHist * sizeof(float));
    }

private:
    // Longest history any tap reaches back: offset (47 + 1) / 2 = 24.
    static constexpr uint32_t kHist = 32;

    struct Tap {
        uint32_t offset = 0;
        float coeff = 0.0f;
    };

    static inline void axpy(float* __restrict dst, const float* __restrict src, float c, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) {
            dst[i] += c * src[i];
        }
    }

    std::array<Tap, HalfbandStage::kMaxTaps> m_even{};
    std::array<Tap, HalfbandStage::kMaxTaps> m_odd{};
    uint32_t m_evenTaps = 0;
    uint32_t m_oddTaps = 0;
    uint32_t m_numTaps = 0;

    std::array<float, kHist> m_upHist{};
    std::array<float, kHist> m_downHistEven{};
    std::array<float, kHist> m_downHistOdd{};
};

/// 4x oversampling of a block: DSP::Oversampler's cascade (47-tap stage at 2x,
/// 29-tap stage at 4x), same kernels, same latency (kReportedLatency).
///
/// Use it where the nonlinear stage is memoryless and can run over a whole
/// block between upsample and downsample. Keep DSP::Oversampler where a
/// per-sample recursion (a detector, a feedback loop) lives in the oversampled
/// domain.
class BlockOversampler4x {
public:
    /// Most base-rate frames per call.
    static constexpr uint32_t kMaxFrames = HalfbandBlockStage::kMaxBlock / 2u;
    static constexpr uint32_t kFactor = 4;
    static constexpr uint32_t kReportedLatency = Oversampler::kReportedLatency;

    void prepare() {
        m_stage1.design(Oversampler::kStage1Taps, Oversampler::kKaiserBeta);
        m_stage2.design(Oversampler::kStage2Taps, Oversampler::kKaiserBeta);
    }

    void reset() {
        m_stage1.reset();
        m_stage2.reset();
    }

    /// n frames in -> 4n oversampled samples out, in the order
    /// Oversampler::upsample writes them.
    void upsample(const float* in, float* os, uint32_t n) {
        float mid[2u * kMaxFrames];
        m_stage1.upsample(in, mid, n);
        m_stage2.upsample(mid, os, 2u * n);
    }

    /// 4n oversampled samples in -> n frames out. `os` is read, not modified.
    void downsample(const float* os, float* out, uint32_t n) {
        float mid[2u * kMaxFrames];
        m_stage2.downsample(os, mid, 2u * n);
        m_stage1.downsample(mid, out, n);
    }

private:
    HalfbandBlockStage m_stage1;
    HalfbandBlockStage m_stage2;
};

} // namespace DSP
} // namespace Audio
} // namespace Aestra
