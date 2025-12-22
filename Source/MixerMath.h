// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cmath>
#include <string>
#include <cstdio>

namespace Nomad {

/**
 * @brief Math utilities for mixer UI dB/linear conversions and formatting.
 *
 * Audio thread writes LINEAR peaks (0..1), UI converts to dB for display.
 * UI smoothing happens in dB space (looks nicer, more natural decay).
 *
 * Requirements: 8.1, 8.2 - Fader range from -infinity dB to +6 dB,
 * minimum value maps to ≤ -90 dB and displays "-∞".
 */
namespace MixerMath {

/// Minimum dB value (below this is treated as silence)
constexpr float DB_MIN = -90.0f;

/// Threshold below which we display "-∞"
constexpr float DB_SILENCE_THRESHOLD = -90.0f;

/// Maximum fader dB value
constexpr float DB_MAX = 6.0f;

/**
 * @brief Convert dB to linear amplitude.
 *
 * @param db Decibel value
 * @return Linear amplitude (0.0 for silence)
 */
inline float dbToLinear(float db) {
    if (db <= DB_SILENCE_THRESHOLD) return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

/**
 * @brief Convert linear amplitude to dB.
 *
 * @param linear Linear amplitude (0.0 to 1.0+)
 * @return Decibel value (clamped to DB_MIN for silence)
 */
inline float linearToDb(float linear) {
    if (linear <= 0.0f) return DB_MIN;
    float db = 20.0f * std::log10(linear);
    return std::max(db, DB_MIN);
}

/**
 * @brief Format dB value for display.
 *
 * Shows "-∞" for values at or below silence threshold.
 * Otherwise shows value with one decimal place.
 *
 * @param db Decibel value
 * @return Formatted string (e.g., "-12.5", "0.0", "-∞")
 */
inline std::string formatDb(float db) {
    if (db <= DB_SILENCE_THRESHOLD) return "-\xE2\x88\x9E"; // UTF-8 infinity symbol
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", db);
    return buf;
}

/**
 * @brief Format dB value with "dB" suffix for display.
 *
 * @param db Decibel value
 * @return Formatted string with suffix (e.g., "-12.5 dB", "0.0 dB", "-∞ dB")
 */
inline std::string formatDbWithSuffix(float db) {
    return formatDb(db) + " dB";
}

/**
 * @brief Clamp dB value to valid fader range.
 *
 * @param db Decibel value
 * @return Clamped value between DB_MIN and DB_MAX
 */
inline float clampDb(float db) {
    return std::max(DB_MIN, std::min(DB_MAX, db));
}

} // namespace MixerMath
} // namespace Nomad
