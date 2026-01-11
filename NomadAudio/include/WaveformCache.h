// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TimeTypes.h"
#include "ClipSource.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace Nomad {
namespace Audio {

// =============================================================================
// WaveformPeak - Min/Max sample pair
// =============================================================================

/**
 * @brief A single min/max peak pair for waveform display
 */
struct WaveformPeak {
    float min = 0.0f;
    float max = 0.0f;
    
    WaveformPeak() = default;
    WaveformPeak(float minVal, float maxVal) : min(minVal), max(maxVal) {}
    
    /// Merge with another peak (union)
    void merge(const WaveformPeak& other) {
        min = std::min(min, other.min);
        max = std::max(max, other.max);
    }
};

// =============================================================================
// WaveformMipLevel - Single resolution level of peaks
// =============================================================================

/**
 * @brief A single mipmap level of waveform peaks
 * 
 * Each level stores min/max pairs at a specific samples-per-pixel ratio.
 * Lower levels = more detail, higher levels = more zoomed out.
 */
struct WaveformMipLevel {
    std::vector<WaveformPeak> peaks;     ///< Peak data for each channel
    uint32_t samplesPerPeak = 1;         ///< How many source samples per peak
    uint32_t numChannels = 0;            ///< Number of channels
    SampleIndex numPeaks = 0;            ///< Number of peaks per channel
    
    /// Get peak at index for channel
    WaveformPeak getPeak(uint32_t channel, SampleIndex peakIndex) const {
        if (channel >= numChannels || peakIndex < 0 || peakIndex >= numPeaks) {
            return WaveformPeak();
        }
        size_t idx = static_cast<size_t>(peakIndex * numChannels + channel);
        return idx < peaks.size() ? peaks[idx] : WaveformPeak();
    }
    
    /// Get interpolated peak at fractional index
    WaveformPeak getInterpolatedPeak(uint32_t channel, double peakIndex) const {
        if (channel >= numChannels || peakIndex < 0 || numPeaks == 0) {
            return WaveformPeak();
        }
        
        SampleIndex idx0 = static_cast<SampleIndex>(peakIndex);
        SampleIndex idx1 = idx0 + 1;
        float frac = static_cast<float>(peakIndex - idx0);
        
        WaveformPeak p0 = getPeak(channel, idx0);
        WaveformPeak p1 = getPeak(channel, idx1);
        
        return WaveformPeak(
            p0.min + frac * (p1.min - p0.min),
            p0.max + frac * (p1.max - p0.max)
        );
    }
    
    /// Get peaks for a range, merged
    WaveformPeak getPeakRange(uint32_t channel, SampleIndex startPeak, SampleIndex endPeak) const {
        if (channel >= numChannels || startPeak >= numPeaks) {
            return WaveformPeak();
        }
        
        startPeak = std::max(startPeak, static_cast<SampleIndex>(0));
        endPeak = std::min(endPeak, numPeaks);
        
        if (startPeak >= endPeak) {
            return getPeak(channel, startPeak);
        }
        
        WaveformPeak result = getPeak(channel, startPeak);
        for (SampleIndex i = startPeak + 1; i < endPeak; ++i) {
            result.merge(getPeak(channel, i));
        }
        return result;
    }
};

// =============================================================================
// WaveformCache - Multi-resolution peak cache
// =============================================================================

/**
 * @brief Multi-resolution waveform peak cache for efficient rendering
 * 
 * This cache stores pre-computed min/max peak data at multiple resolutions
 * (mipmap levels). The UI queries the appropriate level based on zoom.
 * 
 * Mip levels (example):
 * - Level 0: 64 samples/peak (most detailed)
 * - Level 1: 256 samples/peak
 * - Level 2: 1024 samples/peak
 * - Level 3: 4096 samples/peak
 * - Level 4: 16384 samples/peak (most zoomed out)
 * 
 * The cache is built on a background thread when audio is loaded.
 * The UI can check isReady() before using.
 */
class WaveformCache {
public:
    /// Default mip level settings
    static constexpr uint32_t DEFAULT_BASE_SAMPLES_PER_PEAK = 64;
    static constexpr uint32_t DEFAULT_NUM_LEVELS = 5;
    static constexpr uint32_t MIP_LEVEL_MULTIPLIER = 4;  // Each level is 4x coarser
    
    WaveformCache();
    ~WaveformCache();
    
