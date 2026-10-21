// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "RealtimeThreadGuard.h" // AESTRA_RT_NONBLOCKING

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

/**
 * @brief ITU-R BS.1770-4 inspired true peak meter.
 *
 * Measures intersample peaks by reconstructing the band-limited signal at
 * 4x the input sample rate using a Kaiser-windowed sinc polyphase FIR
 * upsampler (12 taps/phase × 4 phases = 48 prototype taps).
 *
 * Annex 2 of BS.1770-4 specifies a 4x oversampling FIR with stopband
 * rejection ≥ 60 dB (we target ≥ 80 dB by using Kaiser β ≈ 9.4). Per the
 * standard the *audio* signal is unchanged; only the meter sees the
 * oversampled stream.
 *
 * Threading:
 *   - initialize()/reset(): main thread only (regenerates coefficients,
 *     zeroes history). NOT RT-safe.
 *   - process()/getters: RT-safe. Zero allocation, no locks, deterministic.
 *
 * The meter holds running peaks (sample peak + true peak) since the last
 * call to reset(). Peaks are non-negative absolute values; convenience
 * getters return dBTP (clamped to -200 dBTP for silence).
 */
class TruePeakMeter {
public:
    static constexpr uint32_t kOversampleFactor = 4;
    static constexpr uint32_t kTapsPerPhase = 12;
    static constexpr uint32_t kFilterTaps = kOversampleFactor * kTapsPerPhase; // 48

    TruePeakMeter();

    /**
     * @brief (Re)generate filter coefficients for the given sample rate and clear history.
     * @note Coefficient values are sample-rate agnostic for a fixed oversample
     *       factor; the parameter is retained for API stability and future
     *       sample-rate-aware variants.
     */
    void initialize(uint32_t sampleRate) noexcept;

    /**
     * @brief Clear filter history and running peak accumulators.
     */
    void reset() noexcept;

    /**
     * @brief Clear running peak accumulators only; preserve filter history.
     *
     * Use this when you want to take a fresh peak reading (e.g. UI meter
     * "peak hold" reset) without disturbing the FIR's internal state. RT-safe.
     */
    void clearPeaks() noexcept AESTRA_RT_NONBLOCKING {
        m_samplePeakL = 0.0f;
        m_samplePeakR = 0.0f;
        m_truePeakL = 0.0f;
        m_truePeakR = 0.0f;
    }

    /**
     * @brief Process an interleaved stereo block and update peaks.
     * @param interleavedStereo  Pointer to L/R/L/R/... float samples.
     * @param numFrames          Number of frames (one frame = one stereo pair).
     *
     * RT-safe: uses only fixed-size arrays already owned by *this — and
     * AESTRA_RT_NONBLOCKING is what holds that claim to the compiler.
     */
    void processStereo(const float* interleavedStereo,
                       uint32_t numFrames) noexcept AESTRA_RT_NONBLOCKING;

    /**
     * @brief Process a single mono channel.
     */
    void processMono(const float* mono, uint32_t numFrames) noexcept AESTRA_RT_NONBLOCKING;

    // ---------------------------------------------------------------------
    // Accessors (RT-safe; cheap)
    // ---------------------------------------------------------------------

    float getSamplePeakL() const noexcept { return m_samplePeakL; }
    float getSamplePeakR() const noexcept { return m_samplePeakR; }
    float getTruePeakL() const noexcept { return m_truePeakL; }
    float getTruePeakR() const noexcept { return m_truePeakR; }

    float getMaxSamplePeak() const noexcept {
        return m_samplePeakL > m_samplePeakR ? m_samplePeakL : m_samplePeakR;
    }
    float getMaxTruePeak() const noexcept {
        return m_truePeakL > m_truePeakR ? m_truePeakL : m_truePeakR;
    }

    /**
     * @brief Convert a linear peak value to dBTP, with a -200 dB floor for silence.
     */
    static float linearToDbTp(float linearPeak) noexcept {
        return linearPeak > 0.0f ? 20.0f * std::log10(linearPeak) : -200.0f;
    }

    float getTruePeakLdBTP() const noexcept { return linearToDbTp(m_truePeakL); }
    float getTruePeakRdBTP() const noexcept { return linearToDbTp(m_truePeakR); }
    float getMaxTruePeakdBTP() const noexcept { return linearToDbTp(getMaxTruePeak()); }

