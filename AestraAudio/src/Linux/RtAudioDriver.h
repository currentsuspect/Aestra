#pragma once
#include "../../include/Drivers/IAudioDriver.h"

#include <RtAudio.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace Aestra {
namespace Audio {

// STUB: Legacy types used by RtAudioDriver — Phase 2 will unify with IAudioDriver types
struct AudioDeviceConfig {
    unsigned int deviceId{0};
    unsigned int sampleRate{48000};
    unsigned int bufferSize{512};
    unsigned int inputChannels{0};
    unsigned int outputChannels{2};
};

class IAudioCallback {
public:
    virtual ~IAudioCallback() = default;
    virtual void process(const float* input, float* output, unsigned int frames) = 0;
};

// STUB: RtAudioDriver — standalone Linux audio driver, not yet conforming to IAudioDriver interface
// Phase 2 will align this with the IAudioDriver API (openStream, getDevices, etc.)
class RtAudioDriver {
public:
    RtAudioDriver();
    ~RtAudioDriver();

    // IAudioDriver interface
    std::string getDriverName() const { return "RtAudio"; }
    std::vector<AudioDeviceInfo> enumerateDevices();
    bool openDevice(const AudioDeviceConfig& config);
    void closeDevice();
    bool startStream(IAudioCallback* callback);
    void stopStream();
    bool isStreamOpen() const { return isStreamOpen_; }
    bool isStreamRunning() const { return isStreamRunning_; }
    double getStreamCpuLoad() const { return 0.0; } // RtAudio doesn't provide this directly
    bool supportsExclusiveMode() const;

    // Internal callback
    static int rtAudioCallback(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime,
                               RtAudioStreamStatus status, void* userData);

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

} // namespace Audio
} // namespace Aestra
