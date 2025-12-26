// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioDeviceManager.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <chrono>
#include "NomadLog.h"

namespace Nomad {
namespace Audio {

AudioDeviceManager::AudioDeviceManager()
    : m_currentCallback(nullptr)
    , m_currentUserData(nullptr)
    , m_initialized(false)
    , m_wasRunning(false)
{
}

AudioDeviceManager::~AudioDeviceManager() {
    shutdown();
}

void AudioDeviceManager::addDriver(std::unique_ptr<IAudioDriver> driver) {
    if (driver) {
        m_drivers.push_back(std::move(driver));
    }
}

bool AudioDeviceManager::initialize() {
    if (m_initialized) {
        return true;
    }

    std::cout << "\n=== NomadAudio Multi-Tier Driver System ===" << std::endl;
    std::cout << "Initializing professional audio drivers..." << std::endl;

    try {
        // Register platform-specific drivers (Dependency Injection Point)
        RegisterPlatformDrivers(*this);
        
        if (m_drivers.empty()) {
             std::cout << "No audio drivers available!" << std::endl;
             // Continue initialization but warn? Or fail? 
             // Logic suggests returning false if strictly no audio capability is fatal.
             // But for audit tool we might want to continue. 
             // Let's print warning.
        }

        std::cout << "Registered " << m_drivers.size() << " driver(s)." << std::endl;

        // Log driver status
        for (const auto& driver : m_drivers) {
            std::cout << (driver->isAvailable() ? "✓ " : "✗ ") << driver->getDisplayName() 
                      << (driver->isAvailable() ? " available" : " unavailable") << std::endl;
        }
        
        // Removed explicit ASIO scanning (now handled by drivers themselves if registered)
        
        m_initialized = true;
        std::cout << "=== Audio System Ready ===" << std::endl;
        std::cout << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "AudioDeviceManager::initialize: Exception: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "AudioDeviceManager::initialize: Unknown exception" << std::endl;
        return false;
    }
}

void AudioDeviceManager::shutdown() {
    if (m_initialized) {
        closeStream();
        
        m_drivers.clear();
        
        m_activeDriver = nullptr;
        m_initialized = false;
        
        std::cout << "Audio system shutdown complete" << std::endl;
    }
}

std::vector<AudioDeviceInfo> AudioDeviceManager::getDevices() const {
    if (!m_initialized) {
        return {};
    }
    
    // Aggregated devices from all available drivers?
    // Or just the active one?
    // Original code: "active driver -> exclusive -> shared -> rtaudio"
    // We should preserve this priority logic.
    // Iterating drivers in order (assuming registration order implies priority).
    
    // Use active driver if available
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

AudioDeviceInfo AudioDeviceManager::getDefaultOutputDevice() const {
    if (!m_initialized) {
        return {};
    }

    // Get all devices and find first output device
    auto devices = getDevices();
    
    for (const auto& device : devices) {
        if (device.maxOutputChannels > 0 && device.isDefaultOutput) {
            return device;
        }
    }
    
    // If no default marked, return first output device
    for (const auto& device : devices) {
        if (device.maxOutputChannels > 0) {
            return device;
        }
    }
    
    return {};
}

AudioDeviceInfo AudioDeviceManager::getDefaultInputDevice() const {
    if (!m_initialized) {
        return {};
    }

    // Get all devices and find first input device
    auto devices = getDevices();
    
    for (const auto& device : devices) {
        if (device.maxInputChannels > 0 && device.isDefaultInput) {
            return device;
        }
    }
    
    // If no default marked, return first input device
    for (const auto& device : devices) {
        if (device.maxInputChannels > 0) {
            return device;
        }
    }
    
    return {};
}


bool AudioDeviceManager::tryDriver(IAudioDriver* driver, const AudioStreamConfig& config,
                                   AudioCallback callback, void* userData) {
    if (!driver || !driver->isAvailable()) {
        return false;
    }
    
    std::cout << "Trying " << driver->getDisplayName() << "..." << std::endl;
    
    if (driver->openStream(config, callback, userData)) {
        std::cout << "âœ“ " << driver->getDisplayName() << " opened successfully" << std::endl;
        double lat = driver->getStreamLatency();
        std::cout << "  Latency: " << (lat * 1000.0) << "ms" << std::endl;
        m_activeDriver = driver;
        return true;
    }
    
    std::cout << "✗ " << driver->getDisplayName() << " failed: " 
              << driver->getErrorMessage() << std::endl;
    return false;
}

bool AudioDeviceManager::openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) {
    Nomad::Log::info("[AudioDeviceManager] openStream called. Rate: " + std::to_string(config.sampleRate) + "Hz, Device: " + std::to_string(config.deviceId));

    if (!m_initialized) {
        Nomad::Log::error("[AudioDeviceManager] openStream failed: Not initialized");
        return false;
    }

    m_currentConfig = config;
    m_currentCallback = callback;
    m_currentUserData = userData;

    // Iterate through drivers to find one that works
    // Prioritize exclusive mode if that was the "preferred" (implied by previous usage)
    for (auto& driver : m_drivers) {
        if (tryDriver(driver.get(), config, callback, userData)) {
            // Log success handled in tryDriver
            return true;
        }
    }

    Nomad::Log::error("[AudioDeviceManager] All drivers failed to open stream!");
    return false;
}

void AudioDeviceManager::closeStream() {
    if (m_activeDriver) {
        m_activeDriver->closeStream();
        m_activeDriver = nullptr;
    }
    // DON'T clear callback/userData here - they need to be preserved for stream reopening
    // They will be updated when openStream() is called with new values
    // m_currentCallback = nullptr;
    // m_currentUserData = nullptr;
}

bool AudioDeviceManager::startStream() {
    auto logStreamInfo = [this](IAudioDriver* driver, const char* label) {
        if (!driver) return;
        uint32_t actualRate = driver->getStreamSampleRate();
        uint32_t requestedRate = m_currentConfig.sampleRate;
        std::cout << label << ": requested " << requestedRate << " Hz"
                  << ", actual " << actualRate << " Hz"
                  << ", buffer " << m_currentConfig.bufferSize << " frames"
                  << std::endl;
    };

    if (m_activeDriver) {
        bool ok = m_activeDriver->startStream();
        if (ok) {
            logStreamInfo(m_activeDriver, "Active driver stream started");
        }
        return ok;
    }
    return false;
}

void AudioDeviceManager::stopStream() {
    if (m_activeDriver) {
        m_activeDriver->stopStream();
    }
}

bool AudioDeviceManager::isStreamRunning() const {
    if (m_activeDriver) {
        return m_activeDriver->isStreamRunning();
    }
    return false;
}

double AudioDeviceManager::getStreamLatency() const {
    if (m_activeDriver) {
        return m_activeDriver->getStreamLatency();
    }
    return 0.0;
}

uint32_t AudioDeviceManager::getStreamSampleRate() const {
    if (m_activeDriver) {
        return m_activeDriver->getStreamSampleRate();
    }
    return 0;
}

uint32_t AudioDeviceManager::getStreamBufferSize() const {
    if (m_activeDriver) {
        return m_activeDriver->getStreamBufferSize();
    }
    return 0;
}

void AudioDeviceManager::getLatencyCompensationValues(double& inputLatencyMs, double& outputLatencyMs) const {
    // Get base latency from current stream
    double baseLatencySeconds = getStreamLatency();
    double baseLatencyMs = baseLatencySeconds * 1000.0;
    
    // For recording, we need to compensate for:
    // 1. Input latency (time from mic to buffer)
    // 2. Output latency (time from buffer to monitoring headphones)
    
    // If we have input channels, assume input latency equals output latency
    if (m_currentConfig.numInputChannels > 0) {
        inputLatencyMs = baseLatencyMs;
        outputLatencyMs = baseLatencyMs;
    } else {
        // Output-only configuration
        inputLatencyMs = 0.0;
        outputLatencyMs = baseLatencyMs;
    }
    
    // Store in config for reference
    const_cast<AudioStreamConfig&>(m_currentConfig).inputLatencyMs = inputLatencyMs;
    const_cast<AudioStreamConfig&>(m_currentConfig).outputLatencyMs = outputLatencyMs;
}

bool AudioDeviceManager::switchDevice(uint32_t deviceId) {
    if (!m_initialized) {
        return false;
    }

    // Validate the new device
    if (!validateDeviceConfig(deviceId, m_currentConfig.sampleRate)) {
        return false;
    }

    // Remember if stream was running
    m_wasRunning = isStreamRunning();

    // Stop and close current stream
    if (m_wasRunning) {
        stopStream();
    }
    closeStream();

    // Update configuration with new device
    m_currentConfig.deviceId = deviceId;

    // Reopen stream with new device
    if (!openStream(m_currentConfig, m_currentCallback, m_currentUserData)) {
        return false;
    }

    // Restart stream if it was running
    if (m_wasRunning) {
        return startStream();
    }

    return true;
}

bool AudioDeviceManager::setSampleRate(uint32_t sampleRate) {
    Nomad::Log::info("[AudioDeviceManager] Request to set sample rate to: " + std::to_string(sampleRate));
    
    if (!m_initialized) {
        Nomad::Log::error("[AudioDeviceManager] setSampleRate failed: Not initialized");
        return false;
    }

    // Validate sample rate (common rates)
    // TODO: Add more robust validation or query device capabilities
    if (sampleRate != 44100 && sampleRate != 48000 && sampleRate != 88200 &&
        sampleRate != 96000 && sampleRate != 176400 && sampleRate != 192000) {
        Nomad::Log::error("[AudioDeviceManager] setSampleRate failed: Invalid rate " + std::to_string(sampleRate));
        return false;
    }

    // Validate the new sample rate
    if (!validateDeviceConfig(m_currentConfig.deviceId, sampleRate)) {
        return false;
    }

    // Save previous sample rate for rollback
    uint32_t previousSampleRate = m_currentConfig.sampleRate;

    // Remember if stream was running
    m_wasRunning = isStreamRunning();

    // Stop and close current stream
    if (m_wasRunning) {
        stopStream();
    }
    closeStream();

    // Wait for device to fully release (helps avoid AUDCLNT_E_DEVICE_IN_USE race conditions)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Update configuration with new sample rate
    m_currentConfig.sampleRate = sampleRate;

    // Reopen stream with new sample rate
    if (!openStream(m_currentConfig, m_currentCallback, m_currentUserData)) {
        Nomad::Log::error("[AudioDeviceManager] Failed to reopen stream with sample rate " + std::to_string(sampleRate) + ", rolling back to " + std::to_string(previousSampleRate));
        
        // Rollback to previous sample rate
        m_currentConfig.sampleRate = previousSampleRate;
        
        // Try to restore previous working state
        if (!openStream(m_currentConfig, m_currentCallback, m_currentUserData)) {
            Nomad::Log::error("[AudioDeviceManager] CRITICAL: Failed to restore previous sample rate!");
            return false;
        }
        
        // If we successfully rolled back, restart if needed
        if (m_wasRunning) {
            startStream();
        }
        
        return false;  // Still return false because the requested change failed
    }

    // Restart stream if it was running
    if (m_wasRunning) {
        return startStream();
    }

    return true;
}

bool AudioDeviceManager::setBufferSize(uint32_t bufferSize) {
    if (!m_initialized) {
        return false;
    }

    // Validate buffer size (reasonable range: 64 to 8192 frames)
    if (bufferSize < 64 || bufferSize > 8192) {
        return false;
    }

    // Save previous buffer size for rollback
    uint32_t previousBufferSize = m_currentConfig.bufferSize;

    // Remember if stream was running
    m_wasRunning = isStreamRunning();

    // Stop and close current stream
    if (m_wasRunning) {
        stopStream();
    }
    closeStream();

    // Update configuration with new buffer size
    m_currentConfig.bufferSize = bufferSize;

    // Reopen stream with new buffer size
    if (!openStream(m_currentConfig, m_currentCallback, m_currentUserData)) {
        std::cerr << "[AudioDeviceManager] Failed to reopen stream with buffer size " 
                  << bufferSize << ", rolling back to " << previousBufferSize << std::endl;
        
        // Rollback to previous buffer size
        m_currentConfig.bufferSize = previousBufferSize;
        
        // Try to restore previous working state
        if (!openStream(m_currentConfig, m_currentCallback, m_currentUserData)) {
            std::cerr << "[AudioDeviceManager] CRITICAL: Failed to restore previous buffer size!" << std::endl;
            return false;
        }
        
        // If we successfully rolled back, restart if needed
        if (m_wasRunning) {
            startStream();
        }
        
        return false;  // Still return false because the requested change failed
    }

    // Restart stream if it was running
    if (m_wasRunning) {
        return startStream();
    }

    return true;
}

bool AudioDeviceManager::validateDeviceConfig(uint32_t deviceId, uint32_t sampleRate) const {
    if (!m_initialized) {
        Nomad::Log::error("[AudioDeviceManager] validateDeviceConfig failed: Not initialized");
        return false;
    }

    auto devices = getDevices();
    
    // Find the device
    for (const auto& device : devices) {
        if (device.id == deviceId) {
            // Check if device has output channels
            if (device.maxOutputChannels == 0) {
                Nomad::Log::error("[AudioDeviceManager] validateDeviceConfig failed: Device has 0 output channels");
                return false;
            }

            // Check if sample rate is supported
            bool sampleRateSupported = false;
            std::string supportedRatesStr;
            for (uint32_t supportedRate : device.supportedSampleRates) {
                supportedRatesStr += std::to_string(supportedRate) + " ";
                if (supportedRate == sampleRate) {
                    sampleRateSupported = true;
                    break;
                }
            }

            if (!sampleRateSupported) {
        // Log as warning but ALLOW it. 
        // Some drivers (e.g. WASAPI Exclusive on certain virtual devices) report empty/incorrect supported lists 
        // during enumeration but work fine when actually opened. 
        // We let openStream() be the final judge.
        Nomad::Log::warning("[AudioDeviceManager] Sample rate " + std::to_string(sampleRate) + " not in supported list (Driver Issue?), attempting anyway...");
        return true; 
    }        return sampleRateSupported;
        }
    }

    Nomad::Log::error("[AudioDeviceManager] validateDeviceConfig failed: Device ID " + std::to_string(deviceId) + " not found");
    return false; // Device not found
}

AudioDriverType AudioDeviceManager::getActiveDriverType() const {
    if (m_activeDriver) {
        return m_activeDriver->getDriverType();
    }
    return AudioDriverType::UNKNOWN;
}

DriverStatistics AudioDeviceManager::getDriverStatistics() const {
    if (m_activeDriver) {
        return m_activeDriver->getStatistics();
    }
    return DriverStatistics();
}

bool AudioDeviceManager::setPreferredDriverType(AudioDriverType type) {
    if (!m_initialized) {
        std::cerr << "Cannot set driver type: not initialized" << std::endl;
        return false;
    }

    // Only support WASAPI types for now (conceptually)
    // In our new generic model, we just store the preference.
    // Real switching happens in openStream.
    std::cout << "\n=== Changing Driver Type ===" << std::endl;

    m_preferredDriverType = type;

    // If stream is open, reopen with new driver preference
    if (m_activeDriver && m_currentCallback) {
        bool wasRunning = isStreamRunning();
        
        // Save callback and user data before closing (closeStream clears them!)
        auto savedCallback = m_currentCallback;
        auto savedUserData = m_currentUserData;
        auto savedConfig = m_currentConfig;
        
        if (wasRunning) {
            stopStream();
        }
        closeStream();
        
        // Reopen with preferred driver - this will try preferred first, then fallback
        bool success = openStream(savedConfig, savedCallback, savedUserData);
        
        if (!success) {
            std::cerr << "âœ— Failed to reopen stream with any driver!" << std::endl;
            return false;
        }
        
        if (wasRunning) {
            if (!startStream()) {
                std::cerr << "âœ— Failed to restart stream!" << std::endl;
                return false;
            }
        }
        
        return true;
    }

    return true;
}

bool AudioDeviceManager::isDriverTypeAvailable(AudioDriverType type) const {
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
    std::vector<AudioDriverType> types;
    
    for (const auto& driver : m_drivers) {
        if (driver && driver->isAvailable()) {
            types.push_back(driver->getDriverType());
        }
    }
    
    return types;
}

bool AudioDeviceManager::isUsingFallbackDriver() const {
    if (!m_activeDriver) {
        return false;
    }

    AudioDriverType activeType = getActiveDriverType();
    return activeType != m_preferredDriverType;
}



void AudioDeviceManager::setAutoBufferScaling(bool enable, uint32_t underrunsPerMinuteThreshold) {
    m_autoBufferScalingEnabled = enable;
    m_underrunThreshold = underrunsPerMinuteThreshold;
    m_lastUnderrunCheck = std::chrono::steady_clock::now();
    m_lastUnderrunCount = 0;
    
    if (enable) {
        std::cout << "[Auto-Buffer Scaling] Enabled with threshold: " 
                  << underrunsPerMinuteThreshold << " underruns/minute" << std::endl;
    }
}

void AudioDeviceManager::checkAndAutoScaleBuffer() {
    if (!m_autoBufferScalingEnabled || !m_activeDriver || !isStreamRunning()) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastUnderrunCheck);
    
