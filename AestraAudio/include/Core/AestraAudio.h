// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file AestraAudio.h
 * @brief Main header for AestraAudio library
 * 
 * AestraAudio provides real-time cross-platform audio I/O powered by RtAudio.
 * 
 * Features:
 * - Low-latency audio streaming (<10ms)
 * - Cross-platform device management
 * - Lock-free audio callbacks
 * - Simple API for audio applications
 * 
 * Supported APIs:
 * - Windows: WASAPI, DirectSound
 * - macOS: CoreAudio
 * - Linux: ALSA, JACK
 * 
 * @version 1.1.0
 * @license MIT
 */

#include "AudioDriver.h"
#include "AudioDeviceManager.h"
#include "AudioProcessor.h"
#include "MixerBus.h"
#include "Oscillator.h"
#include "MixerChannel.h"
#include "TrackManager.h"


namespace Aestra {
namespace Audio {

/**
 * @brief Get AestraAudio version string
 */
const char* getVersion();

/**
 * @brief Get RtAudio backend name
 */
const char* getBackendName();

} // namespace Audio
} // namespace Aestra
