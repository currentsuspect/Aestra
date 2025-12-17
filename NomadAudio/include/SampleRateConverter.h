// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cmath>

// SIMD detection
#if defined(_MSC_VER)
    #include <intrin.h>
    #define NOMAD_HAS_SSE 1
    #if defined(__AVX__) || defined(__AVX2__)
        #define NOMAD_HAS_AVX 1
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(__SSE__) || defined(__x86_64__)
        #include <x86intrin.h>
        #define NOMAD_HAS_SSE 1
    #endif
    #if defined(__AVX__)
        #define NOMAD_HAS_AVX 1
    #endif
#endif

namespace Nomad {
namespace Audio {

// =============================================================================
// Sample Rate Converter Quality Levels
// =============================================================================

/**
 * @brief Resampling quality presets
 * 
 * Higher quality uses more filter taps but produces better results.
 * All modes are real-time safe with zero dynamic allocation.
 */
enum class SRCQuality {
    Linear,     // 2-point linear interpolation (lowest CPU, audible artifacts)
    Cubic,      // 4-point cubic Hermite (good balance, minimal artifacts)
    Sinc8,      // 8-point windowed sinc (high quality)
    Sinc16,     // 16-point windowed sinc (mastering grade)
    Sinc64      // 64-point windowed sinc (reference grade)
};

// =============================================================================
// Compile-Time Constants
// =============================================================================

namespace SRCConstants {
    // Maximum supported filter size (must accommodate largest quality mode)
    constexpr uint32_t MAX_FILTER_TAPS = 64;
    
    // Number of fractional phases for polyphase filter bank
    // Higher = better accuracy, more memory. 256 is a good balance.
    constexpr uint32_t POLYPHASE_PHASES = 256;
    
    // Maximum supported channels (for fixed-size history buffer)
    constexpr uint32_t MAX_CHANNELS = 8;
    
    // History buffer size: enough samples to cover max filter + lookahead
    constexpr uint32_t HISTORY_SIZE = MAX_FILTER_TAPS * 2;
    
    // Mathematical constants
    constexpr double PI = 3.14159265358979323846;
    constexpr double KAISER_BETA_DEFAULT = 8.0;  // Kaiser window parameter
}

// =============================================================================
// Precomputed Filter Table (Polyphase Sinc Coefficients)
// =============================================================================

/**
 * @brief Precomputed polyphase filter bank
 * 
 * Stores windowed sinc coefficients organized by fractional phase.
 * Generated once during configure(), used in process() with zero overhead.
 * 
 * Layout: coeffs[phase][tap] where:
 *   - phase ∈ [0, POLYPHASE_PHASES)
 *   - tap ∈ [0, numTaps)
 */
struct PolyphaseFilterBank {
    // Coefficient storage: [phase][tap]
    // Aligned for SIMD (32-byte for AVX, 16-byte for SSE)
    alignas(32) float coeffs[SRCConstants::POLYPHASE_PHASES][SRCConstants::MAX_FILTER_TAPS];
    
    uint32_t numTaps{0};      // Active taps for current quality
    uint32_t halfTaps{0};     // numTaps / 2 (for centering)
    
    // Clear all coefficients
    void clear() noexcept {
        for (auto& phase : coeffs) {
            for (float& c : phase) c = 0.0f;
        }
        numTaps = 0;
        halfTaps = 0;
    }
};

// =============================================================================
// Sample History Ring Buffer
// =============================================================================

/**
 * @brief Ring buffer for input sample history
 * 
 * Maintains enough history for the filter to operate. Fixed size allocation,
 * zero dynamic allocation during operation.
 */
struct SampleHistory {
    // Planar + mirrored storage for SIMD-friendly contiguous windows.
    // Layout: data[channel][index], where index spans kMirrorFactor * HISTORY_SIZE.
    // Mirroring eliminates wrap checks for tap windows (RT-safe, fixed size).
    static constexpr uint32_t kMirrorFactor = 3; // 2 is usually enough; 3 covers any start+MAX_TAPS window safely.

    alignas(32) float data[SRCConstants::MAX_CHANNELS][SRCConstants::HISTORY_SIZE * kMirrorFactor];
    
    uint32_t writePos{0};       // Current write position in ring
    uint32_t channels{0};       // Number of active channels
    uint32_t size{0};           // Total frames in history
    
    // Initialize the history buffer
    void init(uint32_t numChannels) noexcept {
        channels = numChannels;
        size = SRCConstants::HISTORY_SIZE;
        writePos = 0;
        // Clear buffer
        for (auto& chBuf : data) {
            for (float& s : chBuf) {
                s = 0.0f;
            }
        }
    }
    
