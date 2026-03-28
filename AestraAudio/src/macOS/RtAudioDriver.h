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

// Audio device configuration for RtAudio
struct AudioDeviceConfig {
    unsigned int deviceId{0};
    unsigned int sampleRate{48000};
    unsigned int bufferSize{512};
    unsigned int inputChannels{0};
    unsigned int outputChannels{2};
    // Compatibility alias
    unsigned int numInputChannels{0};
    unsigned int numOutputChannels{2};
};

class IAudioCallback {
public:
    virtual ~IAudioCallback() = default;
    virtual void process(const float* input, float* output, unsigned int frames) = 0;
};

// RtAudio driver for macOS using CoreAudio backend
class RtAudioDriver {
public:
    RtAudioDriver();
    ~RtAudioDriver();

    std::string getDriverName() const { return "RtAudio (CoreAudio)"; }
    std::vector<AudioDeviceInfo> enumerateDevices();
    bool openDevice(const AudioDeviceConfig& config);
    void closeDevice();
    bool startStream(IAudioCallback* callback);
    void stopStream();
    bool isStreamOpen() const { return isStreamOpen_; }
    bool isStreamRunning() const { return isStreamRunning_; }
    double getStreamCpuLoad() const { return 0.0; }
    bool supportsExclusiveMode() const;

    // Internal callback (static)
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
    std::vector<std::vector<float>> outputBuffers_; // Non-interleaved channel buffers
    std::vector<float*> outputBufferPtrs_;         // Pointers for callback
    unsigned int bufferSize_ = 512;
    unsigned int numOutputChannels_ = 2;

    // Monitoring
    std::atomic<uint64_t> xrunCount_{0};
};

} // namespace Audio
} // namespace Aestra
