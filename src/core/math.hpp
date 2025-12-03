/**
 * @file math.hpp
 * @brief Math utilities - Migration wrapper
 * 
 * Wraps existing NomadMath.h and re-exports in new namespace.
 */

#pragma once

// Include the existing implementation  
#include "NomadCore/include/NomadMath.h"

namespace nomad::math {

//=============================================================================
// Re-export existing utilities
//=============================================================================

// Pull in all existing Nomad math utilities
using namespace ::Nomad::Math;

//=============================================================================
// Constants (ensure consistency)
//=============================================================================

constexpr float PI_F = 3.14159265358979323846f;
constexpr double PI_D = 3.14159265358979323846;
constexpr float TWO_PI_F = PI_F * 2.0f;
constexpr double TWO_PI_D = PI_D * 2.0;
constexpr float HALF_PI_F = PI_F * 0.5f;

//=============================================================================
// Audio-specific math (extensions)
//=============================================================================

/// Convert decibels to linear gain
inline constexpr float dbToLinear(float db) {
    return (db <= -96.0f) ? 0.0f : std::pow(10.0f, db / 20.0f);
}

/// Convert linear gain to decibels
inline constexpr float linearToDb(float linear) {
    return (linear <= 0.0f) ? -96.0f : 20.0f * std::log10(linear);
}

/// Convert MIDI note to frequency (A4 = 440Hz)
inline constexpr float midiToFreq(float note, float tuning = 440.0f) {
    return tuning * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

/// Convert frequency to MIDI note
inline constexpr float freqToMidi(float freq, float tuning = 440.0f) {
    return 69.0f + 12.0f * std::log2(freq / tuning);
}

/// Clamp value to range
template<typename T>
inline constexpr T clamp(T value, T min, T max) {
    return (value < min) ? min : (value > max) ? max : value;
}

/// Linear interpolation
template<typename T>
inline constexpr T lerp(T a, T b, T t) {
    return a + t * (b - a);
}

/// Normalize value from one range to another
inline constexpr float normalize(float value, float inMin, float inMax, 
                                  float outMin = 0.0f, float outMax = 1.0f) {
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}

} // namespace nomad::math