    // Push a frame (all channels) into the ring buffer
    void push(const float* frame) noexcept {
        const uint32_t base0 = writePos;
        const uint32_t base1 = base0 + size;
        const uint32_t base2 = base0 + 2 * size;

        for (uint32_t ch = 0; ch < channels; ++ch) {
            const float s = frame[ch];
            data[ch][base0] = s;
            data[ch][base1] = s;
            data[ch][base2] = s;
        }
        writePos = (writePos + 1) % size;
    }
    
    // Get a contiguous window pointer for the given channel starting at relPos
    // (0 = oldest, size-1 = newest). relPos wraps like get().
    const float* getWindow(uint32_t channel, int32_t relPos) const noexcept {
        if (size == 0) return nullptr;
        const int32_t sizeI = static_cast<int32_t>(size);
        int32_t rel = relPos % sizeI;
        if (rel < 0) rel += sizeI;

        // Chronological ring is laid out contiguously at [writePos .. writePos+size-1].
        // Mirroring extends past wrap so tap windows are contiguous.
        const uint32_t idx = writePos + static_cast<uint32_t>(rel);
        return &data[channel][idx];
    }

    // Scalar accessor (kept for reference/testing and edge-case fallbacks)
    float get(int32_t relPos, uint32_t channel) const noexcept {
        const float* p = getWindow(channel, relPos);
        return p ? *p : 0.0f;
    }
};

// =============================================================================
// Sample Rate Converter
// =============================================================================

/**
 * @brief Real-time sample rate converter
 * 
 * Converts audio between sample rates using high-quality polyphase sinc
 * interpolation. Designed for real-time audio with:
 * 
 * - Zero dynamic allocation in process()
 * - Precomputed filter coefficients
 * - Configurable quality/CPU tradeoff
 * - Multi-channel interleaved audio support
 * 
 * Usage:
 * @code
 *   SampleRateConverter src;
 *   src.configure(44100, 48000, 2, SRCQuality::Sinc16);
 *   
 *   // In audio callback:
 *   uint32_t written = src.process(input, inputFrames, output, maxOutputFrames);
 * @endcode
 */
class SampleRateConverter {
public:
    SampleRateConverter() = default;
    ~SampleRateConverter() = default;
    
    // Non-copyable (contains internal state)
    SampleRateConverter(const SampleRateConverter&) = delete;
    SampleRateConverter& operator=(const SampleRateConverter&) = delete;
    
    // Move is allowed
    SampleRateConverter(SampleRateConverter&&) = default;
    SampleRateConverter& operator=(SampleRateConverter&&) = default;
    
    // =========================================================================
    // Configuration
    // =========================================================================
    
    /**
     * @brief Configure the converter for a specific rate conversion
     * 
     * This precomputes the polyphase filter bank. Should be called before
     * process(), and can be called again to reconfigure.
     * 
     * @param srcRate Source sample rate (Hz)
     * @param dstRate Destination sample rate (Hz)
     * @param channels Number of audio channels (interleaved)
     * @param quality Interpolation quality
     * 
     * @note NOT real-time safe (allocates filter tables). Call from main thread.
     * @note If srcRate == dstRate, process() becomes a simple passthrough.
     */
    void configure(uint32_t srcRate, uint32_t dstRate, 
                   uint32_t channels, SRCQuality quality);
    
    /**
     * @brief Reset internal state
     * 
     * Clears history buffer and resets position accumulator.
     * Call when seeking or switching audio sources.
     * 
     * @note Real-time safe (no allocation)
     */
    void reset() noexcept;
    
    // =========================================================================
    // Processing
    // =========================================================================
    
    /**
     * @brief Process audio through the rate converter
     * 
     * Converts input audio at source rate to output at destination rate.
     * The number of output frames depends on the ratio and may vary slightly
     * frame-to-frame to maintain phase accuracy.
     * 
     * @param input Input samples (interleaved, srcRate)
     * @param inputFrames Number of input frames to process
     * @param output Output buffer (interleaved, dstRate)
     * @param maxOutputFrames Maximum capacity of output buffer
     * 
     * @return Number of output frames actually written
     * 
     * @note REAL-TIME SAFE: Zero allocation, no exceptions, no locks
     * @note Caller must ensure output buffer is large enough. Safe estimate:
     *       maxOutputFrames >= inputFrames * (dstRate / srcRate) + getLatency()
     */
    uint32_t process(const float* input, uint32_t inputFrames,
                     float* output, uint32_t maxOutputFrames) noexcept;
    
    /**
     * @brief Update conversion ratio in real-time (for pitch shifting)
     * 
     * Smoothly transitions to a new ratio over the specified number of frames
     * to avoid audio clicks.
     * 
     * @param newRatio Target ratio (>1 = faster/higher pitch, <1 = slower/lower pitch)
     * @param smoothFrames Frames over which to transition (0 = instant)
     * 
     * @note REAL-TIME SAFE: Can be called from audio thread
     */
    void setRatio(double newRatio, uint32_t smoothFrames = 256) noexcept;
    
