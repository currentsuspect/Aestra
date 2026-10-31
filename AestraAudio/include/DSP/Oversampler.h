// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

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
//
// WHY THIS FILE LOOKS THE WAY IT DOES (the 4x fir cost)
//
// The 4x path is where a DAW's CPU goes. A naive reading of this class — a
// scalar FIR over a ring buffer, one multiply-accumulate per tap — spends
// ~840 CYCLES per input sample per channel on one plugin, because:
//
//   1. The accumulator is a serial dependency chain. Each add depends on the
//      previous one, so a 47-tap dot product costs 47 x FP-add latency
//      (4 cycles each on this class of core) rather than 47/2 throughput
//      slots. That is the whole cost. The multiply count is not the problem.
//
//   2. The decimator's inner loop advanced its ring index with `% kMaxTaps`,
//      a runtime modulo in the innermost loop.
//
//   3. The interpolator's history was shifted one element at a time on every
//      sample.
//
// The fix is not fewer taps — that would change the transfer function and the
// -79 dB round-trip figure the kernel was chosen for. The fix is to compute
// the SAME kernel, faster:
//
//   * SSE2 (4-wide float) dot products with four independent accumulators, so
//     the dependency chains run in parallel instead of in series.
//   * A power-of-two ring whose second copy makes the FIR window one
//     contiguous run: no modulo, no split loop, no history shift.
//
// SSE2 is part of the x86-64 baseline ABI, so this needs no compiler flag, no
// runtime dispatch and no per-translation-unit targeting — it is available on
// the 4 GB Folio target as-is. A scalar fallback covers every other
// architecture.
//
// WHAT CHANGED NUMERICALLY, EXACTLY
//
// The kernel, the tap set, the tap order and the arithmetic are unchanged.
// Only the SUMMATION ORDER inside each dot product differs: the scalar path
// summed taps strictly left to right into one accumulator, the SIMD path sums
// four interleaved partial sums and adds them at the end. The difference is
// bounded by the float32 rounding of the reassociation (<= 1 ULP of the
// accumulated magnitude in the worst case, far below the -79 dB round-trip
// error the kernel already carries). Tests/AestraAudio/OversamplerParityTest
// pins this against a reference implementation of the original scalar path.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define AESTRA_DSP_HAS_SSE2 1
#include <emmintrin.h>
#endif

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

            // A halfband's even-offset taps are zero by construction. Evaluating
            // sinc() at those offsets leaves sin(pi*m) at ~1e-17 rather than
            // exactly 0, so the float coefficient comes out as a tiny non-zero
            // number and the phase builder below cannot tell it from a real
            // tap -- it kept all 23 terms of the short phase and multiplied 22
            // of them by nothing.
            //
            // Snapping them to an exact zero makes the kernel the halfband it
            // is meant to be and lets the phase builder drop the taps that
            // carry no signal. The kernel changes by ~1e-17 relative, far
            // below one float32 ULP of the output.
            if (k != 0 && (k % 2) == 0) {
                m_coeffs[n] = 0.0f;
                continue;
            }

            const double window =
                besselI0(kaiserBeta * std::sqrt(std::max(0.0, 1.0 - sq((2.0 * n) / (numTaps - 1) - 1.0)))) / i0Beta;
            m_coeffs[n] = static_cast<float>(0.5 * sinc(0.5 * k) * window);
        }
        for (uint32_t n = numTaps; n < kMaxTaps; ++n) {
            m_coeffs[n] = 0.0f;
        }
        buildPhases();
        reset();
    }

    void reset() {
        m_upBuf.fill(0.0f);
        m_downBuf.fill(0.0f);
        m_upWrite = 0;
        m_downWrite = 0;
    }

    uint32_t numTaps() const { return m_numTaps; }

    /// Kernel coefficient n, in the design order (the block oversampler reads
    /// the same taps rather than redesigning them).
    float coeff(uint32_t n) const { return m_coeffs[n]; }

    /// One input sample -> two output samples at 2x rate.
    /// out[p] = 2 * sum_j h[2j + p] * x[n - j]  (zero-stuff + FIR, gain 2)
    void upsample(float in, float* out2) {
        m_upBuf[m_upWrite] = in;
        m_upBuf[m_upWrite + kUpRingSize] = in;
        m_upWrite = (m_upWrite + 1u) & (kUpRingSize - 1u);

        const uint32_t newest = (m_upWrite - 1u) & (kUpRingSize - 1u);
        // The window holds the m_upTerms newest samples, OLDEST FIRST. The
        // second copy of the ring is what makes it contiguous: the newest
        // sample lives at `newest` and also at `newest + kUpRingSize`.
        const float* window = &m_upBuf[newest + kUpRingSize - (m_upTerms - 1u)];

        const float even = dotProduct(m_upPhaseA.data(), window + m_upAOffset, m_upATerms);
        const float odd = dotProduct(m_upPhaseB.data(), window + m_upBOffset, m_upBTerms);
        out2[0] = 2.0f * even;
        out2[1] = 2.0f * odd;
    }

    /// Two input samples at 2x rate -> one output sample (decimating FIR).
    /// The dot product is taken after the even subsample so the round-trip
    /// group delay stays integer at the input rate: y[n] = sum_k h[k]*x2[2n-k].
    float downsample(const float* in2) {
        pushDown(in2[0]);

        const uint32_t newest = (m_downWrite - 1u) & (kDownRingSize - 1u);
        const float* window = &m_downBuf[newest + kDownRingSize - (m_numTaps - 1u)];
        const float acc = dotProduct(m_downKernel.data(), window, m_numTaps);

        pushDown(in2[1]);
        return acc;
    }

