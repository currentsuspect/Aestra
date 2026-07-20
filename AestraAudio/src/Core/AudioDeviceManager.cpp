// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioDeviceManager.h"

#include "AestraLog.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

// Synchronization policy for this file (#391):
//   m_mutex guards all non-atomic manager state. Public methods lock and delegate
//   to *Locked() helpers (which assume the lock is held and compose without
//   re-locking). The health-monitor thread is only started/stopped (joined) at the
//   public boundary, never under the lock. The driver-mode-change callback is
//   captured under the lock and fired only after it is released. See the header
//   for the full policy and the invariants it maintains.

namespace Aestra {
namespace Audio {

AudioDeviceManager::AudioDeviceManager()
    : m_currentCallback(nullptr), m_currentUserData(nullptr), m_initialized(false), m_wasRunning(false) {}

AudioDeviceManager::~AudioDeviceManager() {
    shutdown();
}

void AudioDeviceManager::addDriver(std::unique_ptr<IAudioDriver> driver) {
    if (driver) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_drivers.push_back(std::move(driver));
    }
}

bool AudioDeviceManager::initialize(bool registerPlatformDrivers) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized) {
            return true;
        }
    }

    AESTRA_LOG_INFO("AestraAudio multi-tier driver system initializing...");

    try {
        // Register platform-specific drivers (Dependency Injection Point).
        // NOT under m_mutex: RegisterPlatformDrivers calls back into addDriver(),
        // which locks per push. Startup is single-threaded, so no other thread is
        // racing this; we then lock briefly to publish the initialized state.
        // Tests pass registerPlatformDrivers=false to run against only the drivers
        // they injected via addDriver() (no real hardware).
        if (registerPlatformDrivers) {
            AESTRA_LOG_DEBUG("RegisterPlatformDrivers about to be called");
            RegisterPlatformDrivers(*this);
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        AESTRA_LOG_DEBUG("RegisterPlatformDrivers returned, drivers count: " + std::to_string(m_drivers.size()));

        if (m_drivers.empty()) {
            AESTRA_LOG_WARNING("No audio drivers available");
        } else {
            for (const auto& driver : m_drivers) {
                AESTRA_LOG_DEBUG(std::string(driver->isAvailable() ? "✓ " : "✗ ") + driver->getDisplayName() +
                                 (driver->isAvailable() ? " available" : " unavailable"));
            }
        }

        m_initialized = true;
        AESTRA_LOG_INFO("Audio system initialized and ready");
        return true;

    } catch (const std::exception& e) {
        AESTRA_LOG_ERROR(std::string("AudioDeviceManager::initialize exception: ") + e.what());
        return false;
    }
}

void AudioDeviceManager::shutdown() {
    // Join the health monitor before taking the lock: the monitor thread acquires
    // m_mutex in checkDriverHealth, so joining it under the lock would deadlock
    // (#391 constraint 5). After this returns the monitor is not running.
    stopHealthMonitor();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
        return;
    }
    closeStreamLocked();
    m_drivers.clear();
    m_activeDriver = nullptr;
    m_initialized = false;
    AESTRA_LOG_DEBUG("Audio system shutdown complete");
}

// ---------------------------------------------------------------------------
// Device enumeration
// ---------------------------------------------------------------------------

std::vector<AudioDeviceInfo> AudioDeviceManager::getDevicesLocked() const {
    if (!m_initialized) {
        return {};
    }
    // Prefer the active driver; otherwise the first available one, matching the
    // historical priority (active -> exclusive -> shared -> rtaudio by reg order).
    if (m_activeDriver) {
        return m_activeDriver->getDevices();
    }
    for (const auto& driver : m_drivers) {
        if (driver && driver->isAvailable()) {
            return driver->getDevices();
        }
    }
    return {};
}

std::vector<AudioDeviceInfo> AudioDeviceManager::getDevices() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return getDevicesLocked();
}

