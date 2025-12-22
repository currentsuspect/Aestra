// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Nomad {
namespace Audio {

// =============================================================================
// Canonical Time Units
// =============================================================================

/**
 * @brief Absolute timeline position in samples at project sample rate
 * 
 * This is the canonical unit for all timeline operations.
 * Using 64-bit integer allows for ~6.6 million hours at 48kHz.
 */
using SampleIndex = int64_t;

/**
 * @brief Length of buffers or relative offsets (32-bit for efficiency)
 * 
 * Used for buffer sizes and relative offsets within a single buffer.
 * Max value ~4 billion samples (~24 hours at 48kHz single buffer).
 */
using SampleCount = int32_t;

/**
 * @brief Musical time in ticks (PPQN-based)
 * 
 * For beat/bar calculations. Default PPQN is 960.
 */
using TickIndex = int64_t;

// =============================================================================
// Time Constants
// =============================================================================

constexpr int32_t DEFAULT_PPQN = 960;           // Pulses per quarter note
constexpr double DEFAULT_SAMPLE_RATE = 48000.0;
constexpr double DEFAULT_BPM = 120.0;

// Maximum values for safety checks
constexpr SampleIndex MAX_TIMELINE_SAMPLES = INT64_MAX / 2;
constexpr SampleCount MAX_BUFFER_SAMPLES = 8192;

// =============================================================================
// Time Conversion Functions
// =============================================================================

/**
 * @brief Convert seconds to sample index
 * @param seconds Time in seconds
 * @param sampleRate Sample rate in Hz
 * @return Sample index (floored to nearest sample)
 */
inline SampleIndex secondsToSamples(double seconds, double sampleRate) {
    return static_cast<SampleIndex>(std::floor(seconds * sampleRate));
}

/**
 * @brief Convert sample index to seconds
 * @param samples Sample index
 * @param sampleRate Sample rate in Hz
 * @return Time in seconds
 */
inline double samplesToSeconds(SampleIndex samples, double sampleRate) {
    if (sampleRate <= 0.0) return 0.0;
    return static_cast<double>(samples) / sampleRate;
}

/**
 * @brief Convert beats to sample index
 * @param beats Time in beats (quarter notes)
 * @param bpm Tempo in beats per minute
 * @param sampleRate Sample rate in Hz
 * @return Sample index
 */
inline SampleIndex beatsToSamples(double beats, double bpm, double sampleRate) {
    if (bpm <= 0.0) return 0;
    double seconds = (beats * 60.0) / bpm;
    return secondsToSamples(seconds, sampleRate);
}

/**
 * @brief Convert sample index to beats
 * @param samples Sample index
 * @param bpm Tempo in beats per minute
 * @param sampleRate Sample rate in Hz
 * @return Time in beats
 */
inline double samplesToBeats(SampleIndex samples, double bpm, double sampleRate) {
    if (bpm <= 0.0 || sampleRate <= 0.0) return 0.0;
    double seconds = samplesToSeconds(samples, sampleRate);
    return (seconds * bpm) / 60.0;
}

/**
 * @brief Convert ticks to sample index
 * @param ticks Musical time in ticks
 * @param bpm Tempo in beats per minute
 * @param sampleRate Sample rate in Hz
 * @param ppqn Pulses per quarter note
 * @return Sample index
 */
inline SampleIndex ticksToSamples(TickIndex ticks, double bpm, double sampleRate, int32_t ppqn = DEFAULT_PPQN) {
    if (ppqn <= 0) return 0;
    double beats = static_cast<double>(ticks) / static_cast<double>(ppqn);
    return beatsToSamples(beats, bpm, sampleRate);
}

/**
 * @brief Convert sample index to ticks
 * @param samples Sample index
 * @param bpm Tempo in beats per minute
 * @param sampleRate Sample rate in Hz
 * @param ppqn Pulses per quarter note
 * @return Musical time in ticks
 */
inline TickIndex samplesToTicks(SampleIndex samples, double bpm, double sampleRate, int32_t ppqn = DEFAULT_PPQN) {
    double beats = samplesToBeats(samples, bpm, sampleRate);
    return static_cast<TickIndex>(std::round(beats * ppqn));
}

/**
 * @brief Convert between sample rates
 * @param samples Sample index at source rate
 * @param sourceSampleRate Original sample rate
 * @param targetSampleRate Target sample rate
 * @return Sample index at target rate
 */
inline SampleIndex convertSampleRate(SampleIndex samples, double sourceSampleRate, double targetSampleRate) {
    if (sourceSampleRate <= 0.0 || targetSampleRate <= 0.0) return 0;
    if (sourceSampleRate == targetSampleRate) return samples;
    double seconds = samplesToSeconds(samples, sourceSampleRate);
    return secondsToSamples(seconds, targetSampleRate);
}

// =============================================================================
// Time Range Structure
// =============================================================================

/**
 * @brief Represents a range of time on the timeline
 * 
 * Used for clip boundaries, selection ranges, and buffer windows.
 * Convention: [start, end) - start is inclusive, end is exclusive.
 */
struct SampleRange {
    SampleIndex start = 0;
    SampleIndex end = 0;
    
