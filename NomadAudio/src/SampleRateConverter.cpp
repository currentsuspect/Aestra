// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "SampleRateConverter.h"
#include "NomadLog.h"

#include <algorithm>
#include <cstring>

namespace Nomad {
namespace Audio {

// =============================================================================
// SIMD Helper Functions
// =============================================================================

namespace {

#ifdef NOMAD_HAS_SSE
// SSE dot product: processes 4 floats at a time
float dotProductSSE(const float* a, const float* b, uint32_t n) noexcept {
    __m128 sum = _mm_setzero_ps();
    
    // Process 4 elements at a time
    uint32_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_load_ps(b + i);  // Aligned load for coefficients
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

#ifdef NOMAD_HAS_AVX
// AVX dot product: processes 8 floats at a time
float dotProductAVX(const float* a, const float* b, uint32_t n) noexcept {
    __m256 sum = _mm256_setzero_ps();

    uint32_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_load_ps(b + i);  // Aligned load for coefficients
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
float dotProductScalar(const float* a, const float* b, uint32_t n) noexcept {
    float sum = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

} // anonymous namespace

// =============================================================================
// Configuration
// =============================================================================

void SampleRateConverter::configure(uint32_t srcRate, uint32_t dstRate,
                                    uint32_t channels, SRCQuality quality) {
    // Validate parameters
    if (srcRate == 0 || dstRate == 0) {
        Log::warning("SampleRateConverter: Invalid sample rate (0)");
        return;
    }
    if (channels == 0 || channels > SRCConstants::MAX_CHANNELS) {
        Log::warning("SampleRateConverter: Invalid channel count: " + 
                    std::to_string(channels) + " (max: " + 
                    std::to_string(SRCConstants::MAX_CHANNELS) + ")");
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
    
    // Generate filter coefficients
    if (!m_isPassthrough) {
        generateFilterBank(quality);
    }
    
    m_configured = true;
    
    Log::info("SampleRateConverter configured: " + std::to_string(srcRate) + 
              " -> " + std::to_string(dstRate) + " Hz, " +
              std::to_string(channels) + " channels, quality=" + 
              std::to_string(static_cast<int>(quality)) +
              (m_isPassthrough ? " (passthrough)" : "") +
              (hasSIMD() ? " [SIMD]" : " [Scalar]"));
}

void SampleRateConverter::reset() noexcept {
    // Clear history buffer
    for (auto& chBuf : m_history.data) {
        for (float& s : chBuf) s = 0.0f;
    }
    m_history.writePos = 0;
    m_historyFilled = 0;
    
    // Reset position accumulator
    m_srcPosition = 0.0;
    
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
    m_filterBank.clear();
    
    // Determine number of taps based on quality
    uint32_t numTaps = 0;
    double kaiserBeta = SRCConstants::KAISER_BETA_DEFAULT;
    
    switch (quality) {
        case SRCQuality::Linear:
            numTaps = 2;
            kaiserBeta = 2.0;  // Very narrow window
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
    
    m_filterBank.numTaps = numTaps;
    m_filterBank.halfTaps = numTaps / 2;
    
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
        const double frac = static_cast<double>(phase) / 
                           static_cast<double>(SRCConstants::POLYPHASE_PHASES);
        
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
                sinc = std::sin(pix) / (SRCConstants::PI * x);  // sinc(x*cutoff) = sin(pi*x*cutoff)/(pi*x)
            } else {
                sinc = cutoff;  // limx->0 sin(pi*x*c)/(pi*x) = c
            }
            
            // Apply Kaiser window centered in the tap array
            const double window = kaiserWindow(tap, numTaps, kaiserBeta);
            
            // Coefficient is sinc * window (cutoff already incorporated)
            const double coeff = sinc * window;
            m_filterBank.coeffs[phase][tap] = static_cast<float>(coeff);
            sumWeight += coeff;
        }
        
        // Normalize coefficients so they sum to 1.0 (unity gain)
        if (sumWeight > 1e-10) {
            const float invSum = static_cast<float>(1.0 / sumWeight);
            for (uint32_t tap = 0; tap < numTaps; ++tap) {
                m_filterBank.coeffs[phase][tap] *= invSum;
            }
        }
    }
    
    Log::info("SampleRateConverter: Generated " + std::to_string(numTaps) + 
              "-tap filter with " + std::to_string(SRCConstants::POLYPHASE_PHASES) + 
              " phases, cutoff=" + std::to_string(cutoff));
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
    
    for (int k = 1; k < 25; ++k) {  // 25 terms is plenty for convergence
        term *= (halfX / static_cast<double>(k));
        term *= (halfX / static_cast<double>(k));
        sum += term;
        if (term < 1e-12 * sum) break;  // Converged
    }
    
    return sum;
}

// =============================================================================
// Processing
// =============================================================================

uint32_t SampleRateConverter::process(const float* input, uint32_t inputFrames,
                                      float* output, uint32_t maxOutputFrames) noexcept {
    if (!m_configured || inputFrames == 0 || maxOutputFrames == 0) {
        return 0;
    }
    
    // Passthrough mode: direct copy
    if (m_isPassthrough && m_currentRatio == 1.0) {
        const uint32_t copyFrames = std::min(inputFrames, maxOutputFrames);
        std::memcpy(output, input, copyFrames * m_channels * sizeof(float));
        return copyFrames;
    }
    
    const uint32_t numTaps = m_filterBank.numTaps;
    const uint32_t halfTaps = m_filterBank.halfTaps;
    uint32_t outputFrames = 0;

    const bool useSIMD = m_simdEnabled.load(std::memory_order_relaxed) && hasSIMD();
    
    // Update ratio smoothing
    if (m_ratioSmoothFrames > 0 && m_currentRatio != m_targetRatio) {
        // Linear interpolation toward target
        double t = 1.0 - (static_cast<double>(m_ratioSmoothFrames) / 
                         static_cast<double>(m_ratioSmoothTotal));
        m_currentRatio = m_ratio + t * (m_targetRatio - m_ratio);
        m_ratioSmoothFrames = (m_ratioSmoothFrames > inputFrames) ? 
                              (m_ratioSmoothFrames - inputFrames) : 0;
        if (m_ratioSmoothFrames == 0) {
            m_currentRatio = m_targetRatio;
        }
    }
    
    // Use current (possibly smoothed) ratio
    const double effectiveRatio = m_currentRatio;
    
    // Process input samples
    for (uint32_t inFrame = 0; inFrame < inputFrames; ++inFrame) {
        // Push input frame to history
        m_history.push(input + inFrame * m_channels);
        m_historyFilled = std::min(m_historyFilled + 1, m_history.size);
        
        // Generate output samples while we have enough history
        // srcPosition tracks where we are in the input stream
        // Each output sample is at dstPosition = outputFrames
        // The corresponding srcPosition = outputFrames / ratio
        
        while (outputFrames < maxOutputFrames) {
            // What input position does this output sample correspond to?
            const double targetSrcPos = static_cast<double>(outputFrames) / effectiveRatio;
            
            // How far are we from that position?
            // We've consumed (inFrame + 1) input frames so far
            const double currentSrcPos = static_cast<double>(inFrame + 1);
            
            // If we need more input to produce this output, break
            if (targetSrcPos > currentSrcPos - static_cast<double>(halfTaps)) {
                break;
            }
            
            // Calculate fractional position within history
            const double historyPos = static_cast<double>(m_history.size - 1) - 
                                     (currentSrcPos - targetSrcPos);
            
            const uint32_t intPos = static_cast<uint32_t>(historyPos);
            const double fracPos = historyPos - static_cast<double>(intPos);
            
            // Quantize fractional position to polyphase index
            const uint32_t phaseIndex = static_cast<uint32_t>(
                fracPos * static_cast<double>(SRCConstants::POLYPHASE_PHASES)
            ) % SRCConstants::POLYPHASE_PHASES;
            
            // Generate output for each channel
            for (uint32_t ch = 0; ch < m_channels; ++ch) {
                const float* coeffs = m_filterBank.coeffs[phaseIndex];
                const int32_t samplePos0 = static_cast<int32_t>(intPos) - static_cast<int32_t>(halfTaps);
                const float* window = m_history.getWindow(ch, samplePos0);
                float sum = 0.0f;

#ifdef NOMAD_HAS_AVX
                if (useSIMD) {
                    sum = dotProductAVX(window, coeffs, numTaps);
                } else {
                    sum = dotProductScalar(window, coeffs, numTaps);
                }
#elif defined(NOMAD_HAS_SSE)
                if (useSIMD) {
                    sum = dotProductSSE(window, coeffs, numTaps);
                } else {
                    sum = dotProductScalar(window, coeffs, numTaps);
                }
#else
                (void)useSIMD;
                sum = dotProductScalar(window, coeffs, numTaps);
#endif

                output[outputFrames * m_channels + ch] = sum;
            }
            
            ++outputFrames;
        }
    }
    
    return outputFrames;
}

float SampleRateConverter::interpolateSample(uint32_t channel, uint32_t phaseIndex,
                                             int32_t centerPos) const noexcept {
    const float* coeffs = m_filterBank.coeffs[phaseIndex];
    const uint32_t numTaps = m_filterBank.numTaps;
    const uint32_t halfTaps = m_filterBank.halfTaps;

    const int32_t samplePos0 = centerPos - static_cast<int32_t>(halfTaps);
    const float* window = m_history.getWindow(channel, samplePos0);

#ifdef NOMAD_HAS_AVX
    if (m_simdEnabled.load(std::memory_order_relaxed) && hasSIMD()) {
        return dotProductAVX(window, coeffs, numTaps);
    }
#elif defined(NOMAD_HAS_SSE)
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
    if (newRatio <= 0.0) return;  // Invalid ratio
    
    m_targetRatio = newRatio;
    m_ratioSmoothTotal = smoothFrames;
    m_ratioSmoothFrames = smoothFrames;
    
    // If instant transition requested, apply immediately
    if (smoothFrames == 0) {
        m_currentRatio = newRatio;
    }
}

} // namespace Audio
} // namespace Nomad