AudioDeviceInfo AudioDeviceManager::getDefaultOutputDevice() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto devices = getDevicesLocked();
    for (const auto& device : devices) {
        if (device.maxOutputChannels > 0 && device.isDefaultOutput) {
            return device;
        }
    }
    for (const auto& device : devices) {
        if (device.maxOutputChannels > 0) {
            return device;
        }
    }
    return {};
}

AudioDeviceInfo AudioDeviceManager::getDefaultInputDevice() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto devices = getDevicesLocked();
    for (const auto& device : devices) {
        if (device.maxInputChannels > 0 && device.isDefaultInput) {
            return device;
        }
    }
    for (const auto& device : devices) {
        if (device.maxInputChannels > 0) {
            return device;
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Stream open/close/start/stop
// ---------------------------------------------------------------------------

bool AudioDeviceManager::tryDriver(IAudioDriver* driver, const AudioStreamConfig& config, AudioCallback callback,
                                   void* userData) {
    if (!driver || !driver->isAvailable()) {
        return false;
    }

    AESTRA_LOG_DEBUG("Trying " + driver->getDisplayName() + "...");

    if (driver->openStream(config, callback, userData)) {
        AESTRA_LOG_DEBUG(driver->getDisplayName() + " opened successfully");
        double lat = driver->getStreamLatency();
        AESTRA_LOG_DEBUG("  Latency: " + std::to_string(lat * 1000.0) + "ms");
        m_activeDriver = driver;
        return true;
    }

    Aestra::Log::warning(driver->getDisplayName() + " failed: " + driver->getErrorMessage());
    return false;
}

bool AudioDeviceManager::openStreamLocked(const AudioStreamConfig& config, AudioCallback callback, void* userData) {
    AESTRA_LOG_DEBUG("[AudioDeviceManager] openStream called. Rate: " + std::to_string(config.sampleRate) +
                     "Hz, Output Device: " + std::to_string(config.deviceId) +
                     ", Input Device: " + std::to_string(config.inputDeviceId));

    if (!m_initialized) {
        Aestra::Log::error("[AudioDeviceManager] openStream failed: Not initialized");
        return false;
    }

    if (!validateStreamConfigLocked(config)) {
        Aestra::Log::error("[AudioDeviceManager] openStream failed: Invalid stream configuration");
        return false;
    }

    m_currentConfig = config;
    m_currentCallback = callback;
    m_currentUserData = userData;

    std::string firstFailureReason;
    bool firstAttempt = true;

    for (auto& driver : m_drivers) {
        if (tryDriver(driver.get(), config, callback, userData)) {
            if (m_activeDriver && m_activeDriver->getDriverType() != m_preferredDriverType) {
                if (m_fallbackReason.empty()) {
                    m_fallbackReason = "Preferred driver (" + std::string(DriverTypeToString(m_preferredDriverType)) +
                                       ") was unavailable, fell back to " + m_activeDriver->getDisplayName();
                }
            }
            return true;
        }
        if (firstAttempt) {
            firstFailureReason = driver->getErrorMessage();
            firstAttempt = false;
        }
    }

    if (!firstAttempt && !firstFailureReason.empty()) {
        m_fallbackReason = "All drivers failed. Preferred (" + std::string(DriverTypeToString(m_preferredDriverType)) +
                           ") error: " + firstFailureReason;
    }

    Aestra::Log::error("[AudioDeviceManager] All drivers failed to open stream!");
    return false;
}

bool AudioDeviceManager::openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return openStreamLocked(config, callback, userData);
}

void AudioDeviceManager::closeStreamLocked() {
    if (m_activeDriver) {
        m_activeDriver->closeStream();
        m_activeDriver = nullptr;
    }
    // Deliberately preserve m_currentCallback/m_currentUserData for reopening.
}

void AudioDeviceManager::closeStream() {
    stopHealthMonitor(); // lock-free join before locking (#391 constraint 5)
    std::lock_guard<std::mutex> lock(m_mutex);
    closeStreamLocked();
}

bool AudioDeviceManager::startStreamLocked() {
    if (!m_activeDriver) {
        return false;
    }
    bool ok = m_activeDriver->startStream();
    if (ok) {
        uint32_t actualRate = m_activeDriver->getStreamSampleRate();
        AESTRA_LOG_DEBUG(std::string("Active driver stream started: requested ") +
                         std::to_string(m_currentConfig.sampleRate) + " Hz, actual " +
                         std::to_string(actualRate) + " Hz, buffer " +
                         std::to_string(m_currentConfig.bufferSize) + " frames");
    }
    return ok;
}

bool AudioDeviceManager::startStream() {
    bool ok;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ok = startStreamLocked();
    }
    // Start the monitor outside the lock — it spawns a thread that will acquire
    // m_mutex in checkDriverHealth; keeping the start off the lock avoids any
    // ordering surprise and matches how it must be stopped (#391).
    if (ok) {
        startHealthMonitorIfStopped();
    }
    return ok;
}

