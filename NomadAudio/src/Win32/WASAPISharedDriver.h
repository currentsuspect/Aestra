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
struct IAudioClient3;
struct IAudioRenderClient;
struct IAudioCaptureClient;

namespace Nomad {
namespace Audio {

/**
 * @brief WASAPI Shared Mode Driver
 * 
 * Default safe mode for all Windows users.
 * Features:
 * - Automatic sample rate conversion
 * - Shared device access
 * - Latency tuning
 * - Thread priority management
 * - Underrun detection and recovery
 */
class WASAPISharedDriver : public NativeAudioDriver {
public:
    WASAPISharedDriver();
    ~WASAPISharedDriver() override;

    // NativeAudioDriver interface
    std::string getDisplayName() const override { return "WASAPI Shared"; }
    AudioDriverType getDriverType() const override { return AudioDriverType::WASAPI_SHARED; }
    DriverCapability getCapabilities() const override;
    DriverState getState() const override { return m_state; }
    DriverError getLastError() const override { return m_lastError; }
    std::string getErrorMessage() const override { return m_errorMessage; }
    DriverStatistics getStatistics() const override { return m_statistics; }
    void resetStatistics() override { m_statistics.reset(); }
    bool initialize() override;
    void shutdown() override;
    bool isAvailable() const override;
    float getTypicalLatencyMs() const override { return 15.0f; }
    void setErrorCallback(ErrorCallback callback) override { m_errorCallback = callback; }

    bool supportsExclusiveMode() const override { return false; }
    
    // Defer RT errors to polling thread
    bool pollDeferredError(DriverError& outError, std::string& outMsg) override;


    // AudioDriver interface
    virtual std::vector<AudioDeviceInfo> getDevices() const override;

    // Native implementation details
    uint32_t getDefaultOutputDevice();
    uint32_t getDefaultInputDevice();
    bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) override;
    void closeStream() override;
    bool startStream() override;
    void stopStream() override;
    bool isStreamRunning() const override { return m_isRunning; }
    double getStreamLatency() const override;
    uint32_t getStreamSampleRate() const override;
    uint32_t getStreamBufferSize() const override {
        return isStreamRunning() ? m_bufferFrameCount : 0;
    }

private:
    // COM interfaces (opaque pointers - Windows-specific implementation)
    void* m_deviceEnumerator = nullptr;  // IMMDeviceEnumerator*
    void* m_device = nullptr;  // IMMDevice*
    void* m_audioClient = nullptr;  // IAudioClient*
    void* m_audioClient3 = nullptr;  // IAudioClient3* (for low-latency shared mode, Win10+)
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
    
    // Deferred Error Handling (Atomic/Lock-free for RT safety)
    std::atomic<bool> m_hasDeferredError{ false };
    std::atomic<DriverError> m_deferredError{ DriverError::NONE };
    std::atomic<uint32_t> m_deferredHResult{ 0 };

    // Stream configuration
    AudioStreamConfig m_config;
    AudioCallback m_userCallback = nullptr;
    void* m_userData = nullptr;

    // Format information
    void* m_waveFormat = nullptr;  // WAVEFORMATEX* (opaque)
    uint32_t m_bufferFrameCount = 0;

    // Performance monitoring (Windows-specific, only accessed in .cpp)
    // Using uint64_t to avoid Windows types in header
    uint64_t m_perfFreq = 0;
    uint64_t m_lastCallbackTime = 0;

    // Internal methods
    bool initializeCOM();
    void shutdownCOM();
    bool enumerateDevices(std::vector<AudioDeviceInfo>& devices) const;
    bool openDevice(uint32_t deviceId);
    void closeDevice();
    bool initializeAudioClient();
    void audioThreadProc();
    void fillAudioBufferWithSilence();
    void setError(DriverError error, const std::string& message);
    void updateStatistics(double callbackTimeUs);
    bool setThreadPriority();
};

} // namespace Audio
} // namespace Nomad
