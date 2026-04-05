// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "SampleRateConverter.h"

#include "../../AestraCore/include/AestraLog.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

namespace Aestra {
namespace Audio {

// =============================================================================
// Shared Filter Bank Cache
// =============================================================================

namespace {
struct FilterCache {
    std::mutex mutex;
    std::map<SRCQuality, std::weak_ptr<const PolyphaseFilterBank>> entries;
};

FilterCache& getFilterCache() {
    static FilterCache cache;
    return cache;
}
} // namespace

std::shared_ptr<const PolyphaseFilterBank> SampleRateConverter::getSharedFilterBank(SRCQuality quality) {
    auto& cache = getFilterCache();
    std::lock_guard<std::mutex> lock(cache.mutex);

    auto it = cache.entries.find(quality);
    if (it != cache.entries.end()) {
        if (auto shared = it->second.lock()) {
            return shared;
        }
    }

    // Not in cache or expired, create new
    auto newBank = std::make_shared<PolyphaseFilterBank>();

    // We use a temporary SRC instance to generate the bank with a standard upsampling cutoff (0.98)
    SampleRateConverter temp;
    temp.m_ratio = 1.01; // Fake upsampling ratio to trigger standard cutoff
    temp.generateFilterBank(quality);

    // Copy generated coeffs to the shared bank
    *newBank = temp.m_localFilterBank;

    cache.entries[quality] = newBank;
    return newBank;
}

// =============================================================================
// SIMD Helper Functions
// =============================================================================

namespace {

#ifdef AESTRA_HAS_SSE
// SSE dot product: processes 4 floats at a time
float dotProductSSE(const float* a, const float* b, uint32_t n) noexcept {
    __m128 sum = _mm_setzero_ps();

    // Process 4 elements at a time
    uint32_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_load_ps(b + i); // Aligned load for coefficients
        sum = _mm_add_ps(sum, _mm_mul_ps(va, vb));
    }

    // Horizontal sum: sum[0] + sum[1] + sum[2] + sum[3]
    __m128 shuf = _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(2, 3, 0, 1));
    sum = _mm_add_ps(sum, shuf);
    shuf = _mm_movehl_ps(shuf, sum);
    sum = _mm_add_ss(sum, shuf);
    float result = _mm_cvtss_f32(sum);

    // Handle remaining elements (scalar)
    for (; i < n; ++i) {
        result += a[i] * b[i];
    }

    return result;
}
#endif

#ifdef AESTRA_HAS_AVX
// AVX dot product: processes 8 floats at a time
float dotProductAVX(const float* a, const float* b, uint32_t n) noexcept {
    __m256 sum = _mm256_setzero_ps();

    uint32_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_load_ps(b + i); // Aligned load for coefficients
        sum = _mm256_add_ps(sum, _mm256_mul_ps(va, vb));
    }

    // Reduce 8-wide sum to scalar
    __m128 low = _mm256_castps256_ps128(sum);
    __m128 high = _mm256_extractf128_ps(sum, 1);
    __m128 sum128 = _mm_add_ps(low, high);

    __m128 shuf = _mm_shuffle_ps(sum128, sum128, _MM_SHUFFLE(2, 3, 0, 1));
    sum128 = _mm_add_ps(sum128, shuf);
    shuf = _mm_movehl_ps(shuf, sum128);
    sum128 = _mm_add_ss(sum128, shuf);
    float result = _mm_cvtss_f32(sum128);

    for (; i < n; ++i) {
        result += a[i] * b[i];
    }

    return result;
}
#endif