void AudioDeviceManager::stopStreamLocked() {
    if (m_activeDriver) {
        m_activeDriver->stopStream();
    }
}

void AudioDeviceManager::stopStream() {
    stopHealthMonitor(); // lock-free join before locking (#391 constraint 5)
    std::lock_guard<std::mutex> lock(m_mutex);
    stopStreamLocked();
}

bool AudioDeviceManager::isStreamRunningLocked() const {
    return m_activeDriver ? m_activeDriver->isStreamRunning() : false;
}

bool AudioDeviceManager::isStreamRunning() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return isStreamRunningLocked();
}

double AudioDeviceManager::getStreamLatency() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeDriver ? m_activeDriver->getStreamLatency() : 0.0;
}

uint32_t AudioDeviceManager::getStreamSampleRate() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeDriver ? m_activeDriver->getStreamSampleRate() : 0;
}

uint32_t AudioDeviceManager::getStreamBufferSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeDriver ? m_activeDriver->getStreamBufferSize() : 0;
}

void AudioDeviceManager::getLatencyCompensationValues(double& inputLatencyMs, double& outputLatencyMs) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    const double baseLatencyMs = (m_activeDriver ? m_activeDriver->getStreamLatency() : 0.0) * 1000.0;

    // For recording we compensate for input latency (mic->buffer) and output
    // latency (buffer->monitoring). Assume symmetric when an input is present.
    if (m_currentConfig.numInputChannels > 0) {
        inputLatencyMs = baseLatencyMs;
        outputLatencyMs = baseLatencyMs;
    } else {
        inputLatencyMs = 0.0;
        outputLatencyMs = baseLatencyMs;
    }
    // Intentionally does NOT write back into m_currentConfig: a logically const
    // getter must not mutate shared state (the previous const_cast was removed,
    // #391). The values are returned purely through the out-parameters.
}

AudioStreamConfig AudioDeviceManager::getCurrentConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentConfig; // value snapshot (#391 invariant 1)
}

// ---------------------------------------------------------------------------
// Reconfiguration transactions
//
// Each of these is a single locked transaction: capture running state, stop and
// close, apply the new config, and — on failure — roll back to the previous
// working configuration before returning. The health monitor is joined up front
// (outside the lock) and restarted afterward if a stream ends up running, so a
// transition is never observable half-complete and never deadlocks on the join.
// ---------------------------------------------------------------------------

