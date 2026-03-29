// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RtAudioDriver.h"

#include <iostream>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {

RtAudioDriver::RtAudioDriver() {
    std::vector<RtAudio::Api> candidates;
#ifdef __LINUX_PULSE__
    candidates.push_back(RtAudio::LINUX_PULSE);
    std::cout << "[RtAudio] LINUX_PULSE defined, adding to candidates\n";
#endif
#ifdef __LINUX_ALSA__
    candidates.push_back(RtAudio::LINUX_ALSA);
    std::cout << "[RtAudio] LINUX_ALSA defined, adding to candidates\n";
#endif
#ifdef __UNIX_JACK__
    candidates.push_back(RtAudio::UNIX_JACK);
    std::cout << "[RtAudio] UNIX_JACK defined, adding to candidates\n";
#endif
    candidates.push_back(RtAudio::UNSPECIFIED);
    std::cout << "[RtAudio] Trying " << candidates.size() << " backends\n";

    if (!tryInitializeBackend(candidates)) {
        m_lastError = "No Linux RtAudio backend could be initialized";
        std::cerr << "[RtAudio] FAILED: " << m_lastError << "\n";
    } else {
        std::cout << "[RtAudio] SUCCESS: Backend initialized\n";
    }
}

RtAudioDriver::~RtAudioDriver() {
    closeStream();
}

std::string RtAudioDriver::getDisplayName() const {
    const char* api = apiName(m_rtAudio ? m_rtAudio->getCurrentApi() : RtAudio::UNSPECIFIED);
    return std::string("RtAudio (") + api + ")";
}

AudioDriverType RtAudioDriver::getDriverType() const {
    return m_driverType;
}

bool RtAudioDriver::isAvailable() const {
    return m_rtAudio != nullptr;
}

std::vector<AudioDeviceInfo> RtAudioDriver::getDevices() {
    std::vector<AudioDeviceInfo> devices;
    if (!m_rtAudio) {
        return devices;
    }

    try {
        for (unsigned int id : m_rtAudio->getDeviceIds()) {
            RtAudio::DeviceInfo rtInfo = m_rtAudio->getDeviceInfo(id);
            if (rtInfo.outputChannels == 0 && rtInfo.inputChannels == 0) {
                continue;
            }

            AudioDeviceInfo info{};
            info.id = id;
            info.name = rtInfo.name;
            info.maxInputChannels = rtInfo.inputChannels;
            info.maxOutputChannels = rtInfo.outputChannels;
            info.supportedSampleRates = rtInfo.sampleRates;
            info.preferredSampleRate = rtInfo.preferredSampleRate;
            info.isDefaultInput = rtInfo.isDefaultInput;
            info.isDefaultOutput = rtInfo.isDefaultOutput;
            devices.push_back(std::move(info));
        }
    } catch (const std::exception& e) {
        m_lastError = e.what();
    }

    return devices;
}

bool RtAudioDriver::openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) {
    if (!m_rtAudio) {
        m_lastError = "RtAudio backend unavailable";
        return false;
    }

    closeStream();

    RtAudio::StreamParameters outputParams{};
    outputParams.deviceId = config.deviceId;
    outputParams.nChannels = config.numOutputChannels;
    outputParams.firstChannel = 0;

    RtAudio::StreamParameters inputParamsData{};
    RtAudio::StreamParameters* inputParams = nullptr;
    if (config.numInputChannels > 0) {
        inputParamsData.deviceId = (config.inputDeviceId != 0) ? config.inputDeviceId : config.deviceId;
        inputParamsData.nChannels = config.numInputChannels;
        inputParamsData.firstChannel = 0;
        inputParams = &inputParamsData;
    }

    RtAudio::StreamOptions options{};
    options.flags = RTAUDIO_MINIMIZE_LATENCY;
    options.numberOfBuffers = 2;
    options.priority = 0;

    unsigned int sampleRate = config.sampleRate;
    unsigned int bufferFrames = config.bufferSize;

    m_userCallback.store(callback, std::memory_order_relaxed);
    m_userData.store(userData, std::memory_order_relaxed);
    m_lastError.clear();

    RtAudioErrorType error = m_rtAudio->openStream(&outputParams, inputParams, RTAUDIO_FLOAT32, sampleRate,
                                                   &bufferFrames, &RtAudioDriver::rtAudioCallback, this, &options);
    if (error != RTAUDIO_NO_ERROR) {
        m_lastError = "RtAudio failed to open stream";
        m_userCallback.store(nullptr, std::memory_order_relaxed);
        m_userData.store(nullptr, std::memory_order_relaxed);
        return false;
    }

    m_bufferSize.store(bufferFrames, std::memory_order_relaxed);
    return true;
}

