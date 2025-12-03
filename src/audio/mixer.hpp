/**
 * @file mixer.hpp
 * @brief Mixer/Bus system - Migration wrapper
 * 
 * Wraps your existing MixerBus.h implementation.
 */

#pragma once

// Include YOUR existing mixer implementation
#include "NomadAudio/include/MixerBus.h"

namespace nomad::audio {

//=============================================================================
// Re-export existing types in new namespace  
//=============================================================================

using MixerBus = ::Nomad::Audio::MixerBus;

// Export any additional mixer-related types from your implementation

} // namespace nomad::audio
