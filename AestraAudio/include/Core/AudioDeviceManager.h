// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Drivers/AudioDriverTypes.h"
#include "../Drivers/IAudioDriver.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Manages audio devices and streams
 *
 * Provides high-level interface for audio I/O:
 * - Device enumeration and selection
 * - Stream configuration
 * - Callback management
 */
class AudioDeviceManager {
public:
    /** @brief Construct the audio device manager. */
    AudioDeviceManager();
    /** @brief Shut down drivers and release active stream resources. */
    ~AudioDeviceManager();

    /**
     * @brief Initialize audio system
     * @param registerPlatformDrivers When true (default), registers the platform
     *        audio backends. Pass false to initialize using only drivers already
     *        added via addDriver() — used by dependency-injection tests that must
     *        run against a controlled driver set with no real hardware.
     * @return True when at least one driver backend was initialized successfully.
     */
    bool initialize(bool registerPlatformDrivers = true);

    /**
     * @brief Shutdown audio system
     */
    void shutdown();

    /**
     * @brief Get available audio devices
     * @return Enumerated audio devices for the active backend.
     */
    std::vector<AudioDeviceInfo> getDevices() const;

    /**
     * @brief Get default output device
     * @return Default output device descriptor.
     */
    AudioDeviceInfo getDefaultOutputDevice() const;

    /**
     * @brief Get default input device
     * @return Default input device descriptor.
     */
    AudioDeviceInfo getDefaultInputDevice() const;

