// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

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
    void clearPeaks() noexcept {
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
     * RT-safe: uses only fixed-size arrays already owned by *this.
     */
    void processStereo(const float* interleavedStereo, uint32_t numFrames) noexcept;

    /**
     * @brief Process a single mono channel.
     */
    void processMono(const float* mono, uint32_t numFrames) noexcept;

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
    // Polyphase coefficients: m_polyphase[phase][tap] is e_phase[tap]
    // where the prototype FIR is h[phase + kOversampleFactor*tap].
    // Aligned for SIMD-friendly access if we vectorise later.
    alignas(32) std::array<std::array<float, kTapsPerPhase>, kOversampleFactor> m_polyphase{};

    // Per-channel history of the most recent kTapsPerPhase input samples.
    // Indexed [0] = newest, [kTapsPerPhase-1] = oldest (FIFO shift on push).
    alignas(32) std::array<float, kTapsPerPhase> m_historyL{};
    alignas(32) std::array<float, kTapsPerPhase> m_historyR{};

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

    // Push a sample into a history FIFO (newest first), returning nothing.
    static inline void pushHistory(std::array<float, kTapsPerPhase>& hist, float sample) noexcept {
        // Shift newer-first FIFO right by one and write sample at index 0.
        for (uint32_t i = kTapsPerPhase - 1; i > 0; --i) {
            hist[i] = hist[i - 1];
        }
        hist[0] = sample;
    }

    // Compute the maximum |y| across the 4 polyphase outputs for one channel,
    // using the current history. Updates the running true peak.
    inline void updateTruePeakFromHistory(const std::array<float, kTapsPerPhase>& hist,
                                          float& runningTruePeak) const noexcept {
        for (uint32_t phase = 0; phase < kOversampleFactor; ++phase) {
            float acc = 0.0f;
            const auto& coeffs = m_polyphase[phase];
            for (uint32_t tap = 0; tap < kTapsPerPhase; ++tap) {
                acc += coeffs[tap] * hist[tap];
            }
            const float mag = acc < 0.0f ? -acc : acc;
            if (mag > runningTruePeak) {
                runningTruePeak = mag;
            }
        }
    }
};

} // namespace Audio
} // namespace Aestra