bool AudioDeviceManager::switchDevice(uint32_t deviceId) {
    stopHealthMonitor();
    bool result;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_initialized) {
            result = false;
        } else if (m_currentConfig.deviceId == deviceId) {
            result = true;
        } else if (!validateDeviceConfigLocked(deviceId, m_currentConfig.sampleRate)) {
            result = false;
        } else {
            const uint32_t previousDevice = m_currentConfig.deviceId;
            m_wasRunning = isStreamRunningLocked();
            if (m_wasRunning) {
                stopStreamLocked();
            }
            closeStreamLocked();

            m_currentConfig.deviceId = deviceId;
            if (!openStreamLocked(m_currentConfig, m_currentCallback, m_currentUserData)) {
                // Roll back to the previous device so we do not leave a dead stream.
                m_currentConfig.deviceId = previousDevice;
                if (openStreamLocked(m_currentConfig, m_currentCallback, m_currentUserData) && m_wasRunning) {
                    startStreamLocked();
                }
                result = false;
            } else {
                result = (!m_wasRunning) || startStreamLocked();
            }
        }
    }
    if (isStreamRunning()) {
        startHealthMonitorIfStopped();
    }
    return result;
}

bool AudioDeviceManager::switchInputDevice(uint32_t deviceId) {
    stopHealthMonitor();
    bool result;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_initialized) {
            result = false;
        } else if (m_currentConfig.inputDeviceId == deviceId) {
            result = true;
        } else {
            const auto devices = getDevicesLocked();
            const auto it = std::find_if(devices.begin(), devices.end(), [deviceId](const AudioDeviceInfo& device) {
                return device.id == deviceId;
            });
            if (it == devices.end() || it->maxInputChannels == 0) {
                result = false;
            } else {
                const uint32_t previousInput = m_currentConfig.inputDeviceId;
                const uint32_t previousInChans = m_currentConfig.numInputChannels;
                m_wasRunning = isStreamRunningLocked();
                if (m_wasRunning) {
                    stopStreamLocked();
                }
                closeStreamLocked();

                m_currentConfig.inputDeviceId = deviceId;
                if (m_currentConfig.numInputChannels > it->maxInputChannels) {
                    m_currentConfig.numInputChannels = it->maxInputChannels;
                }

                if (!openStreamLocked(m_currentConfig, m_currentCallback, m_currentUserData)) {
                    m_currentConfig.inputDeviceId = previousInput;
                    m_currentConfig.numInputChannels = previousInChans;
                    if (openStreamLocked(m_currentConfig, m_currentCallback, m_currentUserData) && m_wasRunning) {
                        startStreamLocked();
                    }
                    result = false;
                } else {
                    result = (!m_wasRunning) || startStreamLocked();
                }
            }
        }
    }
    if (isStreamRunning()) {
        startHealthMonitorIfStopped();
    }
    return result;
}

bool AudioDeviceManager::setSampleRate(uint32_t sampleRate) {
    AESTRA_LOG_DEBUG("Request to set sample rate to: " + std::to_string(sampleRate));
    stopHealthMonitor();
    bool result;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_initialized) {
            AESTRA_LOG_ERROR("setSampleRate failed: Not initialized");
            result = false;
        } else if (m_currentConfig.sampleRate == sampleRate) {
            AESTRA_LOG_DEBUG("Sample rate unchanged (" + std::to_string(sampleRate) + " Hz), skipping reopen");
            result = true;
        } else if (sampleRate != 44100 && sampleRate != 48000 && sampleRate != 88200 && sampleRate != 96000 &&
                   sampleRate != 176400 && sampleRate != 192000) {
            Aestra::Log::error("[AudioDeviceManager] setSampleRate failed: Invalid rate " + std::to_string(sampleRate));
            result = false;
        } else if (!validateDeviceConfigLocked(m_currentConfig.deviceId, sampleRate)) {
            result = false;
        } else {
            const uint32_t previousSampleRate = m_currentConfig.sampleRate;
            m_wasRunning = isStreamRunningLocked();
            if (m_wasRunning) {
                stopStreamLocked();
            }
            closeStreamLocked();

            // Give the device time to fully release (avoids AUDCLNT_E_DEVICE_IN_USE).
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            m_currentConfig.sampleRate = sampleRate;
            if (!openStreamLocked(m_currentConfig, m_currentCallback, m_currentUserData)) {
                Aestra::Log::error("[AudioDeviceManager] Failed to reopen stream with sample rate " +
                                   std::to_string(sampleRate) + ", rolling back to " +
                                   std::to_string(previousSampleRate));
                m_currentConfig.sampleRate = previousSampleRate;
                if (!openStreamLocked(m_currentConfig, m_currentCallback, m_currentUserData)) {
                    Aestra::Log::error("[AudioDeviceManager] CRITICAL: Failed to restore previous sample rate!");
                } else if (m_wasRunning) {
                    startStreamLocked();
                }
                result = false;
            } else {
                result = (!m_wasRunning) || startStreamLocked();
            }
        }
    }
    if (isStreamRunning()) {
        startHealthMonitorIfStopped();
    }
    return result;
}