    /**
     * @brief Open audio stream with configuration
     * @param config Requested stream configuration.
     * @param callback Audio callback invoked by the backend.
     * @param userData User data forwarded to the callback.
     * @return True when the stream opened successfully.
     */
    bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData);

    /**
     * @brief Close current audio stream
     */
    void closeStream();

    /**
     * @brief Start audio processing
     * @return True when the stream started successfully.
     */
    bool startStream();

    /**
     * @brief Stop audio processing
     */
    void stopStream();

    /**
     * @brief Check if stream is active
     * @return True while the backend stream is running.
     */
    bool isStreamRunning() const;

    /**
     * @brief Get current stream latency
     * @return Stream latency in seconds.
     */
    double getStreamLatency() const;

    /**
     * @brief Get the actual stream sample rate (post-backend)
     *
     * Returns 0 if no active stream.
     */
    uint32_t getStreamSampleRate() const;

    /**
     * @brief Get the actual stream buffer size (post-backend)
     *
     * Returns 0 if no active stream.
     */
    uint32_t getStreamBufferSize() const;

    /**
     * @brief Get latency compensation values for recording
     * @param[out] inputLatencyMs Input latency in milliseconds
     * @param[out] outputLatencyMs Output latency in milliseconds
     *
     * Call this after opening a stream to get latency compensation values.
     * These should be passed to Track::setLatencyCompensation() for recording.
     */
    void getLatencyCompensationValues(double& inputLatencyMs, double& outputLatencyMs) const;

    /**
     * @brief Get current configuration
     * @return A value snapshot of the current stream configuration. Never a
     *         reference into mutable shared state — the returned copy stays
     *         coherent even as another thread reconfigures the stream (#391).
     */
    AudioStreamConfig getCurrentConfig() const;

    /**
     * @brief Switch to a different audio device
     * @param deviceId New device ID to switch to
     * @return true if device switch was successful
     *
     * This will stop the current stream, reconfigure with the new device,
     * and restart the stream if it was running.
     */
    bool switchDevice(uint32_t deviceId);

    /**
     * @brief Switch to a different input device while preserving the current output device.
     */
    bool switchInputDevice(uint32_t deviceId);

    /**
     * @brief Update sample rate
     * @param sampleRate New sample rate in Hz
     * @return true if sample rate was updated successfully
     *
     * This will restart the stream with the new sample rate.
     */
    bool setSampleRate(uint32_t sampleRate);

    /**
     * @brief Update buffer size
     * @param bufferSize New buffer size in frames
     * @return true if buffer size was updated successfully
     *
     * This will restart the stream with the new buffer size.
     */
    bool setBufferSize(uint32_t bufferSize);

    /**
     * @brief Validate if a device supports the given configuration
     * @param deviceId Device ID to validate
     * @param sampleRate Desired sample rate
     * @return true if the device supports the configuration
     */
    bool validateDeviceConfig(uint32_t deviceId, uint32_t sampleRate) const;

    /**
     * @brief Get active driver type
     * @return Driver type currently servicing the stream.
     */
    AudioDriverType getActiveDriverType() const;

    /**
     * @brief Set preferred driver type
     * @param type Preferred driver type (WASAPI_EXCLUSIVE or WASAPI_SHARED)
     * @return true if driver type was set successfully
     *
     * This will attempt to reopen the stream with the specified driver.
     * Falls back to next best driver if the preferred one fails.
     */
    bool setPreferredDriverType(AudioDriverType type);

    /**
     * @brief Check if a specific driver type is currently available
     * @param type Driver type to check
     * @return true if the driver can be opened (not blocked by another app)
     *
     * This performs a quick test-open to check availability without changing
     * the active stream. Useful for UI to grey out unavailable options.
     */
    bool isDriverTypeAvailable(AudioDriverType type) const;

    /**
     * @brief Get list of available driver types
     * @return Driver types available on the current platform.
     */
    std::vector<AudioDriverType> getAvailableDriverTypes() const;

    /**
     * @brief Check if we're using a fallback driver (not the preferred one)
     * @return true if active driver differs from preferred driver
     *
     * This is useful for showing warnings in the UI when Exclusive mode
     * was requested but Shared mode is active (e.g., due to conflicts).
     */
    bool isUsingFallbackDriver() const;

    /**
     * @brief Callback type for driver mode change notifications
     * @param preferredType The driver type that was requested
     * @param actualType The driver type that was actually used
     * @param reason Human-readable explanation of why fallback occurred
     */
    using DriverModeChangeCallback =
        std::function<void(AudioDriverType preferredType, AudioDriverType actualType, const std::string& reason)>;

    /**
     * @brief Set callback for driver mode changes (fallback notifications)
     * @param callback Function to call when driver mode changes
     *
     * This callback is invoked when:
     * - Exclusive mode was requested but Shared mode is used (conflict)
     * - Any automatic driver fallback occurs
     *
     * Use this to show info bars or notifications in the UI.
     */
    void setDriverModeChangeCallback(DriverModeChangeCallback callback);

    /**
     * @brief Get reason for current fallback (if any)
     * @return Human-readable reason, or empty string if using preferred driver
     */
    std::string getFallbackReason() const;

    /**
     * @brief Add a driver to the manager (Dependency Injection)
     * @param driver Unique pointer to the driver instance
     */
    void addDriver(std::unique_ptr<IAudioDriver> driver);

    /**
     * @brief Get active driver statistics
     * @return Driver telemetry snapshot.
     */
    DriverStatistics getDriverStatistics() const;

    /**
     * @brief Enable/disable auto-buffer scaling on underruns
     * @param enable True to enable auto-scaling, false to disable
     * @param underrunsPerMinuteThreshold Number of underruns/min before scaling (default: 10)
     */
    void setAutoBufferScaling(bool enable, uint32_t underrunsPerMinuteThreshold = 10);

    /**
     * @brief Check underrun rate and auto-scale buffer if needed
     * Call this periodically (e.g., every second) to monitor performance
     */
    /**
     * @brief Check underrun rate and auto-scale buffer if needed
     * Call this periodically (e.g., every second) to monitor performance
     */
    void checkAndAutoScaleBuffer();

    /**
     * @brief Monitor active driver health and perform safety fallback if stalled.
     */
    void checkDriverHealth();

    /**
     * @brief Start the automatic driver health monitoring thread.
     * Called after a stream is opened successfully.
     */
    void startHealthMonitor();

    /**
     * @brief Stop the automatic driver health monitoring thread.
     * Called when the stream is closed or the manager is shut down.
     */
    void stopHealthMonitor();

    /**
     * @brief Force switch to the internal dummy driver (e.g. after a crash/disconnect).
     */
    bool switchToSafetyDriver();

    /**
     * @brief Enable/Disable dithering for active driver
     * @param enabled True to enable driver-side dithering.
     */
    void setDitheringEnabled(bool enabled);

    /**
     * @brief Check if dithering is enabled (preference)
     */
    bool isDitheringEnabled() const;