// Scalar fallback dot product
// Scalar dot product with SSE4.1 target for auto-vectorization
// This enables the compiler to use SSE multiply-add instructions on x86
#ifdef __x86_64__
__attribute__((target("sse4.1")))
#endif
float dotProductScalar(const float* __restrict__ a, const float* __restrict__ b, uint32_t n) noexcept {
    float sum = 0.0f;
    uint32_t i = 0;

    // Unroll by 8 for better instruction-level parallelism
    for (; i + 7 < n; i += 8) {
        sum += a[i] * b[i] + a[i + 1] * b[i + 1] +
               a[i + 2] * b[i + 2] + a[i + 3] * b[i + 3] +
               a[i + 4] * b[i + 4] + a[i + 5] * b[i + 5] +
               a[i + 6] * b[i + 6] + a[i + 7] * b[i + 7];
    }

    // Remainder
    for (; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

} // anonymous namespace

// =============================================================================
// Configuration
// =============================================================================

void SampleRateConverter::configure(uint32_t srcRate, uint32_t dstRate, uint32_t channels, SRCQuality quality) {
    // Validate parameters
    if (srcRate == 0 || dstRate == 0) {
        Log::warning("SampleRateConverter: Invalid sample rate (0)");
        return;
    }
    if (channels == 0 || channels > SRCConstants::MAX_CHANNELS) {
        Log::warning("SampleRateConverter: Invalid channel count: " + std::to_string(channels) +
                     " (max: " + std::to_string(SRCConstants::MAX_CHANNELS) + ")");
        return;
    }

    m_srcRate = srcRate;
    m_dstRate = dstRate;
    m_channels = channels;
    m_quality = quality;
    m_ratio = static_cast<double>(dstRate) / static_cast<double>(srcRate);
    m_currentRatio = m_ratio;
    m_targetRatio = m_ratio;
    m_ratioSmoothFrames = 0;
    m_ratioSmoothTotal = 0;

    // Check for passthrough mode (no conversion needed)
    m_isPassthrough = (srcRate == dstRate);

    // Initialize history buffer
    m_history.init(channels);
    m_historyFilled = 0;
    m_srcPosition = 0.0;
    m_nextOutputSrcPos = 0.0; // [FIX] Init phase

    // Generate/Fetch Filter Bank
    if (!m_isPassthrough) {
        if (m_ratio >= 1.0) {
            // Upsampling: use shared cache (fixed cutoff)
            m_sharedFilterBank = getSharedFilterBank(quality);
        } else {
            // Downsampling: need instance-owned filter with specific cutoff
            m_sharedFilterBank = nullptr;
            generateFilterBank(quality);
        }
    } else {
        m_sharedFilterBank = nullptr;
    }

    m_configured = true;

    Log::info("SampleRateConverter configured: " + std::to_string(srcRate) + " -> " + std::to_string(dstRate) +
              " Hz, " + std::to_string(channels) + " channels, quality=" + std::to_string(static_cast<int>(quality)) +
              (m_isPassthrough ? " (passthrough)" : "") + (hasSIMD() ? " [SIMD]" : " [Scalar]"));
}

void SampleRateConverter::reset() noexcept {
    // Clear history buffer
    for (uint32_t ch = 0; ch < m_channels; ++ch) {
        for (float& s : m_history.data[ch])
            s = 0.0f;
    }
    m_history.writePos = 0;
    m_historyFilled = 0;

    // Reset position accumulator
    m_srcPosition = 0.0;
    m_nextOutputSrcPos = 0.0; // [FIX] Reset phase

    // Reset ratio smoothing
    m_currentRatio = m_ratio;
    m_targetRatio = m_ratio;
    m_ratioSmoothFrames = 0;
    m_ratioSmoothTotal = 0;
}

// =============================================================================
// Filter Generation
// =============================================================================

void SampleRateConverter::generateFilterBank(SRCQuality quality) {
    m_localFilterBank.clear();

    // Determine number of taps based on quality
    uint32_t numTaps = 0;
    double kaiserBeta = SRCConstants::KAISER_BETA_DEFAULT;

    switch (quality) {
    case SRCQuality::Linear:
        numTaps = 2;
        kaiserBeta = 2.0; // Very narrow window
        break;
    case SRCQuality::Cubic:
        numTaps = 4;
        kaiserBeta = 4.0;
        break;
    case SRCQuality::Sinc8:
        numTaps = 8;
        kaiserBeta = 6.0;
        break;
    case SRCQuality::Sinc16:
        numTaps = 16;
        kaiserBeta = 8.0;
        break;
    case SRCQuality::Sinc64:
        numTaps = 64;
        kaiserBeta = 10.0;
        break;
    }

    m_localFilterBank.numTaps = numTaps;
    m_localFilterBank.halfTaps = numTaps / 2;

    const double halfTaps = static_cast<double>(numTaps) / 2.0;

    // Cutoff frequency for anti-aliasing:
    // When downsampling (ratio < 1): limit to output Nyquist = ratio * Nyquist
    // When upsampling (ratio > 1): no limiting needed (already below output Nyquist)
    // Apply 0.95 factor for transition band (prevents ringing at Nyquist)
    double cutoff = 1.0;
    if (m_ratio < 1.0) {
        // Downsampling: need to filter out frequencies above output Nyquist
        cutoff = m_ratio * 0.95;
    } else {
        // Upsampling: full bandwidth, slight rolloff for smoothness
        cutoff = 0.98;
    }

    // Generate coefficients for each fractional phase
    for (uint32_t phase = 0; phase < SRCConstants::POLYPHASE_PHASES; ++phase) {
        // Fractional offset for this phase (0.0 to 1.0)
        const double frac = static_cast<double>(phase) / static_cast<double>(SRCConstants::POLYPHASE_PHASES);

        double sumWeight = 0.0;

        // Compute sinc * window for each tap
        for (uint32_t tap = 0; tap < numTaps; ++tap) {
            // x is the distance from the ideal sample position to this tap
            // Center the filter: tap 0 is at position -halfTaps, tap numTaps-1 is at +halfTaps
            const double x = (static_cast<double>(tap) - halfTaps) - frac;

            // Sinc function with cutoff applied
            double sinc = 1.0;
            if (std::abs(x) > 1e-10) {
                const double pix = SRCConstants::PI * x * cutoff;
                sinc = std::sin(pix) / (SRCConstants::PI * x); // sinc(x*cutoff) = sin(pi*x*cutoff)/(pi*x)
            } else {
                sinc = cutoff; // limx->0 sin(pi*x*c)/(pi*x) = c
            }

            // Apply Kaiser window centered in the tap array
            const double window = kaiserWindow(tap, numTaps, kaiserBeta);

            // Coefficient is sinc * window (cutoff already incorporated)
            const double coeff = sinc * window;
            m_localFilterBank.coeffs[phase][tap] = static_cast<float>(coeff);
            sumWeight += coeff;
        }

        // Normalize coefficients so they sum to 1.0 (unity gain)
        if (sumWeight > 1e-10) {
            const float invSum = static_cast<float>(1.0 / sumWeight);
            for (uint32_t tap = 0; tap < numTaps; ++tap) {
                m_localFilterBank.coeffs[phase][tap] *= invSum;
            }
        }
    }

    Log::info("SampleRateConverter: Generated " + std::to_string(numTaps) + "-tap filter with " +
              std::to_string(SRCConstants::POLYPHASE_PHASES) + " phases, cutoff=" + std::to_string(cutoff));
}

// =============================================================================
// Kaiser Window Helpers
// =============================================================================

double SampleRateConverter::kaiserWindow(double n, double N, double beta) noexcept {
    // Kaiser window: I0(beta * sqrt(1 - ((n - N/2) / (N/2))^2)) / I0(beta)
    const double halfN = (N - 1.0) / 2.0;
    const double ratio = (n - halfN) / halfN;
    const double arg = beta * std::sqrt(std::max(0.0, 1.0 - ratio * ratio));
    return bessel_I0(arg) / bessel_I0(beta);
}

double SampleRateConverter::bessel_I0(double x) noexcept {
    // Modified Bessel function of first kind, order 0
    // Using series expansion (converges quickly for typical beta values)
    double sum = 1.0;
    double term = 1.0;
    const double halfX = x / 2.0;

    for (int k = 1; k < 25; ++k) { // 25 terms is plenty for convergence
        term *= (halfX / static_cast<double>(k));
        term *= (halfX / static_cast<double>(k));
        sum += term;
        if (term < 1e-12 * sum)
            break; // Converged
    }

    return sum;
}

// =============================================================================
// Processing
// =============================================================================

uint32_t SampleRateConverter::process(const float* input, uint32_t inputFrames, float* output,
                                      uint32_t maxOutputFrames) noexcept {
    if (!m_configured || inputFrames == 0 || maxOutputFrames == 0) {
        return 0;
    }

    // Passthrough mode: direct copy
    if (m_isPassthrough && m_currentRatio == 1.0) {
        const uint32_t copyFrames = std::min(inputFrames, maxOutputFrames);
        std::memcpy(output, input, copyFrames * m_channels * sizeof(float));
        return copyFrames;
    }

    const PolyphaseFilterBank* bank = getFilterBank();
    if (!bank)
        return 0;

    const uint32_t numTaps = bank->numTaps;
    const uint32_t halfTaps = bank->halfTaps;
    uint32_t outputFrames = 0;

    const bool useSIMD = m_simdEnabled.load(std::memory_order_relaxed) && hasSIMD();

    // Update ratio smoothing
    if (m_ratioSmoothFrames > 0 && m_currentRatio != m_targetRatio) {
        // Linear interpolation toward target
        double t = 1.0 - (static_cast<double>(m_ratioSmoothFrames) / static_cast<double>(m_ratioSmoothTotal));
        m_currentRatio = m_ratio + t * (m_targetRatio - m_ratio);
        m_ratioSmoothFrames = (m_ratioSmoothFrames > inputFrames) ? (m_ratioSmoothFrames - inputFrames) : 0;
        if (m_ratioSmoothFrames == 0) {
            m_currentRatio = m_targetRatio;
        }
    }

    // Use current (possibly smoothed) ratio
    const double effectiveRatio = m_currentRatio;
    const double invRatio = 1.0 / effectiveRatio; // hoisted: constant for entire process() call

    // Hoisted constants (invariant across all frames in this call)
    const double historySizeD = static_cast<double>(m_history.size);
    const double historySizeMinus1 = historySizeD - 1.0;
    const double historySizeMinusHalfTaps = historySizeD - static_cast<double>(halfTaps);
    const double halfTapsD = static_cast<double>(halfTaps);
    const double polyPhaseScale = static_cast<double>(SRCConstants::POLYPHASE_PHASES);
    const uint32_t historySizeMask = m_history.size - 1;

    // Hoist member variables into locals to reduce memory traffic in the hot loop
    double srcPosition = m_srcPosition;
    double nextOutputSrcPos = m_nextOutputSrcPos;

    // Track srcPosition - nextOutputSrcPos as a single variable.
    // In the output loop, srcPosition is constant and nextOutputSrcPos increases
    // by invRatio each iteration, so srcNextDiff decreases by invRatio.
    double srcNextDiff = srcPosition - nextOutputSrcPos;

    // Process input samples — use incrementing pointer to avoid multiply per frame
    const float* inPtr = input;
    for (uint32_t inFrame = 0; inFrame < inputFrames; ++inFrame) {
        // Inline push for stereo (most common case) — eliminates function call
        if (m_channels == 2) {
            const uint32_t wp = m_history.writePos;
            const uint32_t wp1 = wp + m_history.size;
            const float l = inPtr[0];
            const float r = inPtr[1];
            m_history.data[0][wp] = l;
            m_history.data[0][wp1] = l;
            m_history.data[1][wp] = r;
            m_history.data[1][wp1] = r;
            m_history.writePos = (wp + 1) & (m_history.size - 1);
        } else {
            m_history.push(inPtr);
        }
        inPtr += m_channels;

        // Logical "now" position in source stream (in samples)
        srcPosition += 1.0;
        srcNextDiff = srcPosition - nextOutputSrcPos;

        // Hoist per-frame constants for the output loop
        const double srcPosMinusHalfTaps = srcPosition - halfTapsD;
        const double srcPosMinusHistorySize = srcPosition - historySizeMinusHalfTaps;

        // Track historyPos directly: starts at historySizeMinus1 - srcNextDiff,
        // increases by invRatio each output sample iteration
        double historyPos = historySizeMinus1 - srcNextDiff;

        // Generate output samples while we have enough history
        while (outputFrames < maxOutputFrames) {
            // If the next sample is ahead of our current history window (accounting for filter tail), wait.
            if (nextOutputSrcPos > srcPosMinusHalfTaps) {
                break;
            }

            // Re-check: If next sample is WAY behind (seek?), reset it to a safe "current" position.
            if (nextOutputSrcPos < srcPosMinusHistorySize) {
                nextOutputSrcPos = srcPosMinusHalfTaps;
                srcNextDiff = srcPosition - nextOutputSrcPos;
                historyPos = historySizeMinus1 - srcNextDiff;
            }

            const uint32_t intPos = static_cast<uint32_t>(historyPos);
            const double fracPos = historyPos - static_cast<double>(intPos);

            // Quantize fractional position to polyphase index
            // POLYPHASE_PHASES = 256 (power of 2), so use bitwise AND instead of modulo
            const uint32_t phaseIndex =
                static_cast<uint32_t>(fracPos * polyPhaseScale + 0.5) &
                (SRCConstants::POLYPHASE_PHASES - 1);

            // Hoist channel-invariant computations
            const float* coeffs = bank->coeffs[phaseIndex];
            const int32_t samplePos0 = static_cast<int32_t>(intPos) - static_cast<int32_t>(halfTaps);

            // Inline window access: compute idx once, then use planar data[ch][idx]
            // Casting to uint32_t before & ensures well-defined two's complement wrapping
            const uint32_t windowIdx = m_history.writePos +
                (static_cast<uint32_t>(samplePos0) & historySizeMask);

            // Precompute output base pointer to avoid multiply per channel
            float* out = output + outputFrames * m_channels;

            // Generate output for each channel — hoist SIMD dispatch outside the loop
            // Fast path for stereo (most common case): unroll to 2 independent accumulators
            if (m_channels == 2) {
                const float* windowL = &m_history.data[0][windowIdx];
                const float* windowR = &m_history.data[1][windowIdx];
#ifdef AESTRA_HAS_AVX
                if (useSIMD) {
                    out[0] = dotProductAVX(windowL, coeffs, numTaps);
                    out[1] = dotProductAVX(windowR, coeffs, numTaps);
                } else {
                    out[0] = dotProductScalar(windowL, coeffs, numTaps);
                    out[1] = dotProductScalar(windowR, coeffs, numTaps);
                }
#elif defined(AESTRA_HAS_SSE)
                if (useSIMD) {
                    out[0] = dotProductSSE(windowL, coeffs, numTaps);
                    out[1] = dotProductSSE(windowR, coeffs, numTaps);
                } else {
                    out[0] = dotProductScalar(windowL, coeffs, numTaps);
                    out[1] = dotProductScalar(windowR, coeffs, numTaps);
                }
#else
                (void)useSIMD;
                out[0] = dotProductScalar(windowL, coeffs, numTaps);
                out[1] = dotProductScalar(windowR, coeffs, numTaps);
#endif
            } else {
#ifdef AESTRA_HAS_AVX
                if (useSIMD) {
                    for (uint32_t ch = 0; ch < m_channels; ++ch) {
                        out[ch] = dotProductAVX(&m_history.data[ch][windowIdx], coeffs, numTaps);
                    }
                } else {
                    for (uint32_t ch = 0; ch < m_channels; ++ch) {
                        out[ch] = dotProductScalar(&m_history.data[ch][windowIdx], coeffs, numTaps);
                    }
                }
#elif defined(AESTRA_HAS_SSE)
                if (useSIMD) {
                    for (uint32_t ch = 0; ch < m_channels; ++ch) {
                        out[ch] = dotProductSSE(&m_history.data[ch][windowIdx], coeffs, numTaps);
                    }
                } else {
                    for (uint32_t ch = 0; ch < m_channels; ++ch) {
                        out[ch] = dotProductScalar(&m_history.data[ch][windowIdx], coeffs, numTaps);
                    }
                }
#else
                (void)useSIMD;
                for (uint32_t ch = 0; ch < m_channels; ++ch) {
                    out[ch] = dotProductScalar(&m_history.data[ch][windowIdx], coeffs, numTaps);
                }
#endif
            }

            ++outputFrames;
            nextOutputSrcPos += invRatio;
            srcNextDiff -= invRatio;
            historyPos += invRatio;
        }
    }

    // Write back hoisted locals to member variables
    m_srcPosition = srcPosition;
    m_nextOutputSrcPos = nextOutputSrcPos;

    return outputFrames;
}

float SampleRateConverter::interpolateSample(uint32_t channel, uint32_t phaseIndex, int32_t centerPos) const noexcept {
    const PolyphaseFilterBank* bank = getFilterBank();
    if (!bank)
        return 0.0f; // Should not happen if configured

    const uint32_t numTaps = bank->numTaps;
    const uint32_t halfTaps = bank->halfTaps;

    const int32_t samplePos0 = centerPos - static_cast<int32_t>(halfTaps);
    const float* window = m_history.getWindow(channel, samplePos0);
    const float* coeffs = bank->coeffs[phaseIndex];

#ifdef AESTRA_HAS_AVX
    if (m_simdEnabled.load(std::memory_order_relaxed) && hasSIMD()) {
        return dotProductAVX(window, coeffs, numTaps);
    }
#elif defined(AESTRA_HAS_SSE)
    if (m_simdEnabled.load(std::memory_order_relaxed) && hasSIMD()) {
        return dotProductSSE(window, coeffs, numTaps);
    }
#endif

    return dotProductScalar(window, coeffs, numTaps);
}

// =============================================================================
// Variable Ratio Support
// =============================================================================

void SampleRateConverter::setRatio(double newRatio, uint32_t smoothFrames) noexcept {
    if (newRatio <= 0.0)
        return; // Invalid ratio

    m_targetRatio = newRatio;
    m_ratioSmoothTotal = smoothFrames;
    m_ratioSmoothFrames = smoothFrames;

    // If instant transition requested, apply immediately
    if (smoothFrames == 0) {
        m_currentRatio = newRatio;
    }
}

} // namespace Audio
} // namespace Aestra