    // Check every 60 seconds
    if (elapsed.count() < 60) {
        return;
    }
    
    DriverStatistics stats = m_activeDriver->getStatistics();
    uint64_t newUnderruns = stats.underrunCount - m_lastUnderrunCount;
    
    // std::cout << "[Auto-Buffer Scaling] Check - Underruns in last minute: " << newUnderruns << std::endl;
    
    if (newUnderruns >= m_underrunThreshold) {
        // Need to scale up buffer
        uint32_t currentBuffer = m_currentConfig.bufferSize;
        uint32_t newBuffer = currentBuffer;
        
        // Scale: 64->128->256->512->1024
        if (currentBuffer < 128) {
            newBuffer = 128;
        } else if (currentBuffer < 256) {
            newBuffer = 256;
        } else if (currentBuffer < 512) {
            newBuffer = 512;
        } else if (currentBuffer < 1024) {
            newBuffer = 1024;
        } else {
            // std::cerr << "[Auto-Buffer Scaling] Already at maximum buffer size (1024)" << std::endl;
            m_lastUnderrunCheck = now;
            m_lastUnderrunCount = stats.underrunCount;
            return;
        }
        
        // std::cout << "[Auto-Buffer Scaling] Too many underruns (" << newUnderruns << "/" << m_underrunThreshold 
        //           << ") - increasing buffer: " << currentBuffer << " -> " << newBuffer << " frames" << std::endl;
        
        // Update buffer size and restart stream
        bool wasRunning = isStreamRunning();
        if (wasRunning) {
            stopStream();
        }
        
        closeStream();
        m_currentConfig.bufferSize = newBuffer;
        
        if (openStream(m_currentConfig, m_currentCallback, m_currentUserData)) {
            if (wasRunning) {
                startStream();
            }
            // std::cout << "[Auto-Buffer Scaling] Buffer increased successfully. New latency: " 
            //           << getStreamLatency() * 1000.0 << "ms" << std::endl;
        } else {
            // std::cerr << "[Auto-Buffer Scaling] Failed to reopen stream with new buffer size" << std::endl;
        }
    }
    
    m_lastUnderrunCheck = now;
    m_lastUnderrunCount = stats.underrunCount;
}

void AudioDeviceManager::setDitheringEnabled(bool enabled) {
    m_ditherEnabled = enabled;
    if (m_activeDriver) {
        m_activeDriver->setDitheringEnabled(enabled);
    }
}

const char* getVersion() {
    return "1.0.0";
}

const char* getBackendName() {
    return "RtAudio WASAPI";
}

} // namespace Audio
} // namespace Nomad
