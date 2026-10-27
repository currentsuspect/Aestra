// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioDriver.h"
#include "AudioDriverTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace Aestra {
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

    /**
     * @brief Let go of a stream that has stopped making progress, without waiting on it.
     *
     * A backend can wedge inside its own write (PulseAudio's pa_simple_write
     * waits forever when the graph it feeds stops being clocked) while holding
     * the lock stopStream() needs, so a plain stop never returns. A driver that
     * can do better overrides this to hand the wedged stream to a background
     * thread and return at once, ready to open a new stream; the abandoned
     * stream must never call the client callback again. The default is the
     * ordinary stop + close, for drivers that cannot wedge this way.
     */
    virtual void abandonStream() {
        stopStream();
        closeStream();
    }

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
} // namespace Aestra
