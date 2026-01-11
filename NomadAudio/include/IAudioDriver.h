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

    // Device Enumeration
    virtual std::vector<AudioDeviceInfo> getDevices() const = 0;

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

    // Capabilities (Optional)
    virtual bool supportsExclusiveMode() const { return false; }

    /**
     * @brief Poll for deferred errors that occurred in real-time threads.
     * @param[out] outError The error code.
     * @param[out] outMsg The constructed error message.
     * @return true if an error was retrieved, false otherwise.
     */
    virtual bool pollDeferredError(DriverError& outError, std::string& outMsg) { 
        (void)outError; (void)outMsg;
        return false; 
    }
};

} // namespace Audio
} // namespace Nomad
