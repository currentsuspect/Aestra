// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUUID.h"
#include "PatternSource.h" // For PatternID

#include <cstdint>
#include <functional>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Unique identifier for a clip instance
 */
struct ClipInstanceID : public AestraUUID {
    ClipInstanceID() = default;
    ClipInstanceID(const AestraUUID& other) : AestraUUID(other) {}

    static ClipInstanceID generate() { return ClipInstanceID(AestraUUID::generate()); }

    bool isValid() const { return low != 0 || high != 0; }

    /**
     * @brief Parse from string (stub - just returns generate())
     */
    static ClipInstanceID fromString(const std::string& str) {
        // Stub implementation - in real code would parse UUID string
        (void)str;
        return generate();
    }
};

/**
 * @brief Unique identifier for a playlist lane (track row)
 */
struct PlaylistLaneID : public AestraUUID {
    PlaylistLaneID() = default;
    PlaylistLaneID(const AestraUUID& other) : AestraUUID(other) {}

    static PlaylistLaneID generate() { return PlaylistLaneID(AestraUUID::generate()); }

    bool isValid() const { return low != 0 || high != 0; }
};

/**
 * @brief Clip edits (user modifications to clip content)
 */
struct ClipEdits {
    float fadeInBeats = 0.0f;
    float fadeOutBeats = 0.0f;
    float gain = 1.0f;
    float gainLinear = 1.0f; // Alternative name for gain
    float pitchSemitones = 0.0f;
    double timeStretchRatio = 1.0;
    float pan = 0.0f;
    bool muted = false;
    float playbackRate = 1.0f;
    double sourceStart = 0.0;
};

/**
 * @brief Represents a single clip instance on the playlist
 */
struct ClipInstance {
    ClipInstanceID id;
    std::string name;               // Clip name
    PatternID patternId;            // Pattern ID for pattern clips
    uint32_t colorRGBA{0xFFFFFFFF}; // Clip color
    double startBeat = 0.0;
    double durationBeats = 4.0;
    double sourceOffset = 0.0; // Offset into source material

    ClipEdits edits;

    // Source reference (audio file, pattern, etc.)
    uint64_t sourceId = 0;

    ClipInstance() = default;

    double endBeat() const { return startBeat + durationBeats; }

    /**
     * @brief Check if a beat position is within this clip
     */
    bool containsBeat(double beat) const { return beat >= startBeat && beat < endBeat(); }
};

} // namespace Audio
} // namespace Aestra

// Hash specializations for std::unordered_map
namespace std {
template <> struct hash<Aestra::Audio::ClipInstanceID> {
    size_t operator()(const Aestra::Audio::ClipInstanceID& id) const noexcept {
        return hash<Aestra::Audio::AestraUUID>()(id);
    }
};

template <> struct hash<Aestra::Audio::PlaylistLaneID> {
    size_t operator()(const Aestra::Audio::PlaylistLaneID& id) const noexcept {
        return hash<Aestra::Audio::AestraUUID>()(id);
    }
};
} // namespace std
