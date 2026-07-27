// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUUID.h"
#include "PatternSource.h" // For PatternID

#include <cstdint>
#include <functional>
#include <string>

namespace Aestra {
namespace Audio {

/** New audio clips start below unity to preserve useful summing headroom. */
constexpr float DEFAULT_AUDIO_CLIP_GAIN_DB = -5.0f;
constexpr float DEFAULT_AUDIO_CLIP_GAIN_LINEAR = 0.56234133f;

/**
 * @brief Unique identifier for a clip instance
 */
struct ClipInstanceID : public AestraUUID {
    ClipInstanceID() = default;
    ClipInstanceID(const AestraUUID& other) : AestraUUID(other) {}

    static ClipInstanceID generate() { return ClipInstanceID(AestraUUID::generate()); }

    bool isValid() const { return low != 0 || high != 0; }

    /**
     * @brief Parse the toString() representation.
     * @return The parsed ID, or an invalid (zero) ID when the string doesn't
     *         parse — callers mint a fresh ID for invalid ones, which keeps
     *         old project files (without clip ids) loading fine while saved
     *         ids stay stable across load cycles (#446).
     */
    static ClipInstanceID fromString(const std::string& str) {
        AestraUUID parsed;
        if (AestraUUID::tryParse(str, parsed)) {
            return ClipInstanceID(parsed);
        }
        return ClipInstanceID();
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

    /**
     * Defaults for a newly created audio clip. The value-initialized defaults
     * above intentionally remain unity for legacy project fields and generic
     * clip construction, so loading an older project cannot change its mix.
     */
    static ClipEdits forNewAudioClip() {
        ClipEdits edits;
        edits.gain = DEFAULT_AUDIO_CLIP_GAIN_LINEAR;
        edits.gainLinear = DEFAULT_AUDIO_CLIP_GAIN_LINEAR;
        return edits;
    }
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
    double durationSeconds = 0.0; // Canonical duration for audio clips; MIDI clips remain beat-native.
    double sourceOffset = 0.0;        // Beat offset into source material.
    double sourceOffsetSeconds = 0.0; // Canonical audio-source offset; MIDI clips remain beat-native.

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