private:
    // -------------------------------------------------------------------------
    // Synchronization policy (#391)
    //
    // m_mutex guards ALL non-atomic manager state below (drivers, active driver,
    // current config/callback, preferences, fallback reason, driver-mode callback,
    // auto-scale bookkeeping, health-monitor bookkeeping). Public methods acquire
    // it and delegate to *Locked() helpers that assume it is held; the helpers
    // compose without re-locking. Rules that keep this deadlock-free:
    //   - The health-monitor thread is only ever started/stopped (joined) at the
    //     public boundary, never while m_mutex is held. checkDriverHealthLocked
    //     runs under the lock but never joins itself.
    //   - m_driverModeChangeCallback is captured under the lock and invoked only
    //     after the lock is released (it may re-enter a manager getter).
    //   - The realtime audio callback never touches this manager, so m_mutex is
    //     never acquired from the audio thread.
    //   - m_activeDriver is a borrowed pointer into m_drivers; m_drivers is only
    //     cleared under the lock (shutdown), so the pointer is valid for the whole
    //     of any locked section and is never used outside one.
    // -------------------------------------------------------------------------
    mutable std::mutex m_mutex;

    // Driver management
    std::vector<std::unique_ptr<IAudioDriver>> m_drivers;
    IAudioDriver* m_activeDriver = nullptr;
    AudioDriverType m_preferredDriverType =
        AudioDriverType::WASAPI_EXCLUSIVE; // Prefer Exclusive, auto-fallback to Shared if blocked

    // Preferences
    bool m_ditherEnabled = false;

    AudioStreamConfig m_currentConfig;
    AudioCallback m_currentCallback;
    void* m_currentUserData;
    bool m_initialized;
    bool m_wasRunning;

    // Driver mode change notification
    DriverModeChangeCallback m_driverModeChangeCallback;
    std::string m_fallbackReason;

    // Auto-buffer scaling
    bool m_autoBufferScalingEnabled = false;
    uint32_t m_underrunThreshold = 10; // Underruns per minute
    uint64_t m_lastUnderrunCount = 0;
    std::chrono::steady_clock::time_point m_lastUnderrunCheck;

    // Driver-health stall detection state (was function-local statics — now
    // per-instance and guarded by m_mutex like the rest of the manager state).
    uint64_t m_healthLastCallbackCount = 0;
    std::chrono::steady_clock::time_point m_healthLastUpdateTime;
    bool m_healthTrackingInitialized = false;

    // K-002: Driver health monitoring thread.
    // m_healthMonitorMutex serializes the monitor's lifecycle (thread handle +
    // running flag) independently of m_mutex. It is a leaf lock — never acquired
    // while m_mutex is held — so joining under it cannot deadlock (the monitor
    // thread only ever wants m_mutex, never this one).
    std::mutex m_healthMonitorMutex;
    std::atomic<bool> m_healthMonitorRunning{false};
    std::thread m_healthMonitorThread;
    void healthMonitorLoop();
    void startHealthMonitorIfStopped();

    // A pending driver-mode-change notification captured while holding m_mutex,
    // to be fired by the caller after the lock is released (#391 constraint 6).
    struct PendingModeChange {
        bool valid = false;
        AudioDriverType preferred{};
        AudioDriverType actual{};
        std::string reason;
    };
    void fireModeChange(const PendingModeChange& pending) const;

    // Locked helpers: caller must hold m_mutex. These do NOT touch the health
    // monitor thread and do NOT fire callbacks (see synchronization policy).
    std::vector<AudioDeviceInfo> getDevicesLocked() const;
    bool isStreamRunningLocked() const;
    bool openStreamLocked(const AudioStreamConfig& config, AudioCallback callback, void* userData);
    void closeStreamLocked();
    bool startStreamLocked();
    void stopStreamLocked();
    bool validateDeviceConfigLocked(uint32_t deviceId, uint32_t sampleRate) const;
    bool validateStreamConfigLocked(const AudioStreamConfig& config) const;
    bool tryDriver(IAudioDriver* driver, const AudioStreamConfig& config, AudioCallback callback, void* userData);
    void checkDriverHealthLocked(PendingModeChange& outPending);
    bool switchToSafetyDriverLocked(PendingModeChange& outPending);
};

// =============================================================================
// Registry Interface (Implemented by Platform Backend)
// =============================================================================
void RegisterPlatformDrivers(AudioDeviceManager& manager);

} // namespace Audio
} // namespace Aestra