bool AudioDeviceManager::setBufferSize(uint32_t bufferSize) {
    stopHealthMonitor();
    bool result;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_initialized) {
            result = false;
        } else if (m_currentConfig.bufferSize == bufferSize) {
            AESTRA_LOG_DEBUG("Buffer size unchanged (" + std::to_string(bufferSize) + "), skipping reopen");
            result = true;
        } else if (bufferSize < 64 || bufferSize > 8192) {
            result = false;
        } else {
            const uint32_t previousBufferSize = m_currentConfig.bufferSize;
            m_wasRunning = isStreamRunningLocked();
            if (m_wasRunning) {
                stopStreamLocked();
            }
            closeStreamLocked();

            m_currentConfig.bufferSize = bufferSize;
            if (!openStreamLocked(m_currentConfig, m_currentCallback, m_currentUserData)) {
                AESTRA_LOG_ERROR("[AudioDeviceManager] Failed to reopen stream with buffer size " +
                                 std::to_string(bufferSize) + ", rolling back to " +
                                 std::to_string(previousBufferSize));
                m_currentConfig.bufferSize = previousBufferSize;
                if (!openStreamLocked(m_currentConfig, m_currentCallback, m_currentUserData)) {
                    AESTRA_LOG_ERROR("[AudioDeviceManager] CRITICAL: Failed to restore previous buffer size!");
                } else if (m_wasRunning) {
                    startStreamLocked();
                }
                result = false;
            } else {
                result = (!m_wasRunning) || startStreamLocked();
            }
        }
    }
    if (isStreamRunning()) {
        startHealthMonitorIfStopped();
    }
    return result;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

bool AudioDeviceManager::validateDeviceConfigLocked(uint32_t deviceId, uint32_t sampleRate) const {
    if (!m_initialized) {
        Aestra::Log::error("[AudioDeviceManager] validateDeviceConfig failed: Not initialized");
        return false;
    }

    const auto devices = getDevicesLocked();
    for (const auto& device : devices) {
        if (device.id == deviceId) {
            if (device.maxOutputChannels == 0) {
                Aestra::Log::error("[AudioDeviceManager] validateDeviceConfig failed: Device has 0 output channels");
                return false;
            }
            for (uint32_t supportedRate : device.supportedSampleRates) {
                if (supportedRate == sampleRate) {
                    return true;
                }
            }
            // Some drivers report empty/incorrect supported lists during enumeration
            // but work when actually opened. Warn but allow; openStream is final judge.
            Aestra::Log::warning("[AudioDeviceManager] Sample rate " + std::to_string(sampleRate) +
                                 " not in supported list (Driver Issue?), attempting anyway...");
            return true;
        }
    }

    Aestra::Log::error("[AudioDeviceManager] validateDeviceConfig failed: Device ID " + std::to_string(deviceId) +
                       " not found");
    return false;
}

bool AudioDeviceManager::validateDeviceConfig(uint32_t deviceId, uint32_t sampleRate) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return validateDeviceConfigLocked(deviceId, sampleRate);
}

