// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "IAudioDriver.h"
#include "RtAudio.h"

#include <atomic>
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

class RtAudioDriver : public IAudioDriver {
public:
    RtAudioDriver();
    ~RtAudioDriver() override;

    std::string getDisplayName() const override;
    AudioDriverType getDriverType() const override;
    bool isAvailable() const override;

    std::vector<AudioDeviceInfo> getDevices() override;

    bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) override;
    void closeStream() override;
    bool startStream() override;
    void stopStream() override;

    bool isStreamRunning() const override;
    double getStreamLatency() const override;
    uint32_t getStreamSampleRate() const override;
    uint32_t getStreamBufferSize() const override;
    DriverStatistics getStatistics() const override { return m_stats; }
    std::string getErrorMessage() const override { return m_lastError; }

    void setDitheringEnabled(bool enabled) override { (void)enabled; }
    bool isDitheringEnabled() const override { return false; }

private:
    static int rtAudioCallback(void* outputBuffer, void* inputBuffer, unsigned int numFrames, double streamTime,
                               RtAudioStreamStatus status, void* userData);

    bool tryInitializeBackend(const std::vector<RtAudio::Api>& candidates);
    static AudioDriverType apiToDriverType(RtAudio::Api api);
    static const char* apiName(RtAudio::Api api);

    std::unique_ptr<RtAudio> m_rtAudio;
    AudioDriverType m_driverType{AudioDriverType::UNKNOWN};
    std::atomic<AudioCallback> m_userCallback{nullptr};
    std::atomic<void*> m_userData{nullptr};
    std::atomic<uint32_t> m_bufferSize{0};
    DriverStatistics m_stats;
    std::string m_lastError;
};

} // namespace Audio
} // namespace Aestra
