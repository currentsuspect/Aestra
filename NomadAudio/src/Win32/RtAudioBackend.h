// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "IAudioDriver.h"
#include "RtAudio.h"
#include <memory>
#include <atomic>

namespace Nomad {
namespace Audio {

/**
 * @brief RtAudio implementation of IAudioDriver
 */
class RtAudioBackend : public IAudioDriver {
public:
    RtAudioBackend();
    ~RtAudioBackend() override;

    // Driver Information
    std::string getDisplayName() const override { return "RtAudio Backend"; }
    AudioDriverType getDriverType() const override { return AudioDriverType::RTAUDIO; }
    bool isAvailable() const override { return true; } // RtAudio usually always available (wrapper)

    // Device Enumeration
    std::vector<AudioDeviceInfo> getDevices() const override;

    // Stream Management
    bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) override;
    void closeStream() override;
    bool startStream() override;
    void stopStream() override;

    // Stream State
    bool isStreamRunning() const override;
    double getStreamLatency() const override;
    uint32_t getStreamSampleRate() const override;
    uint32_t getStreamBufferSize() const override;
    
    DriverStatistics getStatistics() const override { return m_stats; }
    std::string getErrorMessage() const override { return m_lastError; }

    // Initialization (legacy shim)
    bool initialize() { return true; }

    // Get the actual API being used
    RtAudio::Api getCurrentApi() const { return m_rtAudio ? m_rtAudio->getCurrentApi() : RtAudio::UNSPECIFIED; }

private:
    std::unique_ptr<RtAudio> m_rtAudio;
    std::atomic<AudioCallback> m_userCallback;
    std::atomic<void*> m_userData;
    std::atomic<uint32_t> m_bufferSize{0};  // Store buffer size after stream opened

    // RtAudio callback wrapper
    static int rtAudioCallback(
        void* outputBuffer,
        void* inputBuffer,
        unsigned int numFrames,
        double streamTime,
        RtAudioStreamStatus status,
        void* userData
    );

    DriverStatistics m_stats;
    std::string m_lastError;
};

} // namespace Audio
} // namespace Nomad
