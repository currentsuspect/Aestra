// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TimeTypes.h"
#include "ClipSource.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <random>
#include <string>

namespace Nomad {
namespace Audio {

// =============================================================================
// PlaylistClipID - Unique clip identity
// =============================================================================

/**
 * @brief Unique identifier for a playlist clip
 * 
 * Uses a 64-bit UUID-style value for stable identity across sessions.
 */
struct PlaylistClipID {
    uint64_t high = 0;
    uint64_t low = 0;
    
    bool isValid() const { return high != 0 || low != 0; }
    bool operator==(const PlaylistClipID& other) const { 
        return high == other.high && low == other.low; 
    }
    bool operator!=(const PlaylistClipID& other) const { return !(*this == other); }
    bool operator<(const PlaylistClipID& other) const { 
        return high < other.high || (high == other.high && low < other.low); 
    }
    
    std::string toString() const {
        char buf[37];
        snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                 static_cast<uint32_t>(high >> 32),
                 static_cast<uint16_t>(high >> 16),
                 static_cast<uint16_t>(high),
                 static_cast<uint16_t>(low >> 48),
                 static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFULL));
        return std::string(buf);
    }
    
    static PlaylistClipID generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        
        PlaylistClipID id;
        id.high = dis(gen);
        id.low = dis(gen);
        // Set version 4 (random) and variant bits
        id.high = (id.high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        id.low = (id.low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
        return id;
    }
    
    static PlaylistClipID fromString(const std::string& str) {
        PlaylistClipID id;
        if (str.length() >= 36) {
            unsigned int a, b, c, d;
            unsigned long long e;
            if (sscanf(str.c_str(), "%8x-%4x-%4x-%4x-%12llx", &a, &b, &c, &d, &e) == 5) {
                id.high = (static_cast<uint64_t>(a) << 32) | 
                         (static_cast<uint64_t>(b) << 16) | 
                         static_cast<uint64_t>(c);
                id.low = (static_cast<uint64_t>(d) << 48) | e;
            }
        }
        return id;
    }
    
    struct Hash {
        size_t operator()(const PlaylistClipID& id) const {
            return std::hash<uint64_t>()(id.high) ^ 
                   (std::hash<uint64_t>()(id.low) << 1);
        }
    };
};

// =============================================================================
// ClipFlags - Bit flags for clip properties
// =============================================================================

namespace ClipFlags {
    constexpr uint32_t None        = 0;
    constexpr uint32_t Reversed    = 1 << 0;   // Play in reverse
    constexpr uint32_t Looping     = 1 << 1;   // Loop within bounds
    constexpr uint32_t Locked      = 1 << 2;   // Prevent editing
    constexpr uint32_t FadeIn      = 1 << 3;   // Has fade in
    constexpr uint32_t FadeOut     = 1 << 4;   // Has fade out
    constexpr uint32_t Selected    = 1 << 5;   // Currently selected (UI state)
}

// =============================================================================
// PlaylistClip - Atomic playlist object
// =============================================================================

/**
 * @brief A single audio clip on the timeline
 * 
 * The PlaylistClip is the atomic unit of the playlist system:
 * - Represents one region of one audio source on the timeline
 * - Uses sample-based timing for precision
 * - Non-destructive: references source audio, doesn't modify it
 * - Supports trim, gain, pan, and future features (fades, time-stretch)
 * 
 * Key timing concepts:
 * - startTime: where the clip begins on the project timeline (samples)
 * - length: how long the clip plays on the timeline (samples)
 * - sourceStart: offset into the source audio file (samples at source rate)
 * 
 * All timeline values are at PROJECT sample rate.
 * Source offset is at SOURCE sample rate.
 */
struct PlaylistClip {
    // === Identity ===
    PlaylistClipID id;
    
    // === Source Reference ===
    ClipSourceID sourceId;                     ///< Reference to audio source
    
    // === Timeline Position (project sample rate) ===
    SampleIndex startTime = 0;                 ///< Where clip starts on timeline
    SampleIndex length = 0;                    ///< Duration on timeline after trim
    
    // === Source Offset (source sample rate) ===
    SampleIndex sourceStart = 0;               ///< Starting sample in source audio
    // Note: source end is implied as sourceStart + (length * sourceRate/projectRate)
    
    // === Playback Properties ===
    float gainLinear = 1.0f;                   ///< Linear gain [0.0, 2.0]
    float pan = 0.0f;                          ///< Pan position [-1.0 = L, 0 = C, 1.0 = R]
    bool muted = false;                        ///< Skip during playback
    
