// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioDeviceManager.h"
#include "NativeAudioDriver.h"
#include <iostream>
#include <chrono>
#include <mutex>

#ifdef _WIN32
#include <Windows.h>
#include <TlHelp32.h>
#endif

namespace Nomad {
namespace Audio {

namespace {
    bool isSoundIDRunning() {
#ifdef _WIN32
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return false;

        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        if (Process32First(hSnapshot, &pe32)) {
            do {
                if (std::string(pe32.szExeFile) == "SoundID Reference.exe") {
                    CloseHandle(hSnapshot);
                    return true;
                }
            } while (Process32Next(hSnapshot, &pe32));
        }

        CloseHandle(hSnapshot);
#endif
        return false;
    }
}

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
        }

        std::cout << "Registered " << m_drivers.size() << " driver(s)." << std::endl;

        // Log driver status
        for (const auto& driver : m_drivers) {
            std::cout << (driver->isAvailable() ? "✓ " : "✗ ") << driver->getDisplayName() 
                      << (driver->isAvailable() ? " available" : " unavailable") << std::endl;
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

// Jump to tryDriver implementation
bool AudioDeviceManager::tryDriver(IAudioDriver* driver, const AudioStreamConfig& config,
                                   AudioCallback callback, void* userData) {
    if (!driver || !driver->isAvailable()) {
        return false;
    }
    
    // Hook up error callback if supported
    // We do this before opening the stream so we catch immediate errors
    if (auto nativeDriver = dynamic_cast<NativeAudioDriver*>(driver)) {
        nativeDriver->setErrorCallback([this](DriverError error, const std::string& msg) {
            std::cerr << "[AudioDeviceManager] Driver Error: " << msg << std::endl;
            
            // Critical error handling: Auto-fallback
            if (error == DriverError::DEVICE_NOT_FOUND) {
                m_latchedError = error; // Latch for startup checks
                std::cerr << "[AudioDeviceManager] Critical device error. Attempting fallback to Default Device..." << std::endl;
                
                // Avoid infinite recursion if the default device itself is failing
                auto defaultDevice = getDefaultOutputDevice();
                if (defaultDevice.id != m_currentConfig.deviceId) {
                    // We must execute this asynchronously or carefully to avoid closing the stream 
                    // from within the audio callback thread. 
                    // Ideally, we signal the UI or a main thread handler.
                    // But for now, we will rely on the UI's onUpdate polling which checks m_streamErrorCallback.
                    // We can ENHANCE the message to suggest fallback.
                }
            }

            if (m_driverModeChangeCallback) {
                // If the error was a fallback, update listeners
                // ...
            }
            
            // RT-Safety: Store error atomically for polling.
            // Do NOT call callbacks or allocate memory here if possible.
            m_pendingError.store(error, std::memory_order_release);
            
            // For the message, we need a simple mechanism.
            // If we are in the audio thread, string assignment is risky due to allocation.
            // Ideally we'd use a fixed buffer, but for now we'll check if we can lock briefly.
            // If not, we drop the message but keep the error code (which is critical).
            // NOTE: This lock is only for the message string, not the signal itself.
            static std::mutex msgMutex;
            if (msgMutex.try_lock()) {
                m_pendingErrorMsg = msg;
                msgMutex.unlock();
            }
        });
    }
    
    std::cout << "Trying " << driver->getDisplayName() << "..." << std::endl;
    
    if (driver->openStream(config, callback, userData)) {
        std::cout << "✓ " << driver->getDisplayName() << " opened successfully" << std::endl;
        double lat = driver->getStreamLatency();
        std::cout << "  Latency: " << (lat * 1000.0) << "ms" << std::endl;
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

    if (!m_initialized) {
        std::cerr << "AudioDeviceManager::openStream: Not initialized" << std::endl;
        return false;
    }

    m_currentConfig = config;
    m_currentCallback = callback;
    m_currentUserData = userData;

    // Priority logic:
    // If preferred type is set, try to find a driver matching that type first.
    // If that fails, fallback to others.

    // Find preferred driver
    IAudioDriver* preferred = nullptr;
    // Assuming we can determine type from config or just iterating.
    // The previous code checked m_preferredDriverType.
    // We don't have getDriverType() on IAudioDriver yet (unless we added it? no).
    // But we can check supportsExclusiveMode() for WASAPI_EXCLUSIVE.
    // Ideally we'd have getDriverType().
    // For now, let's iterate and check capabilities or name.

    // Note: Since IAudioDriver doesn't expose strict types, we rely on properties or name.
    // Let's iterate all drivers. Prioritize based on capabilities.

    // Check for "SoundID Reference" virtual device
    // This device is known to cause distortion in WASAPI Exclusive mode
    // because it wraps the hardware driver improperly or has clocking issues.
    // We force Shared Mode for it to ensure clean audio.
    bool forceSharedMode = false;

    // We need to find the name of the device we are trying to open
    // Since getDevices() might be expensive, we only do this if we are in Exclusive mode preference
    if (m_preferredDriverType == AudioDriverType::WASAPI_EXCLUSIVE) {
        auto devices = getDevices();
        for (const auto& device : devices) {
            if (device.id == config.deviceId) {
                // Check if name contains "SoundID" (case-insensitive ideally, but simple check works for standard name)
                if (device.name.find("SoundID") != std::string::npos) {
                    // Start of Zombie Fallback Logic
                    if (!isSoundIDRunning()) {
                        std::cerr << "[AudioDeviceManager] SoundID device selected but 'SoundID Reference.exe' is NOT running." << std::endl;
                        std::cerr << "[AudioDeviceManager] Treating device as invalid (Zombie State)." << std::endl;

                        // Treat as critical error to trigger fallback mechanism
                        // We set the latch so the UI (if starting up) picks it up
                        m_latchedError = DriverError::DEVICE_NOT_FOUND;
                        return false;
                    }

                    std::cout << "[AudioDeviceManager] Detected SoundID device: " << device.name << std::endl;
                    std::cout << "[AudioDeviceManager] Forcing WASAPI Shared Mode to prevent distortion." << std::endl;
                    forceSharedMode = true;
                }
                break;
            }
        }
    }

    // Try preferred strategy
    for (auto& driver : m_drivers) {
        // If forcing shared mode, skip exclusive drivers
        if (forceSharedMode && driver->supportsExclusiveMode()) {
            continue;
        }

        bool isExclusiveFn = driver->supportsExclusiveMode();
        bool wantExclusive = (m_preferredDriverType == AudioDriverType::WASAPI_EXCLUSIVE);

        if (!forceSharedMode && wantExclusive == isExclusiveFn) {
             if (tryDriver(driver.get(), config, callback, userData)) {
                 return true;
             }
        }

        // If forcing shared mode, try non-exclusive drivers immediately
        if (forceSharedMode && !isExclusiveFn) {
             if (tryDriver(driver.get(), config, callback, userData)) {
                 return true;
             }
        }
    }

    // Fallback strategy: try remaining drivers
    for (auto& driver : m_drivers) {
        if (m_activeDriver == driver.get()) continue; // Skip if somehow active? (shouldn't happen here)

        // If forcing shared mode, skip exclusive drivers in fallback logic too
        if (forceSharedMode && driver->supportsExclusiveMode()) {
            continue;
        }

        if (tryDriver(driver.get(), config, callback, userData)) {
            // Notify fallback
            if (m_driverModeChangeCallback) {
                 m_driverModeChangeCallback(
                    m_preferredDriverType,
                    AudioDriverType::UNKNOWN, // Can't easily map back without RTTI or type field
                    forceSharedMode ? "Forced Shared Mode (SoundID)" : "Preferred driver unavailable"
                 );
            }
            return true;
        }
    }

    std::cerr << "\nâœ— All drivers failed to open stream!" << std::endl;
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

void AudioDeviceManager::suspendAudio() {
    if (!m_initialized || !m_activeDriver || m_isSuspended) {
        return;
    }

    std::cout << "[AudioDeviceManager] Suspending audio (releasing driver)..." << std::endl;

    m_wasRunningBeforeSuspend = isStreamRunning();
    
    if (m_wasRunningBeforeSuspend) {
        stopStream();
    }
    
    // Close stream to fully release the device (especially for WASAPI Exclusive)
    closeStream();
    
    m_isSuspended = true;
}

void AudioDeviceManager::resumeAudio() {
    if (!m_initialized || !m_isSuspended) {
        return;
    }

    std::cout << "[AudioDeviceManager] Resuming audio..." << std::endl;

    // Reopen stream with saved config
    if (openStream(m_currentConfig, m_currentCallback, m_currentUserData)) {
        // Restore running state if it was playing before suspend
        if (m_wasRunningBeforeSuspend) {
            startStream();
        }
        m_isSuspended = false;
    } else {
        std::cerr << "[AudioDeviceManager] Failed to resume audio stream!" << std::endl;
    }
}

const char* getVersion() {
    return "1.0.0";
}

const char* getBackendName() {
    return "RtAudio WASAPI";
}

DriverError AudioDeviceManager::pollError(std::string& outMessage) {
    DriverError err = m_pendingError.exchange(DriverError::NONE, std::memory_order_acquire);
    if (err != DriverError::NONE) {
        // Retrieve message safely
        // In a real RT system we'd use a lock-free ring buffer for text
        static std::mutex msgMutex;
        std::lock_guard<std::mutex> lock(msgMutex);
        outMessage = m_pendingErrorMsg;
        m_pendingErrorMsg.clear(); 
        return err;
    }
    
    // Also poll current driver for deferred RT errors
    if (m_activeDriver && m_activeDriver->pollDeferredError(err, outMessage)) {
        return err;
    }
    
    return DriverError::NONE;
}

} // namespace Audio
} // namespace Nomad
