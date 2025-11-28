// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "AudioDeviceManager.h"
#include "RtAudioBackend.h"
#include "WASAPIExclusiveDriver.h"
#include "WASAPISharedDriver.h"
#include "ASIODriverInfo.h"
#include <iostream>
#include <chrono>

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

bool AudioDeviceManager::initialize() {
    if (m_initialized) {
        return true;
    }

    std::cout << "\n=== NomadAudio Multi-Tier Driver System ===" << std::endl;
    std::cout << "Initializing professional audio drivers..." << std::endl;

    try {
        // Create WASAPI drivers
        m_exclusiveDriver = std::make_unique<WASAPIExclusiveDriver>();
        m_sharedDriver = std::make_unique<WASAPISharedDriver>();
        
        // Initialize drivers
        bool exclusiveOk = m_exclusiveDriver->initialize();
        bool sharedOk = m_sharedDriver->initialize();
        
        if (exclusiveOk) {
            std::cout << "âœ“ WASAPI Exclusive mode available" << std::endl;
        } else {
            std::cout << "âœ— WASAPI Exclusive mode unavailable" << std::endl;
        }
        
        if (sharedOk) {
            std::cout << "âœ“ WASAPI Shared mode available" << std::endl;
        } else {
            std::cout << "âœ— WASAPI Shared mode unavailable" << std::endl;
        }
        
        // Check for ASIO drivers (info only)
        std::cout << "\nScanning for ASIO drivers..." << std::endl;
        auto asioDrivers = ASIODriverScanner::scanInstalledDrivers();
        if (!asioDrivers.empty()) {
            std::cout << "Found " << asioDrivers.size() << " ASIO driver(s):" << std::endl;
            for (const auto& driver : asioDrivers) {
                std::cout << "  â€¢ " << driver.name << std::endl;
            }
            std::cout << "Note: NOMAD uses WASAPI for equivalent low-latency performance.\n" << std::endl;
        } else {
            std::cout << "No ASIO drivers detected (WASAPI provides professional audio).\n" << std::endl;
        }
        
        // Fallback: Create RtAudio backend if WASAPI fails
        if (!exclusiveOk && !sharedOk) {
            std::cout << "Falling back to RtAudio backend..." << std::endl;
            m_rtAudioDriver = std::make_unique<RtAudioBackend>();
        }
        
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
        
        if (m_exclusiveDriver) {
            m_exclusiveDriver->shutdown();
            m_exclusiveDriver.reset();
        }
        if (m_sharedDriver) {
            m_sharedDriver->shutdown();
            m_sharedDriver.reset();
        }
        if (m_rtAudioDriver) {
            m_rtAudioDriver.reset();
        }
        
        m_activeDriver = nullptr;
        m_initialized = false;
        
        std::cout << "Audio system shutdown complete" << std::endl;
    }
}