    /**
     * @brief Get current effective ratio
     */
    double getCurrentRatio() const noexcept { return m_currentRatio; }
    
    // =========================================================================
    // Query
    // =========================================================================
    
    /**
     * @brief Get the latency introduced by the converter
     * 
     * @return Latency in output frames (depends on filter size)
     */
    uint32_t getLatency() const noexcept { return m_filterBank.halfTaps; }
    
    /**
     * @brief Check if configured and ready to process
     */
    bool isConfigured() const noexcept { return m_configured; }
    
    /**
     * @brief Check if this is a passthrough (same rate, no processing needed)
     */
    bool isPassthrough() const noexcept { return m_isPassthrough; }
    
    /**
     * @brief Get current quality setting
     */
    SRCQuality getQuality() const noexcept { return m_quality; }
    
    /**
     * @brief Get source sample rate
     */
    uint32_t getSourceRate() const noexcept { return m_srcRate; }
    
    /**
     * @brief Get destination sample rate
     */
    uint32_t getDestinationRate() const noexcept { return m_dstRate; }
    
    /**
     * @brief Get channel count
     */
    uint32_t getChannels() const noexcept { return m_channels; }
    
    /**
     * @brief Check if SIMD acceleration is available
     */
    static bool hasSIMD() noexcept {
#ifdef NOMAD_HAS_SSE
        return true;
#else
        return false;
#endif
    }
    
    /**
     * @brief Check if AVX acceleration is available
     */
    static bool hasAVX() noexcept {
#ifdef NOMAD_HAS_AVX
        return true;
#else
        return false;
#endif
    }

    // Allow tests/tools to force scalar processing even when SIMD is available.
    // RT-safe (atomic flag read in process()).
    void setSIMDEnabled(bool enabled) noexcept { m_simdEnabled.store(enabled, std::memory_order_relaxed); }
    bool isSIMDEnabled() const noexcept { return m_simdEnabled.load(std::memory_order_relaxed); }
    
private:
    // =========================================================================
    // Internal Methods
    // =========================================================================
    
    // Generate polyphase filter coefficients for given quality
    void generateFilterBank(SRCQuality quality);
    
    // Calculate Kaiser window value
    static double kaiserWindow(double n, double N, double beta) noexcept;
    
    // Calculate modified Bessel function I0
    static double bessel_I0(double x) noexcept;
    
    // Interpolate one output sample for one channel using polyphase lookup
    float interpolateSample(uint32_t channel, uint32_t phaseIndex, 
                           int32_t centerPos) const noexcept;
    
    // =========================================================================
    // State
    // =========================================================================
    
    // Configuration
    uint32_t m_srcRate{0};
    uint32_t m_dstRate{0};
    uint32_t m_channels{0};
    SRCQuality m_quality{SRCQuality::Sinc16};
    bool m_configured{false};
    bool m_isPassthrough{false};
    
    // Rate ratio (dst/src) as double for precision
    double m_ratio{1.0};
    
    // Fractional position accumulator (maintains phase between calls)
    double m_srcPosition{0.0};
    
    // Precomputed polyphase filter bank
    PolyphaseFilterBank m_filterBank;
    
    // Input sample history (ring buffer)
    SampleHistory m_history;
    
    // Number of valid samples in history (for initial filling)
    uint32_t m_historyFilled{0};
    
    // Variable ratio support (for pitch shifting)
    double m_currentRatio{1.0};      // Current (possibly smoothed) ratio
    double m_targetRatio{1.0};       // Target ratio to smooth toward
    uint32_t m_ratioSmoothFrames{0}; // Frames remaining in transition
    uint32_t m_ratioSmoothTotal{0};  // Total frames for current transition

    // SIMD enable toggle (mostly for tests / debugging)
    std::atomic<bool> m_simdEnabled{true};
};

// =============================================================================
// Utility: Estimate Output Frame Count
// =============================================================================

/**
 * @brief Estimate number of output frames for given input
 * 
 * Useful for pre-allocating output buffers.
 * 
 * @param inputFrames Number of input frames
 * @param srcRate Source sample rate
 * @param dstRate Destination sample rate
 * @param latency Converter latency (from getLatency())
 * 
 * @return Estimated maximum output frames (may slightly overestimate)
 */
inline uint32_t estimateOutputFrames(uint32_t inputFrames, 
                                      uint32_t srcRate, 
                                      uint32_t dstRate,
                                      uint32_t latency = 0) noexcept {
    const double ratio = static_cast<double>(dstRate) / static_cast<double>(srcRate);
    return static_cast<uint32_t>(std::ceil(inputFrames * ratio)) + latency + 1;
}

} // namespace Audio
} // namespace Nomad