    SampleRange() = default;
    SampleRange(SampleIndex s, SampleIndex e) : start(s), end(e) {}
    
    /// Length of the range in samples
    SampleIndex length() const { return end - start; }
    
    /// Check if range is valid (positive length)
    bool isValid() const { return end > start; }
    
    /// Check if range is empty
    bool isEmpty() const { return end <= start; }
    
    /// Check if a sample falls within this range
    bool contains(SampleIndex sample) const { return sample >= start && sample < end; }
    
    /// Check if this range overlaps with another
    bool overlaps(const SampleRange& other) const {
        return !(end <= other.start || start >= other.end);
    }
    
    /// Get the intersection with another range
    SampleRange intersection(const SampleRange& other) const {
        SampleRange result;
        result.start = std::max(start, other.start);
        result.end = std::min(end, other.end);
        if (result.end <= result.start) {
            result.start = result.end = 0;
        }
        return result;
    }
    
    /// Get the union with another range (may include gap)
    SampleRange boundingUnion(const SampleRange& other) const {
        if (isEmpty()) return other;
        if (other.isEmpty()) return *this;
        return SampleRange(std::min(start, other.start), std::max(end, other.end));
    }
    
    /// Offset the range by a delta
    SampleRange offset(SampleIndex delta) const {
        return SampleRange(start + delta, end + delta);
    }
    
    /// Convert to seconds
    double startSeconds(double sampleRate) const { return samplesToSeconds(start, sampleRate); }
    double endSeconds(double sampleRate) const { return samplesToSeconds(end, sampleRate); }
    double durationSeconds(double sampleRate) const { return samplesToSeconds(length(), sampleRate); }
};

// =============================================================================
// Grid & Quantization Helpers
// =============================================================================

/**
 * @brief Grid subdivision values
 */
enum class GridSubdivision {
    Bar,        // 4 beats
    Beat,       // 1 beat (quarter note)
    Half,       // 1/2 beat
    Quarter,    // 1/4 beat (16th note)
    Eighth,     // 1/8 beat (32nd note)
    Triplet,    // 1/3 beat
    None        // No grid (free positioning)
};

/**
 * @brief Get the interval in samples for a grid subdivision
 */
inline SampleIndex getGridInterval(GridSubdivision subdivision, double bpm, double sampleRate) {
    if (subdivision == GridSubdivision::None) return 1;
    
    double beatsPerInterval = 1.0;
    switch (subdivision) {
        case GridSubdivision::Bar:      beatsPerInterval = 4.0; break;
        case GridSubdivision::Beat:     beatsPerInterval = 1.0; break;
        case GridSubdivision::Half:     beatsPerInterval = 0.5; break;
        case GridSubdivision::Quarter:  beatsPerInterval = 0.25; break;
        case GridSubdivision::Eighth:   beatsPerInterval = 0.125; break;
        case GridSubdivision::Triplet:  beatsPerInterval = 1.0 / 3.0; break;
        default: return 1;
    }
    
    return beatsToSamples(beatsPerInterval, bpm, sampleRate);
}

/**
 * @brief Snap a sample position to the nearest grid line
 */
inline SampleIndex snapToGrid(SampleIndex position, GridSubdivision subdivision, 
                              double bpm, double sampleRate) {
    if (subdivision == GridSubdivision::None) return position;
    
    SampleIndex interval = getGridInterval(subdivision, bpm, sampleRate);
    if (interval <= 0) return position;
    
    // Round to nearest multiple of interval
    SampleIndex halfInterval = interval / 2;
    return ((position + halfInterval) / interval) * interval;
}

/**
 * @brief Snap a sample position to the grid floor (previous grid line)
 */
inline SampleIndex snapToGridFloor(SampleIndex position, GridSubdivision subdivision,
                                   double bpm, double sampleRate) {
    if (subdivision == GridSubdivision::None) return position;
    
    SampleIndex interval = getGridInterval(subdivision, bpm, sampleRate);
    if (interval <= 0) return position;
    
    return (position / interval) * interval;
}

/**
 * @brief Snap a sample position to the grid ceiling (next grid line)
 */
inline SampleIndex snapToGridCeil(SampleIndex position, GridSubdivision subdivision,
                                  double bpm, double sampleRate) {
    if (subdivision == GridSubdivision::None) return position;
    
    SampleIndex interval = getGridInterval(subdivision, bpm, sampleRate);
    if (interval <= 0) return position;
    
    return ((position + interval - 1) / interval) * interval;
}

} // namespace Audio
} // namespace Nomad
