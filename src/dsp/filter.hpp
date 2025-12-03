/**
 * @file filter.hpp
 * @brief Filter DSP - Migration wrapper
 * 
 * Wraps your existing 409-line Filter.h implementation.
 * Your ZDF, oversampling, and saturation code stays intact.
 */

#pragma once

// Include YOUR existing filter implementation
#include "NomadAudio/include/Filter.h"

namespace nomad::dsp {

//=============================================================================
// Re-export existing types in new namespace
//=============================================================================

using FilterType = ::Nomad::Audio::DSP::FilterType;
using FilterSlope = ::Nomad::Audio::DSP::FilterSlope;
using OversamplingFactor = ::Nomad::Audio::DSP::OversamplingFactor;
using SaturationType = ::Nomad::Audio::DSP::SaturationType;
using SmoothingType = ::Nomad::Audio::DSP::SmoothingType;

// The main filter classes
using FilterParams = ::Nomad::Audio::DSP::FilterParams;
using BiquadCoeffs = ::Nomad::Audio::DSP::BiquadCoeffs;
using BiquadState = ::Nomad::Audio::DSP::BiquadState;
using BiquadFilter = ::Nomad::Audio::DSP::BiquadFilter;
using Filter = ::Nomad::Audio::DSP::Filter;

//=============================================================================
// Factory functions for common filter configurations
//=============================================================================

/// Create a simple low-pass filter
inline Filter createLowPass(float sampleRate, float cutoff, float resonance = 0.707f) {
    Filter f;
    FilterParams params;
    params.type = FilterType::LowPass;
    params.frequency = cutoff;
    params.resonance = resonance;
    f.prepare(sampleRate);
    f.setParams(params);
    return f;
}

/// Create a simple high-pass filter
inline Filter createHighPass(float sampleRate, float cutoff, float resonance = 0.707f) {
    Filter f;
    FilterParams params;
    params.type = FilterType::HighPass;
    params.frequency = cutoff;
    params.resonance = resonance;
    f.prepare(sampleRate);
    f.setParams(params);
    return f;
}

/// Create a band-pass filter
inline Filter createBandPass(float sampleRate, float cutoff, float bandwidth = 1.0f) {
    Filter f;
    FilterParams params;
    params.type = FilterType::BandPass;
    params.frequency = cutoff;
    params.resonance = bandwidth;
    f.prepare(sampleRate);
    f.setParams(params);
    return f;
}

/// Create a peak/bell EQ filter
inline Filter createPeakEQ(float sampleRate, float freq, float gainDb, float q = 1.0f) {
    Filter f;
    FilterParams params;
    params.type = FilterType::Peak;
    params.frequency = freq;
    params.resonance = q;
    params.gain = gainDb;
    f.prepare(sampleRate);
    f.setParams(params);
    return f;
}

} // namespace nomad::dsp
