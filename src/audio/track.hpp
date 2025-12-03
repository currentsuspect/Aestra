/**
 * @file track.hpp
 * @brief Audio track - Migration wrapper
 * 
 * Wraps your existing 335-line Track.h with all its
 * resampling modes, dithering, and precision options.
 */

#pragma once

// Include YOUR existing track implementation
#include "NomadAudio/include/Track.h"
#include "NomadAudio/include/TrackManager.h"

namespace nomad::audio {

//=============================================================================
// Re-export existing types in new namespace
//=============================================================================

// Track states and enums
using TrackState = ::Nomad::Audio::TrackState;
using InterpolationQuality = ::Nomad::Audio::InterpolationQuality;
using ResamplingMode = ::Nomad::Audio::ResamplingMode;
using DitheringMode = ::Nomad::Audio::DitheringMode;
using InternalPrecision = ::Nomad::Audio::InternalPrecision;
using OversamplingMode = ::Nomad::Audio::OversamplingMode;

// Main classes
using Track = ::Nomad::Audio::Track;
using TrackManager = ::Nomad::Audio::TrackManager;

//=============================================================================
// Track creation helpers
//=============================================================================

/// Create a new audio track with default settings
inline std::shared_ptr<Track> createAudioTrack(const std::string& name = "Audio") {
    auto track = std::make_shared<Track>();
    track->setName(name);
    return track;
}

/// Get the global track manager instance
inline TrackManager& getTrackManager() {
    return ::Nomad::Audio::TrackManager::getInstance();
}

} // namespace nomad::audio