    uint32_t getSampleRate() const noexcept { return m_sampleRate; }
    bool isInitialized() const noexcept { return m_initialized; }

private:
    // Polyphase coefficients, TRANSPOSED.
    //
    // m_polyphaseT[tap][phase] is the coefficient of `phase` at `tap`, where
    // the prototype FIR is h[phase + kOversampleFactor*tap].
    //
    // The transposed layout is what makes the hot loop a single vector
    // accumulation: the four phases read the SAME history, so one 4-wide
    // accumulator can hold all four phase sums at once, one phase per lane.
    // Storing the coefficients phase-major would mean four separate 12-tap
    // dot products, each with its own serial accumulator.
    //
    // The tap order is unchanged, so each lane accumulates exactly the same
    // products in exactly the same order as the scalar version did. The result
    // is BIT-IDENTICAL, not merely close.
    alignas(16) std::array<std::array<float, kOversampleFactor>, kTapsPerPhase> m_polyphaseT{};
    // The hot loop reads four coefficients at a time. The loads use the
    // unaligned form on purpose: the member IS 16-byte aligned and each inner
    // array is exactly 16 bytes, so the addresses are aligned in practice —
    // but making correctness depend on that is a trap, and an unaligned load
    // costs nothing on any CPU this runs on.
    static_assert(sizeof(std::array<float, kOversampleFactor>) == 16,
                  "the polyphase coefficient rows must be exactly one vector wide");

    // Per-channel history of the most recent kTapsPerPhase input samples.
    // Indexed [0] = newest, [kTapsPerPhase-1] = oldest (FIFO shift on push).
    alignas(16) std::array<float, kTapsPerPhase> m_historyL{};
    alignas(16) std::array<float, kTapsPerPhase> m_historyR{};

    float m_samplePeakL{0.0f};
    float m_samplePeakR{0.0f};
    float m_truePeakL{0.0f};
    float m_truePeakR{0.0f};

    uint32_t m_sampleRate{48000};
    bool m_initialized{false};

    // Helpers
    void generateCoefficients() noexcept;
    static double besselI0(double x) noexcept;
    static double kaiser(double n, double N, double beta) noexcept;

    // Push a sample into a history FIFO (newest first).
    static inline void pushHistory(std::array<float, kTapsPerPhase>& hist, float sample) noexcept {
#if defined(AESTRA_DSP_HAS_SSE2)
        // Shift the newest-first FIFO right by one and write the sample at 0.
        // Descending four-wide moves: each store reads the four entries one
        // below it, and those have not been written yet because the copy runs
        // from the top down.
        _mm_storeu_ps(&hist[8], _mm_loadu_ps(&hist[7]));
        _mm_storeu_ps(&hist[4], _mm_loadu_ps(&hist[3]));
        _mm_storeu_ps(&hist[1], _mm_loadu_ps(&hist[0]));
#else
        for (uint32_t i = kTapsPerPhase - 1; i > 0; --i) {
            hist[i] = hist[i - 1];
        }
#endif
        hist[0] = sample;
    }

    // Compute the maximum |y| across the 4 polyphase outputs for one channel,
    // using the current history. Updates the running true peak.
    inline void updateTruePeakFromHistory(const std::array<float, kTapsPerPhase>& hist,
                                          float& runningTruePeak) const noexcept {
#if defined(AESTRA_DSP_HAS_SSE2)
        // One accumulator, four lanes: lane p is the sum for polyphase phase p.
        __m128 acc = _mm_setzero_ps();
        for (uint32_t tap = 0; tap < kTapsPerPhase; ++tap) {
            acc = _mm_add_ps(acc, _mm_mul_ps(_mm_set1_ps(hist[tap]),
                                             _mm_loadu_ps(&m_polyphaseT[tap][0])));
        }
        // Magnitude of each lane, then the largest of the four.
        const __m128 mag = _mm_andnot_ps(_mm_set1_ps(-0.0f), acc);
        __m128 t = _mm_max_ps(mag, _mm_movehl_ps(mag, mag));
        t = _mm_max_ss(t, _mm_shuffle_ps(t, t, 0x55));
        const float peak = _mm_cvtss_f32(t);
        if (peak > runningTruePeak) {
            runningTruePeak = peak;
        }
#else
        for (uint32_t phase = 0; phase < kOversampleFactor; ++phase) {
            float acc = 0.0f;
            for (uint32_t tap = 0; tap < kTapsPerPhase; ++tap) {
                acc += m_polyphaseT[tap][phase] * hist[tap];
            }
            const float mag = acc < 0.0f ? -acc : acc;
            if (mag > runningTruePeak) {
                runningTruePeak = mag;
            }
        }
#endif
    }
};

} // namespace Audio
} // namespace Aestra
