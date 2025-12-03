/**
 * @file oscillator.hpp
 * @brief Oscillator DSP - Migration wrapper
 * 
 * Wraps your existing Oscillator.h implementation.
 */

#pragma once

// Include YOUR existing oscillator implementation
#include "NomadAudio/include/Oscillator.h"

namespace nomad::dsp {

//=============================================================================
// Re-export existing types in new namespace
//=============================================================================

using OscillatorType = ::Nomad::Audio::DSP::WaveformType;
using Oscillator = ::Nomad::Audio::DSP::Oscillator;

// If your oscillator has additional types, export them here

} // namespace nomad::dsp
