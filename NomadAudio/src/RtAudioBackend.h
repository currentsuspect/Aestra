// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioDriver.h"
#include "RtAudio.h"
#include <memory>

namespace Nomad {
namespace Audio {

/**
 * @brief RtAudio implementation of AudioDriver
 */
class RtAudioBackend : public AudioDriver {
public:
    RtAudioBackend();
    ~RtAudioBackend() override;

    std::vector<AudioDeviceInfo> getDevices() override;
    uint32_t getDefaultOutputDevice() override;
    uint32_t getDefaultInputDevice() override;
    bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) override;
    void closeStream() override;
    bool startStream() override;
    void stopStream() override;
    bool isStreamRunning() const override;
    double getStreamLatency() const override;
    
    // Get the actual API being used
    RtAudio::Api getCurrentApi() const { return m_rtAudio ? m_rtAudio->getCurrentApi() : RtAudio::UNSPECIFIED; }

private:
    std::unique_ptr<RtAudio> m_rtAudio;
    AudioCallback m_userCallback;
    void* m_userData;

    // RtAudio callback wrapper
    static int rtAudioCallback(
        void* outputBuffer,
        void* inputBuffer,
        unsigned int numFrames,
        double streamTime,
        RtAudioStreamStatus status,
        void* userData
    );
};

} // namespace Audio
} // namespace Nomad
