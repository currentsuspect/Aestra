#include "RtAudioDriver.h"

#include <algorithm>
#include <cstring>
#include <iostream>

// Logging macros
#ifndef AESTRA_LOG_INFO
#define AESTRA_LOG_INFO(x) std::cout << "[INFO] " << x << std::endl
#define AESTRA_LOG_WARN(x) std::cerr << "[WARN] " << x << std::endl
#define AESTRA_LOG_ERROR(x) std::cerr << "[ERROR] " << x << std::endl
#endif

namespace Aestra {
namespace Audio {

namespace {
bool isRtAudioOk(RtAudioErrorType error) {
    return error == RTAUDIO_NO_ERROR;
}
}

RtAudioDriver::RtAudioDriver() {
    // macOS uses CoreAudio
    rtAudio_ = std::make_unique<RtAudio>(RtAudio::MACOSX_CORE);
    AESTRA_LOG_INFO("RtAudio initialized with CoreAudio backend");

    if (rtAudio_->getDeviceIds().empty()) {
        AESTRA_LOG_WARN("No audio devices found!");
    }
}

RtAudioDriver::~RtAudioDriver() {
    closeDevice();
}

std::vector<AudioDeviceInfo> RtAudioDriver::enumerateDevices() {
    std::vector<AudioDeviceInfo> devices;
    if (!rtAudio_)
        return devices;

    const auto deviceIds = rtAudio_->getDeviceIds();
    const unsigned int defaultOutputId = rtAudio_->getDefaultOutputDevice();
    const unsigned int defaultInputId = rtAudio_->getDefaultInputDevice();

    for (unsigned int deviceId : deviceIds) {
        RtAudio::DeviceInfo info = rtAudio_->getDeviceInfo(deviceId);

        if (info.outputChannels > 0 || info.inputChannels > 0) {
            AudioDeviceInfo device{};
            device.id = deviceId;
            device.name = info.name;
            device.maxOutputChannels = info.outputChannels;
            device.maxInputChannels = info.inputChannels;
            device.preferredSampleRate = info.preferredSampleRate;
            device.isDefaultOutput = (deviceId == defaultOutputId);
            device.isDefaultInput = (deviceId == defaultInputId);

            for (auto sr : info.sampleRates) {
                device.supportedSampleRates.push_back(sr);
            }

            devices.push_back(device);
        }
    }

    return devices;
}

bool RtAudioDriver::openDevice(const AudioDeviceConfig& config) {
    if (isStreamOpen_)
        closeDevice();

    if (!rtAudio_) {
        AESTRA_LOG_ERROR("RtAudio not initialized");
        return false;
    }

    const unsigned int deviceId = config.deviceId;
    bufferSize_ = config.bufferSize;
    numOutputChannels_ = config.numOutputChannels > 0 ? config.numOutputChannels : 2;
    const unsigned int numInputChannels = config.numInputChannels;

    outputParams_.deviceId = deviceId;
    outputParams_.nChannels = numOutputChannels_;
    outputParams_.firstChannel = 0;

    RtAudio::StreamParameters* inputParams = nullptr;
    if (numInputChannels > 0) {
        inputParams_.deviceId = deviceId;
        inputParams_.nChannels = numInputChannels;
        inputParams_.firstChannel = 0;
        inputParams = &inputParams_;
    }

    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_NONINTERLEAVED;
    options.numberOfBuffers = 2;
    options.priority = 0;

    unsigned int bufferFrames = bufferSize_;
    const RtAudioErrorType error = rtAudio_->openStream(
        &outputParams_, inputParams, RTAUDIO_FLOAT32, config.sampleRate, &bufferFrames, &RtAudioDriver::rtAudioCallback,
        this, &options);
    if (!isRtAudioOk(error)) {
        AESTRA_LOG_ERROR("Failed to open audio device");
        return false;
    }

    bufferSize_ = bufferFrames;
    outputBuffers_.resize(numOutputChannels_);
    for (auto& buf : outputBuffers_) {
        buf.resize(bufferSize_);
    }
    outputBufferPtrs_.resize(numOutputChannels_);

    isStreamOpen_ = true;

    RtAudio::DeviceInfo info = rtAudio_->getDeviceInfo(deviceId);
    AESTRA_LOG_INFO("Audio device opened: " << info.name << " @ " << config.sampleRate << "Hz");
    return true;
}

void RtAudioDriver::closeDevice() {
    stopStream();
    if (isStreamOpen_ && rtAudio_) {
        try {
            rtAudio_->closeStream();
        } catch (...) {}
        isStreamOpen_ = false;
    }
}

bool RtAudioDriver::startStream(IAudioCallback* callback) {
    if (!isStreamOpen_ || isStreamRunning_)
        return false;

    callback_.store(callback, std::memory_order_release);

    if (!isRtAudioOk(rtAudio_->startStream())) {
        AESTRA_LOG_ERROR("Failed to start audio stream");
        callback_.store(nullptr);
        return false;
    }

    isStreamRunning_ = true;
    AESTRA_LOG_INFO("Audio stream started");
    return true;
}

void RtAudioDriver::stopStream() {
    if (!isStreamRunning_ || !rtAudio_)
        return;

    try {
        rtAudio_->stopStream();
    } catch (...) {}

    isStreamRunning_ = false;
    callback_.store(nullptr);
}

int RtAudioDriver::rtAudioCallback(void* outputBuffer, void* inputBuffer, unsigned int nFrames,
                                   double streamTime, RtAudioStreamStatus status, void* userData) {
    (void)streamTime;
    
    auto* driver = static_cast<RtAudioDriver*>(userData);

    if (status) {
        driver->xrunCount_++;
    }

    IAudioCallback* cb = driver->callback_.load(std::memory_order_acquire);
    if (!cb) {
        // Silence
        float* out = static_cast<float*>(outputBuffer);
        std::memset(out, 0, nFrames * driver->numOutputChannels_ * sizeof(float));
        return 0;
    }

    // Setup non-interleaved buffer pointers
    float* out = static_cast<float*>(outputBuffer);
    for (unsigned int ch = 0; ch < driver->numOutputChannels_; ch++) {
        driver->outputBufferPtrs_[ch] = out + (ch * nFrames);
    }

    // Process audio through callback
    const float* in = static_cast<const float*>(inputBuffer);
    
    // Call the audio callback with non-interleaved data
    cb->process(in, driver->outputBufferPtrs_[0], nFrames);

    return 0;
}

bool RtAudioDriver::supportsExclusiveMode() const {
    // CoreAudio on macOS handles this differently; no exclusive mode in the traditional sense
    return false;
}

} // namespace Audio
} // namespace Aestra
