// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RtAudioDriver.h"
#include "Core/AudioTelemetry.h"

#include <iostream>
#include <utility>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#endif

namespace Aestra {
namespace Audio {

RtAudioDriver::RtAudioDriver() {
    std::vector<RtAudio::Api> candidates;
#ifdef __LINUX_PULSE__
    candidates.push_back(RtAudio::LINUX_PULSE);
#endif
#ifdef __LINUX_ALSA__
    candidates.push_back(RtAudio::LINUX_ALSA);
#endif
#ifdef __UNIX_JACK__
    candidates.push_back(RtAudio::UNIX_JACK);
#endif
    candidates.push_back(RtAudio::UNSPECIFIED);

    if (!tryInitializeBackend(candidates)) {
        m_lastError = "No Linux RtAudio backend could be initialized";
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
    m_telemetry = config.telemetry;
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

#ifdef __linux__
    // Real-time scheduling is set in the audio callback via pthread_once().
    // We can't set it here because RtAudio creates its own callback thread,
    // and pthread_self() at this point is the UI thread, not the audio thread.
    // See rtAudioCallback() for the actual scheduling setup.
#endif

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

// =============================================================================
// Linux Real-Time Scheduling Setup
// =============================================================================
// Called once from the audio callback thread (not the UI thread) to set
// SCHED_FIFO priority. Uses pthread_once to ensure it runs only once per
// process, regardless of how many streams are opened.
// =============================================================================

#ifdef __linux__
static void setupRealtimeScheduling() {
    // Set SCHED_FIFO with midpoint priority
    struct sched_param param;
    int maxPri = sched_get_priority_max(SCHED_FIFO);
    int minPri = sched_get_priority_min(SCHED_FIFO);
    if (maxPri < 0 || minPri < 0) return; // Not supported
    param.sched_priority = (maxPri + minPri) / 2;

    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        // Failed — likely no CAP_SYS_NICE or RLIMIT_RTPRIO=0.
        // This is expected in CI/container environments. Fall back gracefully.
        return;
    }

    // Lock all current and future memory to prevent page faults
    mlockall(MCL_CURRENT | MCL_FUTURE);
}

static pthread_once_t s_rtOnce = PTHREAD_ONCE_INIT;
#endif

// =============================================================================
// Audio Callback
// =============================================================================

int RtAudioDriver::rtAudioCallback(void* outputBuffer, void* inputBuffer, unsigned int numFrames, double streamTime,
                                   RtAudioStreamStatus status, void* userData) {
    auto* driver = static_cast<RtAudioDriver*>(userData);
    if (status != 0) {
        driver->m_stats.underrunCount++;
        if (driver->m_telemetry) {
            driver->m_telemetry->incrementXruns();
            driver->m_telemetry->incrementUnderruns();
        }
    }

#ifdef __linux__
    // Set real-time scheduling on the first callback invocation.
    // This runs on the actual RtAudio callback thread, not the UI thread.
    pthread_once(&s_rtOnce, &setupRealtimeScheduling);
#endif

    AudioCallback callback = driver->m_userCallback.load(std::memory_order_relaxed);
    void* callbackUserData = driver->m_userData.load(std::memory_order_relaxed);
    if (!callback) {
        return 0;
    }

    driver->m_stats.callbackCount++;
    int result = callback(static_cast<float*>(outputBuffer), static_cast<const float*>(inputBuffer), numFrames, streamTime,
                    callbackUserData);

    // Update telemetry (lock-free, RT-safe)
    if (driver->m_telemetry) {
        driver->m_telemetry->updateLastBufferFrames(numFrames);
        uint32_t sr = driver->getStreamSampleRate();
        if (sr > 0) {
            driver->m_telemetry->updateLastSampleRate(sr);
        }
    }

    return result;
}

bool RtAudioDriver::tryInitializeBackend(const std::vector<RtAudio::Api>& candidates) {
    for (RtAudio::Api api : candidates) {
        try {
            auto candidate = (api == RtAudio::UNSPECIFIED) ? std::make_unique<RtAudio>() : std::make_unique<RtAudio>(api);
            candidate->setErrorCallback([](RtAudioErrorType type, const std::string& errorText) {
                if (type != RTAUDIO_NO_ERROR && type != RTAUDIO_WARNING) {
                    std::cerr << "RtAudio Linux error: " << errorText << std::endl;
                }
            });

            auto deviceIds = candidate->getDeviceIds();
            if (!deviceIds.empty()) {
                m_driverType = apiToDriverType(candidate->getCurrentApi());
                m_rtAudio = std::move(candidate);
                return true;
            }
        } catch (const std::exception&) {
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
