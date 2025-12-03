/**
 * @file device.hpp
 * @brief Audio device management - Migration wrapper
 * 
 * Wraps your existing AudioDeviceManager, AudioDriver,
 * WASAPI, and ASIO implementations.
 */

#pragma once

// Include YOUR existing audio device implementations
#include "NomadAudio/include/AudioDeviceManager.h"
#include "NomadAudio/include/AudioDriver.h"
#include "NomadAudio/include/AudioDriverTypes.h"
#include "NomadAudio/include/NativeAudioDriver.h"

// Platform-specific drivers
#ifdef _WIN32
#include "NomadAudio/include/WASAPISharedDriver.h"
#include "NomadAudio/include/WASAPIExclusiveDriver.h"
#include "NomadAudio/include/ASIODriverInfo.h"
#endif

namespace nomad::audio {

//=============================================================================
// Re-export existing types in new namespace
//=============================================================================

// Core device types
using AudioDeviceManager = ::Nomad::Audio::AudioDeviceManager;
using AudioDriver = ::Nomad::Audio::AudioDriver;
using AudioDriverType = ::Nomad::Audio::AudioDriverType;
using AudioDeviceInfo = ::Nomad::Audio::AudioDeviceInfo;

// Callback types
using AudioCallback = ::Nomad::Audio::AudioCallback;

#ifdef _WIN32
// Windows-specific drivers
using WASAPISharedDriver = ::Nomad::Audio::WASAPISharedDriver;
using WASAPIExclusiveDriver = ::Nomad::Audio::WASAPIExclusiveDriver;
using ASIODriverInfo = ::Nomad::Audio::ASIODriverInfo;
#endif

//=============================================================================
// Device management helpers
//=============================================================================

/// Get the global audio device manager
inline AudioDeviceManager& getDeviceManager() {
    return ::Nomad::Audio::AudioDeviceManager::getInstance();
}

/// Get list of available audio devices
inline std::vector<AudioDeviceInfo> getAvailableDevices() {
    return getDeviceManager().getAvailableDevices();
}

/// Get current sample rate
inline double getSampleRate() {
    return getDeviceManager().getSampleRate();
}

/// Get current buffer size
inline int getBufferSize() {
    return getDeviceManager().getBufferSize();
}

} // namespace nomad::audio
