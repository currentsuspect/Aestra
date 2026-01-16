// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "IAudioDriver.h"
#include "AudioDriverTypes.h"
#include <string>
#include <vector>
#include <functional>

namespace Nomad {
namespace Audio {

// Helper for type-to-string conversion


/**
 * @brief Base class for native drivers (WASAPI, ASIO)
 * Extends IAudioDriver with additional diagnostics and state tracking.
 */
class NativeAudioDriver : public IAudioDriver {
public:
    virtual ~NativeAudioDriver() = default;

    // IAudioDriver interface is inherited
    
    // Extended Native Interface
    
    /**
     * @brief Get driver capabilities
     */
    virtual DriverCapability getCapabilities() const = 0;

    /**
     * @brief Get current driver state
     */
    virtual DriverState getState() const = 0;

    /**
     * @brief Get last error
     */
    virtual DriverError getLastError() const = 0;
    
    // IAudioDriver overrides that we want to enforce or extend
    // Note: getErrorMessage, getStatistics are already in IAudioDriver
    
    /**
     * @brief Reset driver statistics
     */
    virtual void resetStatistics() = 0;

    /**
     * @brief Initialize driver
     * @return true if initialization succeeded
     */
    virtual bool initialize() = 0;

    /**
     * @brief Shutdown driver
     */
    virtual void shutdown() = 0;
    
    // Note: isAvailable is in IAudioDriver
    
    // Dithering defaults (can be overridden by specific drivers like WASAPI Shared)
    virtual void setDitheringEnabled(bool enabled) override {}
    virtual bool isDitheringEnabled() const override { return false; }

    /**
     * @brief Get typical latency for this driver type
     */
    virtual float getTypicalLatencyMs() const = 0;

    /**
     * @brief Set error callback for logging
     */
    using ErrorCallback = std::function<void(DriverError, const std::string&)>;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
};

} // namespace Audio
} // namespace Nomad