    // Non-copyable, movable
    WaveformCache(const WaveformCache&) = delete;
    WaveformCache& operator=(const WaveformCache&) = delete;
    WaveformCache(WaveformCache&&) = default;
    WaveformCache& operator=(WaveformCache&&) = default;
    
    /**
     * @brief Build cache from audio buffer
     * 
     * Should be called on a worker thread. Sets ready flag when complete.
     * 
     * @param buffer Source audio buffer
     * @param baseSamplesPerPeak Samples per peak at finest level
     * @param numLevels Number of mip levels to generate
     */
    void buildFromBuffer(const AudioBufferData& buffer,
                         uint32_t baseSamplesPerPeak = DEFAULT_BASE_SAMPLES_PER_PEAK,
                         uint32_t numLevels = DEFAULT_NUM_LEVELS);
    
    /**
     * @brief Build cache from raw interleaved audio
     */
    void buildFromRaw(const float* data, SampleIndex numFrames, uint32_t numChannels,
                      uint32_t baseSamplesPerPeak = DEFAULT_BASE_SAMPLES_PER_PEAK,
                      uint32_t numLevels = DEFAULT_NUM_LEVELS);
    
    /// Check if cache is ready for use
    bool isReady() const { return m_ready.load(std::memory_order_acquire); }
    
    /// Get number of mip levels
    size_t getNumLevels() const { return m_levels.size(); }
    
    /// Get number of channels
    uint32_t getNumChannels() const { return m_numChannels; }
    
    /// Get source frame count
    SampleIndex getSourceFrames() const { return m_sourceFrames; }
    
    /// Get a mip level
    const WaveformMipLevel* getLevel(size_t levelIndex) const;
    
    /**
     * @brief Select the best mip level for a given samples-per-pixel
     * 
     * Returns the level that most closely matches without being too detailed.
     * 
     * @param samplesPerPixel Current zoom level (samples per screen pixel)
     * @return Best mip level index
     */
    size_t selectLevel(double samplesPerPixel) const;
    
    /**
     * @brief Get peaks for drawing a range at specified zoom
     * 
     * This is the main method for UI rendering. It automatically selects
     * the appropriate mip level and returns peaks for the visible range.
     * 
     * @param channel Channel index
     * @param startSample Start sample in source audio
     * @param endSample End sample in source audio
     * @param numPixels Number of pixels to render
     * @param outPeaks Output vector of peaks (one per pixel)
     */
    void getPeaksForRange(uint32_t channel,
                          SampleIndex startSample, SampleIndex endSample,
                          uint32_t numPixels,
                          std::vector<WaveformPeak>& outPeaks) const;
    
    /// Get a single peak for quick display
    WaveformPeak getQuickPeak(uint32_t channel, SampleIndex startSample, SampleIndex numSamples) const;
    
    /// Clear all cached data
    void clear();
    
    /// Get memory usage in bytes
    size_t getMemoryUsage() const;

private:
    std::vector<WaveformMipLevel> m_levels;
    uint32_t m_numChannels = 0;
    SampleIndex m_sourceFrames = 0;
    std::atomic<bool> m_ready{false};
    mutable std::mutex m_mutex;
    
    void buildLevel(const float* data, SampleIndex numFrames, uint32_t numChannels,
                   uint32_t samplesPerPeak, WaveformMipLevel& outLevel);
    void buildNextLevel(const WaveformMipLevel& source, WaveformMipLevel& dest);
};

// =============================================================================
// WaveformCacheBuilder - Async cache generation
// =============================================================================

/**
 * @brief Helper for async waveform cache building
 * 
 * Example usage:
 * ```
 * WaveformCacheBuilder builder;
 * builder.buildAsync(source, [](std::shared_ptr<WaveformCache> cache) {
 *     source->setWaveformCache(cache);
 * });
 * ```
 */
class WaveformCacheBuilder {
public:
    using CompletionCallback = std::function<void(std::shared_ptr<WaveformCache>)>;
    
    WaveformCacheBuilder();
    ~WaveformCacheBuilder();
    
    /**
     * @brief Build cache asynchronously
     * 
     * @param source Source to build cache for
     * @param callback Called on completion (may be on worker thread)
     */
    void buildAsync(const ClipSource& source, CompletionCallback callback);
    
    /**
     * @brief Build cache synchronously (blocking)
     */
    std::shared_ptr<WaveformCache> buildSync(const ClipSource& source);
    
    /// Cancel any pending builds
    void cancelAll();
    
    /// Get number of pending builds
    size_t getPendingCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Audio
} // namespace Nomad
