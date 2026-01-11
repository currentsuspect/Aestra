// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NativeAudioDriver.h"
#include <thread>
#include <atomic>
#include <mutex>
#include "NativeAudioDriver.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

// Forward declarations for Windows types (implementation details)
// Note: Using opaque pointers to avoid Windows type leaks
struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioRenderClient;
struct IAudioCaptureClient;

namespace Nomad {
namespace Audio {

/**
 * @brief WASAPI Exclusive Mode Driver
 * 
 * Professional low-latency mode for NOMAD.
 * Features:
 * - Exclusive device access (no mixing)
 * - Event-driven callbacks
 * - Sample-rate matching
 * - Ultra-low latency (~3-5ms achievable)
 * - Direct hardware control
 */
class WASAPIExclusiveDriver : public NativeAudioDriver {
public:
    WASAPIExclusiveDriver();
    ~WASAPIExclusiveDriver() override;

    // NativeAudioDriver interface
    std::string getDisplayName() const override { return "WASAPI Exclusive"; }
    AudioDriverType getDriverType() const override { return AudioDriverType::WASAPI_EXCLUSIVE; }
    DriverCapability getCapabilities() const override;
    DriverState getState() const override { return m_state; }
    DriverError getLastError() const override { return m_lastError; }
    std::string getErrorMessage() const override { return m_errorMessage; }
    DriverStatistics getStatistics() const override { return m_statistics; }
    void resetStatistics() override { m_statistics.reset(); }
    bool initialize() override;
    void shutdown() override;
    bool isAvailable() const override;
    float getTypicalLatencyMs() const override { return 5.0f; }
    void setErrorCallback(ErrorCallback callback) override { m_errorCallback = callback; }

    bool supportsExclusiveMode() const override { return true; }

    // AudioDriver interface
    virtual std::vector<AudioDeviceInfo> getDevices() override;

    // Native implementation details
    uint32_t getDefaultOutputDevice();
    uint32_t getDefaultInputDevice();
    bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) override;
    void closeStream() override;
    bool startStream() override;
    void stopStream() override;
    bool isStreamRunning() const override { return m_isRunning; }
    double getStreamLatency() const override;
    uint32_t getStreamSampleRate() const override {
        return isStreamRunning() ? m_actualSampleRate : 0;
    }
    uint32_t getStreamBufferSize() const override {
        return isStreamRunning() ? m_bufferFrameCount : 0;
    }

    /**
     * @brief Check if exclusive mode is available for a device
     */
    bool isExclusiveModeAvailable(uint32_t deviceId);

    /**
     * @brief Get supported sample rates in exclusive mode
     */
    std::vector<uint32_t> getSupportedExclusiveSampleRates(uint32_t deviceIndex);

private:
    // COM interfaces (opaque pointers - Windows-specific implementation)
    void* m_deviceEnumerator = nullptr;  // IMMDeviceEnumerator*
    void* m_device = nullptr;  // IMMDevice*
    void* m_audioClient = nullptr;  // IAudioClient*
    void* m_renderClient = nullptr;  // IAudioRenderClient*
    void* m_captureClient = nullptr;  // IAudioCaptureClient*

    // Thread management
    std::thread m_audioThread;
    std::atomic<bool> m_isRunning{ false };
    std::atomic<bool> m_shouldStop{ false };
    void* m_audioEvent = nullptr;  // HANDLE (opaque)

    // State
    DriverState m_state = DriverState::UNINITIALIZED;
    DriverError m_lastError = DriverError::NONE;
    std::string m_errorMessage;
    DriverStatistics m_statistics;
    ErrorCallback m_errorCallback;

    // Stream configuration
    AudioStreamConfig m_config;
    AudioCallback m_userCallback = nullptr;
    void* m_userData = nullptr;

    // Format information
    void* m_waveFormat = nullptr;  // WAVEFORMATEX* (opaque)
    uint32_t m_bufferFrameCount = 0;
    uint32_t m_actualSampleRate = 0;
    
    // Soft-start ramp to prevent harsh audio on initialization
    uint32_t m_rampSampleCount = 0;
    uint32_t m_rampDurationSamples = 0;
    bool m_isRamping = false;

    // Performance monitoring (Windows-specific, only accessed in .cpp)
    // Using uint64_t to avoid Windows types in header
    uint64_t m_perfFreq = 0;
    uint64_t m_lastCallbackTime = 0;

    // Internal methods
    bool initializeCOM();
    void shutdownCOM();
    bool enumerateDevices(std::vector<AudioDeviceInfo>& devices);
    bool openDevice(uint32_t deviceId);
    void closeDevice();
    bool initializeAudioClient();
    bool initializeSharedFallback();
    bool findBestExclusiveFormat(void** format);  // WAVEFORMATEX** (opaque)
    bool testExclusiveFormat(uint32_t sampleRate, uint32_t channels, void** format);  // WAVEFORMATEX** (opaque)
    bool testExclusiveFormatPCM(uint32_t sampleRate, uint32_t channels, uint32_t bitsPerSample, void** format);  // WAVEFORMATEX** (opaque)
    void audioThreadProc();
    void fillAudioBufferWithSilence();
    void setError(DriverError error, const std::string& message);
    void updateStatistics(double callbackTimeUs);
    bool setThreadPriority();

    bool m_usingSharedFallback{ false };
};

} // namespace Audio
} // namespace Nomad