bool AudioDeviceManager::validateStreamConfigLocked(const AudioStreamConfig& config) const {
    if (!validateDeviceConfigLocked(config.deviceId, config.sampleRate)) {
        return false;
    }

    if (config.numInputChannels == 0) {
        return true;
    }

    const auto devices = getDevicesLocked();
    const uint32_t inputDeviceId = (config.inputDeviceId != 0) ? config.inputDeviceId : config.deviceId;
    for (const auto& device : devices) {
        if (device.id != inputDeviceId) {
            continue;
        }
        if (device.maxInputChannels == 0) {
            Aestra::Log::error("[AudioDeviceManager] validateStreamConfig failed: Input device has 0 input channels");
            return false;
        }
        if (config.numInputChannels > device.maxInputChannels) {
            Aestra::Log::error(
                "[AudioDeviceManager] validateStreamConfig failed: Requested input channels exceed device capacity");
            return false;
        }
        return true;
    }

    Aestra::Log::error("[AudioDeviceManager] validateStreamConfig failed: Input device ID " +
                       std::to_string(inputDeviceId) + " not found");
    return false;
}

// ---------------------------------------------------------------------------
// Driver type / statistics
// ---------------------------------------------------------------------------

AudioDriverType AudioDeviceManager::getActiveDriverType() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeDriver ? m_activeDriver->getDriverType() : AudioDriverType::UNKNOWN;
}

DriverStatistics AudioDeviceManager::getDriverStatistics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeDriver ? m_activeDriver->getStatistics() : DriverStatistics();
}

bool AudioDeviceManager::setPreferredDriverType(AudioDriverType type) {
    stopHealthMonitor();
    bool result;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_initialized) {
            AESTRA_LOG_ERROR("Cannot set driver type: not initialized");
            result = false;
        } else if (m_preferredDriverType == type) {
            result = true; // no-op; avoids redundant reopen during UI population
        } else {
            AESTRA_LOG_DEBUG("Changing driver type");
            m_preferredDriverType = type;

            if (m_activeDriver && m_currentCallback) {
                const bool wasRunning = isStreamRunningLocked();
                auto savedCallback = m_currentCallback;
                auto savedUserData = m_currentUserData;
                auto savedConfig = m_currentConfig;

                if (wasRunning) {
                    stopStreamLocked();
                }
                closeStreamLocked();

                if (!openStreamLocked(savedConfig, savedCallback, savedUserData)) {
                    AESTRA_LOG_ERROR("Failed to reopen stream with any driver");
                    result = false;
                } else if (wasRunning && !startStreamLocked()) {
                    AESTRA_LOG_ERROR("Failed to restart stream");
                    result = false;
                } else {
                    result = true;
                }
            } else {
                result = true;
            }
        }
    }
    if (isStreamRunning()) {
        startHealthMonitorIfStopped();
    }
    return result;
}

bool AudioDeviceManager::isDriverTypeAvailable(AudioDriverType type) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
        return false;
    }
    for (const auto& driver : m_drivers) {
        if (driver->getDriverType() == type) {
            return driver->isAvailable();
        }
    }
    return false;
}

std::vector<AudioDriverType> AudioDeviceManager::getAvailableDriverTypes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AudioDriverType> types;
    for (const auto& driver : m_drivers) {
        if (driver && driver->isAvailable()) {
            types.push_back(driver->getDriverType());
        }
    }
    return types;
}

bool AudioDeviceManager::isUsingFallbackDriver() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_activeDriver) {
        return false;
    }
    return m_activeDriver->getDriverType() != m_preferredDriverType;
}

// ---------------------------------------------------------------------------
// Auto-buffer scaling + driver health
// ---------------------------------------------------------------------------

void AudioDeviceManager::setAutoBufferScaling(bool enable, uint32_t underrunsPerMinuteThreshold) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_autoBufferScalingEnabled = enable;
    m_underrunThreshold = underrunsPerMinuteThreshold;
    m_lastUnderrunCheck = std::chrono::steady_clock::now();
    m_lastUnderrunCount = 0;

    if (enable) {
        AESTRA_LOG_DEBUG("Auto-buffer scaling enabled, threshold: " + std::to_string(underrunsPerMinuteThreshold) +
                         " underruns/minute");
    }
}

