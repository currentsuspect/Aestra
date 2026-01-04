// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioDriver.h"
#include "AudioDriverTypes.h"
#include <string>
#include <vector>
#include <functional>

namespace Nomad {
namespace Audio {

// =============================================================================
// Audio Driver Interface (Platform Neutral)
// =============================================================================

class IAudioDriver {
public:
    virtual ~IAudioDriver() = default;

    // Driver Information
    virtual std::string getDisplayName() const = 0;
    virtual AudioDriverType getDriverType() const = 0;
    virtual bool isAvailable() const = 0;

    /**
     * @brief Get list of available output devices
     */
    virtual std::vector<AudioDeviceInfo> getDevices() = 0;

    // Stream Management
    virtual bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) = 0;
    virtual void closeStream() = 0;
    virtual bool startStream() = 0;
    virtual void stopStream() = 0;

    // Stream State
    virtual bool isStreamRunning() const = 0;
    virtual double getStreamLatency() const = 0; // In seconds
    virtual uint32_t getStreamSampleRate() const = 0;
    virtual uint32_t getStreamBufferSize() const = 0;
    virtual DriverStatistics getStatistics() const = 0;
    virtual std::string getErrorMessage() const = 0;

    /**
     * @brief Enable/Disable dithering for output
     * 
     * Dithering mitigates quantization distortion when converting float audio
     * to lower bit-depths (e.g. 16-bit or 24-bit integer) for the hardware.
     */
    virtual void setDitheringEnabled(bool enabled) = 0;

    /**
     * @brief Check if dithering is enabled
     */
    virtual bool isDitheringEnabled() const = 0;

    // Capabilities (Optional)
    virtual bool supportsExclusiveMode() const { return false; }
};

} // namespace Audio
} // namespace Nomad