void RtAudioDriver::closeStream() {
    if (!m_rtAudio) {
        return;
    }

    if (m_rtAudio->isStreamRunning()) {
        stopStream();
    }
    if (m_rtAudio->isStreamOpen()) {
        m_rtAudio->closeStream();
    }

    m_userCallback.store(nullptr, std::memory_order_relaxed);
    m_userData.store(nullptr, std::memory_order_relaxed);
}

bool RtAudioDriver::startStream() {
    if (!m_rtAudio || !m_rtAudio->isStreamOpen()) {
        m_lastError = "RtAudio stream is not open";
        return false;
    }
    if (m_rtAudio->isStreamRunning()) {
        return true;
    }

    RtAudioErrorType error = m_rtAudio->startStream();
    if (error != RTAUDIO_NO_ERROR) {
        m_lastError = "RtAudio failed to start stream";
        return false;
    }
    return true;
}

void RtAudioDriver::stopStream() {
    if (m_rtAudio && m_rtAudio->isStreamRunning()) {
        m_rtAudio->stopStream();
    }
}

bool RtAudioDriver::isStreamRunning() const {
    return m_rtAudio && m_rtAudio->isStreamRunning();
}

double RtAudioDriver::getStreamLatency() const {
    return (m_rtAudio && m_rtAudio->isStreamOpen()) ? m_rtAudio->getStreamLatency() : 0.0;
}

uint32_t RtAudioDriver::getStreamSampleRate() const {
    return (m_rtAudio && m_rtAudio->isStreamOpen()) ? m_rtAudio->getStreamSampleRate() : 0;
}

uint32_t RtAudioDriver::getStreamBufferSize() const {
    return (m_rtAudio && m_rtAudio->isStreamOpen()) ? m_bufferSize.load(std::memory_order_relaxed) : 0;
}

int RtAudioDriver::rtAudioCallback(void* outputBuffer, void* inputBuffer, unsigned int numFrames, double streamTime,
                                   RtAudioStreamStatus status, void* userData) {
    auto* driver = static_cast<RtAudioDriver*>(userData);
    if (status != 0) {
        driver->m_stats.underrunCount++;
    }

    AudioCallback callback = driver->m_userCallback.load(std::memory_order_relaxed);
    void* callbackUserData = driver->m_userData.load(std::memory_order_relaxed);
    if (!callback) {
        return 0;
    }

    driver->m_stats.callbackCount++;
    return callback(static_cast<float*>(outputBuffer), static_cast<const float*>(inputBuffer), numFrames, streamTime,
                    callbackUserData);
}

bool RtAudioDriver::tryInitializeBackend(const std::vector<RtAudio::Api>& candidates) {
    for (RtAudio::Api api : candidates) {
        try {
            std::cout << "[RtAudio] Trying API: " << api << "\n";
            auto candidate = (api == RtAudio::UNSPECIFIED) ? std::make_unique<RtAudio>() : std::make_unique<RtAudio>(api);
            candidate->setErrorCallback([](RtAudioErrorType type, const std::string& errorText) {
                if (type != RTAUDIO_NO_ERROR && type != RTAUDIO_WARNING) {
                    std::cerr << "RtAudio Linux error: " << errorText << std::endl;
                }
            });

            auto deviceIds = candidate->getDeviceIds();
            std::cout << "[RtAudio] API " << api << " has " << deviceIds.size() << " devices\n";
            
            if (!deviceIds.empty()) {
                m_driverType = apiToDriverType(candidate->getCurrentApi());
                m_rtAudio = std::move(candidate);
                return true;
            }
        } catch (const std::exception& e) {
            std::cerr << "[RtAudio] Exception for API " << api << ": " << e.what() << "\n";
        }
    }

    return false;
}

AudioDriverType RtAudioDriver::apiToDriverType(RtAudio::Api api) {
    switch (api) {
    case RtAudio::LINUX_ALSA:
        return AudioDriverType::ALSA;
    case RtAudio::LINUX_PULSE:
        return AudioDriverType::PULSEAUDIO;
    case RtAudio::UNIX_JACK:
        return AudioDriverType::JACK;
    default:
        return AudioDriverType::RTAUDIO;
    }
}

const char* RtAudioDriver::apiName(RtAudio::Api api) {
    switch (api) {
    case RtAudio::LINUX_ALSA:
        return "ALSA";
    case RtAudio::LINUX_PULSE:
        return "PulseAudio";
    case RtAudio::UNIX_JACK:
        return "JACK";
    default:
        return "Auto";
    }
}

} // namespace Audio
} // namespace Aestra
