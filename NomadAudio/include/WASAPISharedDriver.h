// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NativeAudioDriver.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <atomic>
#include <mutex>

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

    // AudioDriver interface
    std::vector<AudioDeviceInfo> getDevices() override;
    uint32_t getDefaultOutputDevice() override;
    uint32_t getDefaultInputDevice() override;
    bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) override;
    void closeStream() override;
    bool startStream() override;
    void stopStream() override;
    bool isStreamRunning() const override { return m_isRunning; }
    double getStreamLatency() const override;
    uint32_t getStreamSampleRate() const override { return m_waveFormat ? m_waveFormat->nSamplesPerSec : 0; }
    uint32_t getStreamBufferSize() const override { return m_bufferFrameCount; }

private:
    // COM interfaces
    IMMDeviceEnumerator* m_deviceEnumerator = nullptr;
    IMMDevice* m_device = nullptr;
    IAudioClient* m_audioClient = nullptr;
    IAudioClient3* m_audioClient3 = nullptr;  // For low-latency shared mode (Win10+)
    IAudioRenderClient* m_renderClient = nullptr;
    IAudioCaptureClient* m_captureClient = nullptr;

    // Thread management
    std::thread m_audioThread;
    std::atomic<bool> m_isRunning{ false };
    std::atomic<bool> m_shouldStop{ false };
    HANDLE m_audioEvent = nullptr;

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
    WAVEFORMATEX* m_waveFormat = nullptr;
    uint32_t m_bufferFrameCount = 0;

    // Performance monitoring
    LARGE_INTEGER m_perfFreq;
    LARGE_INTEGER m_lastCallbackTime;

    // Internal methods
    bool initializeCOM();
    void shutdownCOM();
    bool enumerateDevices(std::vector<AudioDeviceInfo>& devices);
    bool openDevice(uint32_t deviceId);
    void closeDevice();
    bool initializeAudioClient();
    void audioThreadProc();
    void setError(DriverError error, const std::string& message);
    void updateStatistics(double callbackTimeUs);
    bool setThreadPriority();
};

} // namespace Audio
} // namespace Nomad