std::vector<AudioDeviceInfo> AudioDeviceManager::getDevices() const {
    if (!m_initialized) {
        return {};
    }
    
    // Use active driver if available
    if (m_activeDriver) {
        return m_activeDriver->getDevices();
    }
    
    // Try exclusive first
    if (m_exclusiveDriver && m_exclusiveDriver->isAvailable()) {
        return m_exclusiveDriver->getDevices();
    }
    
    // Then shared
    if (m_sharedDriver && m_sharedDriver->isAvailable()) {
        return m_sharedDriver->getDevices();
    }
    
    // Fallback to RtAudio
    if (m_rtAudioDriver) {
        return m_rtAudioDriver->getDevices();
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

bool AudioDeviceManager::tryDriver(NativeAudioDriver* driver, const AudioStreamConfig& config,
                                   AudioCallback callback, void* userData) {
    if (!driver || !driver->isAvailable()) {
        return false;
    }
    
    std::cout << "Trying " << driver->getDisplayName() << "..." << std::endl;
    
    if (driver->openStream(config, callback, userData)) {
        std::cout << "âœ“ " << driver->getDisplayName() << " opened successfully" << std::endl;
        std::cout << "  Latency: " << (driver->getStreamLatency() * 1000.0) << "ms" << std::endl;
        m_activeDriver = driver;
        return true;
    }
    
    std::cout << "âœ— " << driver->getDisplayName() << " failed: " 
              << driver->getErrorMessage() << std::endl;
    return false;
}

bool AudioDeviceManager::openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) {
    std::cout << "\n=== Opening Audio Stream ===" << std::endl;
    std::cout << "  Device ID: " << config.deviceId << std::endl;
    std::cout << "  Sample Rate: " << config.sampleRate << " Hz" << std::endl;
    std::cout << "  Buffer Size: " << config.bufferSize << " frames" << std::endl;
    std::cout << "  Output Channels: " << config.numOutputChannels << std::endl;
    std::cout << "  Input Channels: " << config.numInputChannels << std::endl;

    if (!m_initialized) {
        std::cerr << "AudioDeviceManager::openStream: Not initialized" << std::endl;
        return false;
    }

    m_currentConfig = config;
    m_currentCallback = callback;
    m_currentUserData = userData;
    
    // Try drivers based on preferred type first, then fallback
    
    // Try preferred driver first
    if (m_preferredDriverType == AudioDriverType::WASAPI_EXCLUSIVE) {
        if (tryDriver(m_exclusiveDriver.get(), config, callback, userData)) {
            std::cout << "=== Stream Opened with WASAPI Exclusive (Preferred) ===" << std::endl;
            return true;
        }
        // Fallback to shared
        if (tryDriver(m_sharedDriver.get(), config, callback, userData)) {
            std::cout << "=== Stream Opened with WASAPI Shared (Fallback) ===" << std::endl;
            return true;
        }
    } else if (m_preferredDriverType == AudioDriverType::WASAPI_SHARED) {
        if (tryDriver(m_sharedDriver.get(), config, callback, userData)) {
            std::cout << "=== Stream Opened with WASAPI Shared (Preferred) ===" << std::endl;
            return true;
        }
        // Fallback to exclusive
        if (tryDriver(m_exclusiveDriver.get(), config, callback, userData)) {
            std::cout << "=== Stream Opened with WASAPI Exclusive (Fallback) ===" << std::endl;
            return true;
        }
    }
    
    // 3. Try RtAudio (legacy fallback)
    if (m_rtAudioDriver && m_rtAudioDriver->openStream(config, callback, userData)) {
        std::cout << "âœ“ RtAudio backend opened successfully" << std::endl;
        std::cout << "=== Stream Opened with RtAudio ===" << std::endl;
        return true;
    }
    
    std::cerr << "\nâœ— All drivers failed to open stream!" << std::endl;
    std::cerr << "=== Stream Open Failed ===" << std::endl;
    return false;
}

void AudioDeviceManager::closeStream() {
    if (m_activeDriver) {
        m_activeDriver->closeStream();
        m_activeDriver = nullptr;
    }
    if (m_rtAudioDriver) {
        m_rtAudioDriver->closeStream();
    }
    // DON'T clear callback/userData here - they need to be preserved for stream reopening
    // They will be updated when openStream() is called with new values
    // m_currentCallback = nullptr;
    // m_currentUserData = nullptr;
}

bool AudioDeviceManager::startStream() {
    if (m_activeDriver) {
        return m_activeDriver->startStream();
    }
    if (m_rtAudioDriver) {
        return m_rtAudioDriver->startStream();
    }
    return false;
}

void AudioDeviceManager::stopStream() {
    if (m_activeDriver) {
        m_activeDriver->stopStream();
    }
    if (m_rtAudioDriver) {
        m_rtAudioDriver->stopStream();
    }
}

bool AudioDeviceManager::isStreamRunning() const {
    if (m_activeDriver) {
        return m_activeDriver->isStreamRunning();
    }
    if (m_rtAudioDriver) {
        return m_rtAudioDriver->isStreamRunning();
    }
    return false;
}

double AudioDeviceManager::getStreamLatency() const {
    if (m_activeDriver) {
        return m_activeDriver->getStreamLatency();
    }
    if (m_rtAudioDriver) {
        return m_rtAudioDriver->getStreamLatency();
    }
    return 0.0;
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
    if (!m_initialized) {
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

    // Update configuration with new sample rate
    m_currentConfig.sampleRate = sampleRate;

    // Reopen stream with new sample rate
    if (!openStream(m_currentConfig, m_currentCallback, m_currentUserData)) {
        std::cerr << "[AudioDeviceManager] Failed to reopen stream with sample rate " 
                  << sampleRate << ", rolling back to " << previousSampleRate << std::endl;
        
        // Rollback to previous sample rate
        m_currentConfig.sampleRate = previousSampleRate;
        
        // Try to restore previous working state
        if (!openStream(m_currentConfig, m_currentCallback, m_currentUserData)) {
            std::cerr << "[AudioDeviceManager] CRITICAL: Failed to restore previous sample rate!" << std::endl;
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
        return false;
    }

    auto devices = getDevices();
    
    // Find the device
    for (const auto& device : devices) {
        if (device.id == deviceId) {
            // Check if device has output channels
            if (device.maxOutputChannels == 0) {
                return false;
            }

            // Check if sample rate is supported
            bool sampleRateSupported = false;
            for (uint32_t supportedRate : device.supportedSampleRates) {
                if (supportedRate == sampleRate) {
                    sampleRateSupported = true;
                    break;
                }
            }

            return sampleRateSupported;
        }
    }

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

    // Only support WASAPI types for now
    if (type != AudioDriverType::WASAPI_EXCLUSIVE && type != AudioDriverType::WASAPI_SHARED) {
        std::cerr << "Cannot set driver type: unsupported type" << std::endl;
        return false;
    }

    std::cout << "\n=== Changing Driver Type ===" << std::endl;
    std::cout << "Requested: " << (type == AudioDriverType::WASAPI_EXCLUSIVE ? "WASAPI Exclusive" : "WASAPI Shared") << std::endl;

    m_preferredDriverType = type;

    // If stream is open, reopen with new driver preference
    if (m_activeDriver && m_currentCallback) {
        bool wasRunning = isStreamRunning();
        
        std::cout << "Stream was " << (wasRunning ? "running" : "stopped") << std::endl;
        
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
            std::cerr << "=== Driver Change Failed ===" << std::endl;
            return false;
        }
        
        if (wasRunning) {
            if (!startStream()) {
                std::cerr << "âœ— Failed to restart stream!" << std::endl;
                return false;
            }
        }
        
        std::cout << "âœ“ Driver changed successfully to: " << (m_activeDriver ? m_activeDriver->getDisplayName() : "Unknown") << std::endl;
        std::cout << "=== Driver Change Complete ===" << std::endl;
        return true;
    }

    return true;
}

bool AudioDeviceManager::isDriverTypeAvailable(AudioDriverType type) const {
    if (!m_initialized) {
        return false;
    }

    // Quick availability check based on driver type
    switch (type) {
        case AudioDriverType::WASAPI_EXCLUSIVE:
            // Check if Exclusive driver exists and is initialized
            if (!m_exclusiveDriver || !m_exclusiveDriver->isAvailable()) {
                return false;
            }
            // If we're currently using a different driver, Exclusive might be blocked
            // by another application (like SoundID Reference)
            // We can't easily test without disrupting the current stream,
            // so we assume it's available if initialized
            return true;

        case AudioDriverType::WASAPI_SHARED:
            // Shared mode is almost always available
            return m_sharedDriver && m_sharedDriver->isAvailable();

        case AudioDriverType::RTAUDIO:
            return m_rtAudioDriver != nullptr;

        default:
            return false;
    }
}

std::vector<AudioDriverType> AudioDeviceManager::getAvailableDriverTypes() const {
    std::vector<AudioDriverType> types;
    
    if (m_exclusiveDriver) {
        types.push_back(AudioDriverType::WASAPI_EXCLUSIVE);
    }
    if (m_sharedDriver) {
        types.push_back(AudioDriverType::WASAPI_SHARED);
    }
    if (m_rtAudioDriver) {
        types.push_back(AudioDriverType::RTAUDIO);
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

std::vector<ASIODriverInfo> AudioDeviceManager::getASIODrivers() const {
    return ASIODriverScanner::scanInstalledDrivers();
}

std::string AudioDeviceManager::getASIOInfo() const {
    return ASIODriverScanner::getAvailabilityMessage();
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

const char* getVersion() {
    return "1.0.0";
}

const char* getBackendName() {
    return "RtAudio WASAPI";
}

} // namespace Audio
} // namespace Nomad