void AudioDeviceManager::checkAndAutoScaleBuffer() {
    // Reconfigures the stream when underruns spike, so — like the other
    // transactions — it must not run while the health monitor might be joining or
    // reconfiguring. Callers must invoke this from the app/main loop, never from
    // the health-monitor thread. Join the monitor up front, work under the lock,
    // then fire any captured callback and restart the monitor outside the lock.
    stopHealthMonitor();

    PendingModeChange pending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        checkDriverHealthLocked(pending);

        if (m_autoBufferScalingEnabled && m_activeDriver && isStreamRunningLocked()) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastUnderrunCheck);
            if (elapsed.count() >= 60) {
                const DriverStatistics stats = m_activeDriver->getStatistics();
                const uint64_t newUnderruns = stats.underrunCount - m_lastUnderrunCount;

                if (newUnderruns >= m_underrunThreshold) {
                    const uint32_t currentBuffer = m_currentConfig.bufferSize;
                    uint32_t newBuffer = currentBuffer;
                    if (currentBuffer < 128) {
                        newBuffer = 128;
                    } else if (currentBuffer < 256) {
                        newBuffer = 256;
                    } else if (currentBuffer < 512) {
                        newBuffer = 512;
                    } else if (currentBuffer < 1024) {
                        newBuffer = 1024;
                    }

                    if (newBuffer != currentBuffer) {
                        const bool wasRunning = isStreamRunningLocked();
                        if (wasRunning) {
                            stopStreamLocked();
                        }
                        closeStreamLocked();
                        m_currentConfig.bufferSize = newBuffer;
                        if (openStreamLocked(m_currentConfig, m_currentCallback, m_currentUserData) && wasRunning) {
                            startStreamLocked();
                        }
                    }
                }

                m_lastUnderrunCheck = now;
                m_lastUnderrunCount = stats.underrunCount;
            }
        }
    }
    fireModeChange(pending);
    if (isStreamRunning()) {
        startHealthMonitorIfStopped();
    }
}

bool AudioDeviceManager::switchToSafetyDriverLocked(PendingModeChange& outPending) {
    AESTRA_LOG_WARNING("Attempting emergency fallback to Dummy driver");

    IAudioDriver* dummy = nullptr;
    for (auto& driver : m_drivers) {
        if (driver->getDriverType() == AudioDriverType::DUMMY) {
            dummy = driver.get();
            break;
        }
    }
    if (!dummy) {
        AESTRA_LOG_ERROR("[AudioDeviceManager] Critical failure: no dummy driver available for fallback");
        return false;
    }

    if (m_activeDriver) {
        try {
            m_activeDriver->stopStream();
            m_activeDriver->closeStream();
        } catch (const std::exception& e) {
            AESTRA_LOG_WARNING("Error closing audio driver: " + std::string(e.what()));
        }
        m_activeDriver = nullptr;
    }

    if (dummy->openStream(m_currentConfig, m_currentCallback, m_currentUserData)) {
        m_activeDriver = dummy;
        if (dummy->startStream()) {
            AESTRA_LOG_WARNING("Safety fallback ACTIVE — audio engine still running");
            // Capture the notification; the caller fires it after unlocking so the
            // callback can safely re-enter a manager getter (#391 constraint 6).
            outPending.valid = static_cast<bool>(m_driverModeChangeCallback);
            outPending.preferred = m_preferredDriverType;
            outPending.actual = AudioDriverType::DUMMY;
            outPending.reason = "Hardware stall/disconnect: fallback to safety driver";
            return true;
        }
    }
    return false;
}

bool AudioDeviceManager::switchToSafetyDriver() {
    PendingModeChange pending;
    bool ok;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ok = switchToSafetyDriverLocked(pending);
    }
    fireModeChange(pending);
    return ok;
}

