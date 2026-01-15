// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "IAudioDriver.h"
#include "ASIOInterface.h"
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>

namespace Nomad {
namespace Audio {

/**
 * @brief ASIO Driver implementation for low-latency professional audio.
 *
 * Handles COM interaction, buffer switching, and format conversion between
 * ASIO planar buffers and AudioEngine interleaved buffers.
 */
class ASIODriver : public IAudioDriver {
public:
    ASIODriver();
    virtual ~ASIODriver();

    // IAudioDriver Implementation =============================================
    std::string getDisplayName() const override { return m_driverName; }
    AudioDriverType getDriverType() const override { return AudioDriverType::ASIO_EXTERNAL; }
    bool isAvailable() const override;

    std::vector<AudioDeviceInfo> getDevices() override;

    bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) override;
    void closeStream() override;
    bool startStream() override;
    void stopStream() override;

    bool isStreamRunning() const override { return m_isRunning.load(); }
    double getStreamLatency() const override;
    uint32_t getStreamSampleRate() const override;
    uint32_t getStreamBufferSize() const override;
    DriverStatistics getStatistics() const override { return m_stats; }
    std::string getErrorMessage() const override { return m_lastError; }

    void setDitheringEnabled(bool enabled) override { m_ditherEnabled = enabled; }
    bool isDitheringEnabled() const override { return m_ditherEnabled; }
    bool supportsExclusiveMode() const override { return true; } // ASIO is always exclusive-ish

    // Internal ASIO Callback Routing ==========================================
    // These must be public to be called by the C-style static wrapper
    ASIO::ASIOTime* asioBufferSwitch(long doubleBufferIndex, long directProcess);
    void asioSampleRateDidChange(double sRate);
    long asioMessage(long selector, long value, void* message, double* opt);

private:
    // Helper to load driver by index
    bool loadDriver(uint32_t deviceIndex);
    bool initASIO();
    bool createBuffers(const AudioStreamConfig& config);
    
    // Format conversion helpers
    void convertInput(long index, float* dest, size_t frames);
    void convertOutput(long index, const float* src, size_t frames);

    // Member Variables
    std::string m_driverName = "ASIO Wrapper";
    std::string m_lastError;
    
    // Driver Registry (Index -> CLSID)
    struct DriverEntry {
        std::string name;
        std::string clsid;
    };
    std::vector<DriverEntry> m_knownDrivers;
    
    ASIO::IASIO* m_asio = nullptr;
    bool m_driversScanned = false;
    std::atomic<bool> m_isRunning{false};
    bool m_ditherEnabled = false;

    // Stream State
    AudioCallback m_callback = nullptr;
    void* m_callbackUserData = nullptr;
    
    std::vector<ASIO::ASIOBufferInfo> m_bufferInfos;
    std::vector<ASIO::ASIOChannelInfo> m_channelInfos;
    
    long m_inputChannels = 0;
    long m_outputChannels = 0;
    long m_configuredInputChannels = 0;
    long m_configuredOutputChannels = 0;
    long m_bufferSize = 0;
    double m_sampleRate = 44100.0;
    
    // Scratch Buffers for Interleaving/Deinterleaving
    // ASIO is planar (LLLL RRRR), AudioEngine is Interleaved (LRLRLRLR)
    std::vector<float> m_interleavedInput;
    std::vector<float> m_interleavedOutput;
    
    DriverStatistics m_stats;
    
    // Static instance for callback routing (Public for global wrappers)
public:
    static ASIODriver* s_instance;
    static ASIO::ASIOCallbacks s_asioCallbacks;
};

} // namespace Audio
} // namespace Nomad
