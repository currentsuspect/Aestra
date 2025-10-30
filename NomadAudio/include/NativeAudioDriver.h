// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioDriver.h"
#include "AudioDriverTypes.h"
#include <memory>
#include <functional>
#include <string>

namespace Nomad {
namespace Audio {

/**
 * @brief Extended audio driver interface with driver type awareness
 * 
 * Base class for all native NOMAD audio driver implementations
 */
class NativeAudioDriver : public AudioDriver {
public:
    virtual ~NativeAudioDriver() = default;

    /**
     * @brief Get driver type
     */
    virtual AudioDriverType getDriverType() const = 0;

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

    /**
     * @brief Get error message
     */
    virtual std::string getErrorMessage() const = 0;

    /**
     * @brief Get driver statistics
     */
    virtual DriverStatistics getStatistics() const = 0;

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

    /**
     * @brief Check if driver is available on this system
     */
    virtual bool isAvailable() const = 0;

    /**
     * @brief Get driver display name
     */
    virtual const char* getDisplayName() const {
        return DriverTypeToString(getDriverType());
    }

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