void AudioDeviceManager::checkDriverHealthLocked(PendingModeChange& outPending) {
    if (!m_activeDriver || !isStreamRunningLocked() || m_activeDriver->getDriverType() == AudioDriverType::DUMMY) {
        return;
    }

    const DriverStatistics stats = m_activeDriver->getStatistics();
    const auto now = std::chrono::steady_clock::now();

    if (!m_healthTrackingInitialized || stats.callbackCount != m_healthLastCallbackCount) {
        m_healthLastCallbackCount = stats.callbackCount;
        m_healthLastUpdateTime = now;
        m_healthTrackingInitialized = true;
        return;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_healthLastUpdateTime).count();
    if (elapsed > 2000) {
        Aestra::Log::error("[AudioDeviceManager] DRIVER STALL DETECTED (" + m_activeDriver->getDisplayName() +
                           "), switching to safety driver");
        switchToSafetyDriverLocked(outPending);
        m_healthLastUpdateTime = now;
    }
}

void AudioDeviceManager::checkDriverHealth() {
    PendingModeChange pending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        checkDriverHealthLocked(pending);
    }
    fireModeChange(pending); // outside the lock (#391 constraint 6)
}

void AudioDeviceManager::fireModeChange(const PendingModeChange& pending) const {
    if (!pending.valid) {
        return;
    }
    // m_driverModeChangeCallback is read under the lock in the capturing path;
    // here we only fire what was captured. A concurrent setDriverModeChangeCallback
    // cannot tear this call because we invoke the local target snapshot.
    DriverModeChangeCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_driverModeChangeCallback;
    }
    if (cb) {
        cb(pending.preferred, pending.actual, pending.reason);
    }
}

void AudioDeviceManager::healthMonitorLoop() {
    while (m_healthMonitorRunning.load(std::memory_order_acquire)) {
        checkDriverHealth(); // locks internally; fires callback outside the lock
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void AudioDeviceManager::startHealthMonitorIfStopped() {
    // Guarded by m_healthMonitorMutex (the monitor lifecycle lock), NOT m_mutex:
    // this serializes concurrent start/stop from different transaction threads and
    // protects the thread handle. The spawned loop acquires m_mutex itself.
    std::lock_guard<std::mutex> lock(m_healthMonitorMutex);
    if (m_healthMonitorRunning.load(std::memory_order_acquire)) {
        return;
    }
    if (m_healthMonitorThread.joinable()) {
        m_healthMonitorThread.join(); // reap a previously-stopped run
    }
    m_healthMonitorRunning.store(true, std::memory_order_release);
    m_healthMonitorThread = std::thread(&AudioDeviceManager::healthMonitorLoop, this);
}

void AudioDeviceManager::startHealthMonitor() {
    startHealthMonitorIfStopped();
}

void AudioDeviceManager::stopHealthMonitor() {
    // Lock-free w.r.t. m_mutex by contract (#391 constraint 5): never called while
    // m_mutex is held. Uses only the lifecycle lock, so joining here cannot
    // deadlock against the monitor thread (which only wants m_mutex).
    std::lock_guard<std::mutex> lock(m_healthMonitorMutex);
    m_healthMonitorRunning.store(false, std::memory_order_release);
    if (m_healthMonitorThread.joinable()) {
        m_healthMonitorThread.join();
    }
}

void AudioDeviceManager::setDitheringEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ditherEnabled = enabled;
    if (m_activeDriver) {
        m_activeDriver->setDitheringEnabled(enabled);
    }
}

bool AudioDeviceManager::isDitheringEnabled() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ditherEnabled;
}

std::string AudioDeviceManager::getFallbackReason() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_fallbackReason;
}

void AudioDeviceManager::setDriverModeChangeCallback(DriverModeChangeCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_driverModeChangeCallback = std::move(callback);
}

const char* getVersion() {
    return "1.0.0";
}

const char* getBackendName() {
    return "RtAudio WASAPI";
}

} // namespace Audio
} // namespace Aestra