private:
    // Ring sizes are powers of two so the wrap is a mask, and each ring is
    // stored TWICE so a FIR window can be read as one contiguous run.
    static constexpr uint32_t kUpRingBits = 5;
    static constexpr uint32_t kUpRingSize = 1u << kUpRingBits;
    static constexpr uint32_t kDownRingBits = 6;
    static constexpr uint32_t kDownRingSize = 1u << kDownRingBits;

    /// Longest phase: the interpolator's even phase has (taps+1)/2 terms.
    static constexpr uint32_t kMaxPhaseTerms = (kMaxTaps + 1u) / 2u;

    /// Phase coefficients, stored in the order the (oldest-first) window is
    /// read. The window is oldest-first while the classic formulation sums
    /// newest-first, so each phase's coefficient run is reversed here, once,
    /// at design time.
    ///
    /// A halfband kernel has an exactly-zero coefficient at every other tap,
    /// so one of the two interpolator phases is a single tap (the centre) and
    /// the other carries all the work. Dropping the exact zeros is free — a
    /// zero coefficient contributes exactly nothing — and it shortens the
    /// cheap phase from 23 taps to 1.
    ///
    /// The surviving taps must stay contiguous in the history for the SIMD dot
    /// product to read them as one run. A halfband always satisfies that; if a
    /// future kernel did not, the phase falls back to the full run.
    void buildPhase(const std::array<float, kMaxPhaseTerms>& newestFirst,
                    uint32_t terms,
                    std::array<float, kMaxPhaseTerms>& out,
                    uint32_t& outTerms,
                    uint32_t& memoryOffset) {
        uint32_t first = terms;
        uint32_t last = 0;
        bool any = false;
        for (uint32_t j = 0; j < terms; ++j) {
            if (newestFirst[j] != 0.0f) {
                if (!any) {
                    first = j;
                    any = true;
                }
                last = j;
            }
        }

        if (!any) {
            out[0] = 0.0f;
            outTerms = 1;
            memoryOffset = m_upTerms - terms;
            return;
        }

        bool contiguous = true;
        for (uint32_t j = first; j <= last; ++j) {
            if (newestFirst[j] == 0.0f) {
                contiguous = false;
                break;
            }
        }

        if (!contiguous) {
            for (uint32_t i = 0; i < terms; ++i) {
                out[i] = newestFirst[terms - 1u - i];
            }
            outTerms = terms;
            memoryOffset = m_upTerms - terms;
            return;
        }

        // Memory order is oldest-first, so the surviving run is reversed.
        for (uint32_t i = 0; i <= last - first; ++i) {
            out[i] = newestFirst[last - i];
        }
        outTerms = last - first + 1u;
        // History offset j (newest-first) is window index m_upTerms-1-j.
        memoryOffset = (m_upTerms - 1u) - last;
    }

    void buildPhases() {
        // Phase A: coeffs[0], coeffs[2], ... against upHist[0], upHist[1], ...
        // Phase B: coeffs[1], coeffs[3], ... against upHist[0], upHist[1], ...
        m_upANewestFirstTerms = 0;
        for (uint32_t j = 0; 2u * j < m_numTaps; ++j) {
            m_upPhaseANewestFirst[m_upANewestFirstTerms++] = m_coeffs[2u * j];
        }
        m_upBNewestFirstTerms = 0;
        for (uint32_t j = 0; 2u * j + 1u < m_numTaps; ++j) {
            m_upPhaseBNewestFirst[m_upBNewestFirstTerms++] = m_coeffs[2u * j + 1u];
        }
        m_upTerms = std::max(m_upANewestFirstTerms, m_upBNewestFirstTerms);

        buildPhase(m_upPhaseANewestFirst, m_upANewestFirstTerms, m_upPhaseA, m_upATerms, m_upAOffset);
        buildPhase(m_upPhaseBNewestFirst, m_upBNewestFirstTerms, m_upPhaseB, m_upBTerms, m_upBOffset);

        // The decimator sums the kernel against the newest-first history; the
        // window is oldest-first, so the kernel is reversed to match. The
        // decimator keeps every tap: its zeros sit at every other tap, so
        // dropping them would break the contiguous run the SIMD path reads.
        for (uint32_t i = 0; i < m_numTaps; ++i) {
            m_downKernel[i] = m_coeffs[m_numTaps - 1u - i];
        }
    }

    void pushDown(float value) {
        m_downBuf[m_downWrite] = value;
        m_downBuf[m_downWrite + kDownRingSize] = value;
        m_downWrite = (m_downWrite + 1u) & (kDownRingSize - 1u);
    }

    /// Sum of a[i] * b[i] over n terms.
    ///
    /// Four independent accumulators: the point of the vector path is that the
    /// partial sums do not wait on each other. The scalar fallback keeps the
    /// original single-accumulator order.
    ///
    /// The templated form exists because the loop control was costing as much
    /// as the arithmetic: nine dot products per input sample, each paying for
    /// a runtime trip count. With the count known at compile time the loop
    /// unrolls into straight-line code with immediate offsets.
    template <uint32_t N>
    static inline float dotProductFixed(const float* a, const float* b) {
#if defined(AESTRA_DSP_HAS_SSE2)
        __m128 acc0 = _mm_setzero_ps();
        __m128 acc1 = _mm_setzero_ps();
        __m128 acc2 = _mm_setzero_ps();
        __m128 acc3 = _mm_setzero_ps();

        uint32_t i = 0;
        for (; i + 16u <= N; i += 16u) {
            acc0 = _mm_add_ps(acc0, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
            acc1 = _mm_add_ps(acc1, _mm_mul_ps(_mm_loadu_ps(a + i + 4), _mm_loadu_ps(b + i + 4)));
            acc2 = _mm_add_ps(acc2, _mm_mul_ps(_mm_loadu_ps(a + i + 8), _mm_loadu_ps(b + i + 8)));
            acc3 = _mm_add_ps(acc3, _mm_mul_ps(_mm_loadu_ps(a + i + 12), _mm_loadu_ps(b + i + 12)));
        }
        for (; i + 4u <= N; i += 4u) {
            acc0 = _mm_add_ps(acc0, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
        }

        __m128 sum = _mm_add_ps(_mm_add_ps(acc0, acc1), _mm_add_ps(acc2, acc3));
        sum = _mm_add_ps(sum, _mm_movehl_ps(sum, sum));
        sum = _mm_add_ss(sum, _mm_shuffle_ps(sum, sum, 0x55));
        float result = _mm_cvtss_f32(sum);

        for (; i < N; ++i) {
            result += a[i] * b[i];
        }
        return result;
#else
        float acc = 0.0f;
        for (uint32_t i = 0; i < N; ++i) {
            acc += a[i] * b[i];
        }
        return acc;
#endif
    }

    static inline float dotProductGeneric(const float* a, const float* b, uint32_t n) {
#if defined(AESTRA_DSP_HAS_SSE2)
        __m128 acc0 = _mm_setzero_ps();
        __m128 acc1 = _mm_setzero_ps();
        __m128 acc2 = _mm_setzero_ps();
        __m128 acc3 = _mm_setzero_ps();

        uint32_t i = 0;
        for (; i + 16u <= n; i += 16u) {
            acc0 = _mm_add_ps(acc0, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
            acc1 = _mm_add_ps(acc1, _mm_mul_ps(_mm_loadu_ps(a + i + 4), _mm_loadu_ps(b + i + 4)));
            acc2 = _mm_add_ps(acc2, _mm_mul_ps(_mm_loadu_ps(a + i + 8), _mm_loadu_ps(b + i + 8)));
            acc3 = _mm_add_ps(acc3, _mm_mul_ps(_mm_loadu_ps(a + i + 12), _mm_loadu_ps(b + i + 12)));
        }
        for (; i + 4u <= n; i += 4u) {
            acc0 = _mm_add_ps(acc0, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
        }

        __m128 sum = _mm_add_ps(_mm_add_ps(acc0, acc1), _mm_add_ps(acc2, acc3));
        sum = _mm_add_ps(sum, _mm_movehl_ps(sum, sum));
        sum = _mm_add_ss(sum, _mm_shuffle_ps(sum, sum, 0x55));
        float result = _mm_cvtss_f32(sum);

        for (; i < n; ++i) {
            result += a[i] * b[i];
        }
        return result;
#else
        float acc = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            acc += a[i] * b[i];
        }
        return acc;
#endif
    }

    static inline float dotProduct(const float* a, const float* b, uint32_t n) {
        // The counts the two stages actually use. Everything else falls back.
        switch (n) {
        case 47:
            return dotProductFixed<47>(a, b);
        case 29:
            return dotProductFixed<29>(a, b);
        case 24:
            return dotProductFixed<24>(a, b);
        case 23:
            return dotProductFixed<23>(a, b);
        case 15:
            return dotProductFixed<15>(a, b);
        case 14:
            return dotProductFixed<14>(a, b);
        case 1:
            return a[0] * b[0];
        default:
            return dotProductGeneric(a, b, n);
        }
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

    // Phase coefficients, window order (oldest first).
    std::array<float, kMaxPhaseTerms> m_upPhaseA{};
    std::array<float, kMaxPhaseTerms> m_upPhaseB{};
    std::array<float, kMaxPhaseTerms> m_upPhaseANewestFirst{};
    std::array<float, kMaxPhaseTerms> m_upPhaseBNewestFirst{};
    std::array<float, kMaxTaps> m_downKernel{};

    std::array<float, 2u * kUpRingSize> m_upBuf{};
    std::array<float, 2u * kDownRingSize> m_downBuf{};
    uint32_t m_upWrite = 0;
    uint32_t m_downWrite = 0;
    uint32_t m_upTerms = 0;
    uint32_t m_upATerms = 0;
    uint32_t m_upBTerms = 0;
    uint32_t m_upAOffset = 0;
    uint32_t m_upBOffset = 0;
    uint32_t m_upANewestFirstTerms = 0;
    uint32_t m_upBNewestFirstTerms = 0;
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

    /// Non-RT: design both stage kernels (factor-independent). Call once from
    /// the owner's initialize/activate; after that setFactor() is enough.
    void prepareKernels() {
        m_stage1.design(kStage1Taps, kKaiserBeta);
        m_stage2.design(kStage2Taps, kKaiserBeta);
        m_kernelsReady = true;
        reset();
    }

    /// RT-safe factor switch: selects 1/2/4 (anything else falls back to 1)
    /// and resets filter state. Requires prepareKernels() to have run.
    void setFactor(uint32_t factor) {
        m_factor = (m_kernelsReady && (factor == 2u || factor == 4u)) ? factor : 1u;
        reset();
    }

    /// Non-RT convenience: design kernels and select the factor in one call.
    void prepare(uint32_t factor) {
        prepareKernels();
        setFactor(factor);
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
    bool m_kernelsReady = false;
};

} // namespace DSP
} // namespace Audio
} // namespace Aestra
