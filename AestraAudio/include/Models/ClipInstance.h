// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUUID.h"
#include "PatternSource.h" // For PatternID

#include <algorithm>
#include <cmath>
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
    static constexpr float kMinPitchSemitones = -24.0f;
    static constexpr float kMaxPitchSemitones = 24.0f;

    float fadeInBeats = 0.0f;
    float fadeOutBeats = 0.0f;
    /** Clip level, linear. The only gain field: the render path, the editor
     *  display and the serializer all read this one. */
    float gainLinear = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    float playbackRate = 1.0f;
    /** Musical pitch offset in semitones, independent of Speed. Rendered as
     *  2^(st/12) folded into the same varispeed ratio as playbackRate; pitch
     *  up plays faster/shorter like any varispeed (#746). */
    float pitchSemitones = 0.0f;
    double sourceStart = 0.0;

    /** Musical varispeed control used by the Audio Clip Editor. Pitch and
     *  duration move together because playbackRate is the render authority;
     *  this does not claim independent time-stretching. */
    static float playbackRateFromSemitones(float semitones) noexcept {
        if (!std::isfinite(semitones))
            return 1.0f;
        const float clamped = std::clamp(semitones, kMinPitchSemitones, kMaxPitchSemitones);
        return std::pow(2.0f, clamped / 12.0f);
    }

    static float semitonesFromPlaybackRate(float playbackRate) noexcept {
        if (!std::isfinite(playbackRate) || playbackRate <= 0.0f)
            return 0.0f;
        const float semitones = 12.0f * std::log2(playbackRate);
        return std::clamp(semitones, kMinPitchSemitones, kMaxPitchSemitones);
    }

    /**
     * Defaults for a newly created audio clip. The value-initialized defaults
     * above intentionally remain unity for legacy project fields and generic
     * clip construction, so loading an older project cannot change its mix.
     */
    static ClipEdits forNewAudioClip() {
        ClipEdits edits;
        edits.gainLinear = DEFAULT_AUDIO_CLIP_GAIN_LINEAR;
        return edits;
    }

    /**
     * Compares every field. Callers deciding whether an edit needs persisting
     * must not hand-pick fields: a check that named only gain and the fades
     * silently dropped any other baked change.
     */
    bool operator==(const ClipEdits& other) const {
        return fadeInBeats == other.fadeInBeats && fadeOutBeats == other.fadeOutBeats &&
               gainLinear == other.gainLinear && pan == other.pan && muted == other.muted &&
               playbackRate == other.playbackRate && pitchSemitones == other.pitchSemitones &&
               sourceStart == other.sourceStart;
    }
    bool operator!=(const ClipEdits& other) const { return !(*this == other); }
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