    // === Time-Stretch / SRC ===
    double playbackRate = 1.0;                 ///< Rate multiplier (1.0 = normal)
    // Note: actual SRC ratio may differ if source rate != project rate
    
    // === Fades (in samples) ===
    SampleIndex fadeInLength = 0;              ///< Fade-in duration
    SampleIndex fadeOutLength = 0;             ///< Fade-out duration
    
    // === Flags ===
    uint32_t flags = ClipFlags::None;
    
    // === UI / Metadata ===
    uint32_t colorRGBA = 0xFF4A90D9;           ///< Display color (ARGB)
    std::string name;                          ///< Display name
    
    // === Constructors ===
    PlaylistClip() : id(PlaylistClipID::generate()) {}
    
    explicit PlaylistClip(ClipSourceID source) 
        : id(PlaylistClipID::generate())
        , sourceId(source) {}
    
    // === Computed Properties ===
    
    /// Get the end position on timeline
    SampleIndex getEndTime() const { return startTime + length; }
    
    /// Get the timeline range
    SampleRange getTimelineRange() const { return SampleRange(startTime, getEndTime()); }
    
    /// Check if clip is valid
    bool isValid() const { return id.isValid() && sourceId.isValid() && length > 0; }
    
    /// Check if timeline position falls within this clip
    bool containsTime(SampleIndex time) const {
        return time >= startTime && time < getEndTime();
    }
    
    /// Check if this clip overlaps a time range
    bool overlapsRange(SampleIndex rangeStart, SampleIndex rangeEnd) const {
        return !(getEndTime() <= rangeStart || startTime >= rangeEnd);
    }
    
    bool overlapsRange(const SampleRange& range) const {
        return overlapsRange(range.start, range.end);
    }
    
    /// Convert timeline position to source position (at source sample rate)
    SampleIndex timelineToSource(SampleIndex timelinePos, double projectRate, double sourceRate) const {
        if (projectRate <= 0 || sourceRate <= 0) return sourceStart;
        
        SampleIndex offsetFromClipStart = timelinePos - startTime;
        if (offsetFromClipStart < 0) return sourceStart;
        if (offsetFromClipStart >= length) return sourceStart + static_cast<SampleIndex>(length * sourceRate / projectRate);
        
        double ratio = sourceRate / projectRate;
        return sourceStart + static_cast<SampleIndex>(offsetFromClipStart * ratio * playbackRate);
    }
    
    /// Calculate gain at a specific position (including fades)
    float getGainAtPosition(SampleIndex timelinePos) const {
        if (!containsTime(timelinePos)) return 0.0f;
        
        SampleIndex offsetFromStart = timelinePos - startTime;
        SampleIndex offsetFromEnd = getEndTime() - timelinePos;
        
        float fadeGain = 1.0f;
        
        // Fade in
        if (fadeInLength > 0 && offsetFromStart < fadeInLength) {
            fadeGain *= static_cast<float>(offsetFromStart) / static_cast<float>(fadeInLength);
        }
        
        // Fade out
        if (fadeOutLength > 0 && offsetFromEnd < fadeOutLength) {
            fadeGain *= static_cast<float>(offsetFromEnd) / static_cast<float>(fadeOutLength);
        }
        
        return gainLinear * fadeGain;
    }
    
    // === Flag Helpers ===
    bool isReversed() const { return flags & ClipFlags::Reversed; }
    bool isLooping() const { return flags & ClipFlags::Looping; }
    bool isLocked() const { return flags & ClipFlags::Locked; }
    bool hasFadeIn() const { return flags & ClipFlags::FadeIn; }
    bool hasFadeOut() const { return flags & ClipFlags::FadeOut; }
    
    void setReversed(bool v) { if (v) flags |= ClipFlags::Reversed; else flags &= ~ClipFlags::Reversed; }
    void setLooping(bool v) { if (v) flags |= ClipFlags::Looping; else flags &= ~ClipFlags::Looping; }
    void setLocked(bool v) { if (v) flags |= ClipFlags::Locked; else flags &= ~ClipFlags::Locked; }
};

// =============================================================================
// PlaylistClip Comparison Functions (for sorting)
// =============================================================================

/// Compare clips by start time (for timeline ordering)
inline bool compareByStartTime(const PlaylistClip& a, const PlaylistClip& b) {
    return a.startTime < b.startTime;
}

/// Compare clip pointers by start time
inline bool compareByStartTimePtr(const PlaylistClip* a, const PlaylistClip* b) {
    if (!a) return false;
    if (!b) return true;
    return a->startTime < b->startTime;
}

} // namespace Audio
} // namespace Nomad
