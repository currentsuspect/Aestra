// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RtAudioBackend.h"
#include <stdexcept>
#include <string>

namespace Nomad {
namespace Audio {

RtAudioBackend::RtAudioBackend()
    : m_userCallback(nullptr)
    , m_userData(nullptr)
{
    std::cout << "RtAudioBackend: Initializing WASAPI audio backend" << std::endl;
    std::cout.flush();
    
    try {
        m_rtAudio = std::make_unique<RtAudio>(RtAudio::WINDOWS_WASAPI);
        std::cout << "RtAudioBackend: WASAPI initialized successfully" << std::endl;
        
        // Set error callback
        m_rtAudio->setErrorCallback([](RtAudioErrorType type, const std::string& errorText) {
            if (type != RTAUDIO_NO_ERROR && type != RTAUDIO_WARNING) {
                std::cerr << "RtAudio WASAPI Error: " << errorText << std::endl;
            }
        });
    } catch (const std::exception& e) {
        std::cerr << "RtAudioBackend: WASAPI initialization failed: " << e.what() << std::endl;
        throw;
    }
}

RtAudioBackend::~RtAudioBackend() {
    closeStream();
}

std::vector<AudioDeviceInfo> RtAudioBackend::getDevices() {
    std::vector<AudioDeviceInfo> devices;
    
    try {
        std::vector<unsigned int> deviceIds = m_rtAudio->getDeviceIds();
        std::cout << "RtAudioBackend::getDevices: Found " << deviceIds.size() << " device IDs" << std::endl;
        
        for (unsigned int id : deviceIds) {
            try {
                RtAudio::DeviceInfo rtInfo = m_rtAudio->getDeviceInfo(id);
                
                // Skip invalid devices
                if (rtInfo.outputChannels == 0 && rtInfo.inputChannels == 0) {
                    std::cout << "  Device " << id << ": Skipping (no I/O channels)" << std::endl;
                    continue;
                }
                
                AudioDeviceInfo info;
                info.id = id;
                info.name = rtInfo.name;
                info.maxInputChannels = rtInfo.inputChannels;
                info.maxOutputChannels = rtInfo.outputChannels;
                info.supportedSampleRates = rtInfo.sampleRates;
                info.preferredSampleRate = rtInfo.preferredSampleRate;
                info.isDefaultInput = rtInfo.isDefaultInput;
                info.isDefaultOutput = rtInfo.isDefaultOutput;
                
                std::cout << "  Device " << id << ": " << rtInfo.name 
                          << " (out:" << rtInfo.outputChannels << " in:" << rtInfo.inputChannels << ")" << std::endl;
                
                devices.push_back(info);
            } catch (const std::exception& e) {
                std::cerr << "  Device " << id << ": Exception getting info: " << e.what() << std::endl;
                continue;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "RtAudioBackend::getDevices: Exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "RtAudioBackend::getDevices: Unknown exception" << std::endl;
    }
    
    std::cout << "RtAudioBackend::getDevices: Returning " << devices.size() << " valid devices" << std::endl;
    return devices;
}



bool RtAudioBackend::openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) {
    if (m_rtAudio->isStreamOpen()) {
        closeStream();
    }

    m_userCallback.store(callback, std::memory_order_relaxed);
    m_userData.store(userData, std::memory_order_relaxed);

    RtAudio::StreamParameters outputParams;
    outputParams.deviceId = config.deviceId;
    outputParams.nChannels = config.numOutputChannels;
    outputParams.firstChannel = 0;

    RtAudio::StreamParameters* inputParams = nullptr;
    RtAudio::StreamParameters inputParamsData;
    if (config.numInputChannels > 0) {
        inputParamsData.deviceId = config.deviceId;
        inputParamsData.nChannels = config.numInputChannels;
        inputParamsData.firstChannel = 0;
        inputParams = &inputParamsData;
    }

    unsigned int bufferFrames = config.bufferSize;
    unsigned int sampleRate = config.sampleRate;

    std::cout << "RtAudioBackend::openStream: About to call m_rtAudio->openStream" << std::endl;
    std::cout << "  outputParams.deviceId: " << outputParams.deviceId << std::endl;
    std::cout << "  outputParams.nChannels: " << outputParams.nChannels << std::endl;
    std::cout << "  sampleRate: " << sampleRate << std::endl;
    std::cout << "  bufferFrames: " << bufferFrames << std::endl;
    std::cout.flush();

    RtAudioErrorType error = m_rtAudio->openStream(
        &outputParams,
        inputParams,
        RTAUDIO_FLOAT32,
        sampleRate,
        &bufferFrames,
        &RtAudioBackend::rtAudioCallback,
        this
    );

    std::cout << "RtAudioBackend::openStream: m_rtAudio->openStream returned error code: " << error << std::endl;
    std::cout << "  Final bufferFrames: " << bufferFrames << std::endl;
    std::cout << "  Stream open: " << (m_rtAudio->isStreamOpen() ? "true" : "false") << std::endl;
    std::cout.flush();

    if (error == RTAUDIO_NO_ERROR) {
        m_bufferSize.store(bufferFrames, std::memory_order_relaxed);  // Store actual buffer size returned by RtAudio
    }

    return (error == RTAUDIO_NO_ERROR);
}

void RtAudioBackend::closeStream() {
    if (m_rtAudio->isStreamOpen()) {
        if (m_rtAudio->isStreamRunning()) {
            stopStream();
        }
        m_rtAudio->closeStream();
    }
}

bool RtAudioBackend::startStream() {
    if (!m_rtAudio->isStreamOpen()) {
        std::cout << "RtAudioBackend::startStream: Stream is not open" << std::endl;
        return false;
    }

    // Check if stream is already running
    if (m_rtAudio->isStreamRunning()) {
        std::cout << "RtAudioBackend::startStream: Stream is already running" << std::endl;
        return true;  // Already running is a success
    }

    std::cout << "RtAudioBackend::startStream: Starting stream" << std::endl;
    std::cout.flush();

    RtAudioErrorType error = m_rtAudio->startStream();

    std::cout << "RtAudioBackend::startStream: m_rtAudio->startStream returned error code: " << error << std::endl;
    std::cout << "  Stream running: " << (m_rtAudio->isStreamRunning() ? "true" : "false") << std::endl;
    std::cout.flush();

    return (error == RTAUDIO_NO_ERROR);
}

void RtAudioBackend::stopStream() {
    if (m_rtAudio->isStreamRunning()) {
        m_rtAudio->stopStream();
    }
}

bool RtAudioBackend::isStreamRunning() const {
    return m_rtAudio->isStreamRunning();
}

double RtAudioBackend::getStreamLatency() const {
    if (!m_rtAudio->isStreamOpen()) {
        return 0.0;
    }
    return m_rtAudio->getStreamLatency();
}

uint32_t RtAudioBackend::getStreamSampleRate() const {
    if (!m_rtAudio || !m_rtAudio->isStreamOpen()) {
        return 0;
    }
    return m_rtAudio->getStreamSampleRate();
}

uint32_t RtAudioBackend::getStreamBufferSize() const {
    if (!m_rtAudio || !m_rtAudio->isStreamOpen()) {
        return 0;
    }
    return m_bufferSize.load(std::memory_order_relaxed);  // Return stored buffer size
}

int RtAudioBackend::rtAudioCallback(
    void* outputBuffer,
    void* inputBuffer,
    unsigned int numFrames,
    double streamTime,
    RtAudioStreamStatus status,
    void* userData
) {
    (void)status; // Unused parameter
    
    RtAudioBackend* backend = static_cast<RtAudioBackend*>(userData);
    
    AudioCallback userCallback = backend->m_userCallback.load(std::memory_order_relaxed);
    void* callbackUserData = backend->m_userData.load(std::memory_order_relaxed);
    
    if (userCallback) {
        return userCallback(
            static_cast<float*>(outputBuffer),
            static_cast<const float*>(inputBuffer),
            numFrames,
            streamTime,
            callbackUserData
        );
    }
    
    return 0;
}

} // namespace Audio
} // namespace Nomad
