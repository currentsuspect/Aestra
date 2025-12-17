#pragma once
#include "../../include/IAudioDriver.h"
#include <RtAudio.h>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <functional>

namespace NomadAudio {

class RtAudioDriver : public IAudioDriver {
public:
    RtAudioDriver();
    ~RtAudioDriver() override;

    // IAudioDriver interface
    std::string getDriverName() const override { return "RtAudio"; }
    std::vector<AudioDeviceInfo> enumerateDevices() override;
    bool openDevice(const AudioDeviceConfig& config) override;
    void closeDevice() override;
    bool startStream(IAudioCallback* callback) override;
    void stopStream() override;
    bool isStreamOpen() const override { return isStreamOpen_; }
    bool isStreamRunning() const override { return isStreamRunning_; }
    double getStreamCpuLoad() const override { return 0.0; } // RtAudio doesn't provide this directly
    bool supportsExclusiveMode() const override;

    // Internal callback
    static int rtAudioCallback(void* outputBuffer, void* inputBuffer, unsigned int nFrames,
                             double streamTime, RtAudioStreamStatus status, void* userData);

private:
    std::unique_ptr<RtAudio> rtAudio_;
    RtAudio::StreamParameters outputParams_;
    RtAudio::StreamParameters inputParams_;
    bool isStreamOpen_ = false;
    bool isStreamRunning_ = false;

    std::atomic<IAudioCallback*> callback_{nullptr};
    std::vector<float> inputBuffer_;
    std::vector<float> outputBuffer_; // If we need intermediate buffer, though RtAudio gives direct access
    
    // Monitoring
    std::atomic<uint64_t> xrunCount_{0};
    
    // Helper to check device connection
    bool isDeviceStillConnected();
};

} // namespace NomadAudio
