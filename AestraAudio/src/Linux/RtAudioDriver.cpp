// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RtAudioDriver.h"

#include "Core/AudioTelemetry.h"

#include <cerrno>
#include <chrono>
#include <iostream>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#endif

namespace Aestra {
namespace Audio {

#ifdef __linux__
namespace {
uintptr_t pthreadToToken(pthread_t threadId) {
    if constexpr (std::is_pointer_v<pthread_t>) {
        return reinterpret_cast<uintptr_t>(threadId);
    } else {
        return static_cast<uintptr_t>(threadId);
    }
}

pthread_t tokenToPthread(uintptr_t threadToken) {
    if constexpr (std::is_pointer_v<pthread_t>) {
        return reinterpret_cast<pthread_t>(threadToken);
    } else {
        return static_cast<pthread_t>(threadToken);
    }
}
} // namespace
#endif

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
    options.flags = RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_SCHEDULE_REALTIME;
    options.numberOfBuffers = 2;
    options.priority = sched_get_priority_max(SCHED_FIFO);

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

    const uint32_t actualSampleRate =
        (m_rtAudio && m_rtAudio->isStreamOpen()) ? static_cast<uint32_t>(m_rtAudio->getStreamSampleRate()) : sampleRate;
    m_sampleRate.store(actualSampleRate, std::memory_order_relaxed);
    m_bufferSize.store(bufferFrames, std::memory_order_relaxed);
    return true;
}

void RtAudioDriver::closeStream() {
    if (!m_rtAudio) {
        return;
    }

#ifdef __linux__
    stopRealtimePriorityWorker();
#endif
    if (m_rtAudio->isStreamRunning()) {
        stopStream();
    }
    if (m_rtAudio->isStreamOpen()) {
        m_rtAudio->closeStream();
    }

    m_userCallback.store(nullptr, std::memory_order_relaxed);
    m_userData.store(nullptr, std::memory_order_relaxed);
    m_sampleRate.store(0, std::memory_order_relaxed);
    m_bufferSize.store(0, std::memory_order_relaxed);
#ifdef __linux__
    m_audioThreadToken.store(0, std::memory_order_relaxed);
#endif
}

bool RtAudioDriver::startStream() {
    if (!m_rtAudio || !m_rtAudio->isStreamOpen()) {
        m_lastError = "RtAudio stream is not open";
        return false;
    }
    if (m_rtAudio->isStreamRunning()) {
        return true;
    }

#ifdef __linux__
    stopRealtimePriorityWorker();
    m_callbackRtPriorityAttempted.store(false, std::memory_order_release);
    m_audioThreadToken.store(0, std::memory_order_release);
    if (m_telemetry) {
        m_telemetry->clearThreadPriorityStatus();
    }
    // mlockall is process-wide and can be requested from the starter thread.
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
        if (m_telemetry) {
            m_telemetry->setThreadPriorityBit(0x02); // mlockall success
        }
    } else if (m_telemetry) {
        m_telemetry->updateLinuxRtPriorityErrno(errno);
    }
#endif

    RtAudioErrorType error = m_rtAudio->startStream();
    if (error != RTAUDIO_NO_ERROR) {
        m_lastError = "RtAudio failed to start stream";
        return false;
    }

#ifdef __linux__
    startRealtimePriorityWorker();
#endif

    return true;
}

void RtAudioDriver::stopStream() {
#ifdef __linux__
    stopRealtimePriorityWorker();
#endif
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
    return (m_rtAudio && m_rtAudio->isStreamOpen()) ? m_sampleRate.load(std::memory_order_relaxed) : 0;
}

uint32_t RtAudioDriver::getStreamBufferSize() const {
    return (m_rtAudio && m_rtAudio->isStreamOpen()) ? m_bufferSize.load(std::memory_order_relaxed) : 0;
}

int RtAudioDriver::rtAudioCallback(void* outputBuffer, void* inputBuffer, unsigned int numFrames, double streamTime,
                                   RtAudioStreamStatus status, void* userData) {
    auto* driver = static_cast<RtAudioDriver*>(userData);
#ifdef __linux__
    bool expected = false;
    if (driver &&
        driver->m_callbackRtPriorityAttempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        driver->m_audioThreadToken.store(pthreadToToken(pthread_self()), std::memory_order_release);
    }
#endif

    if (status != 0) {
        driver->m_stats.underrunCount++;
        if (driver->m_telemetry) {
            driver->m_telemetry->incrementXruns();
            driver->m_telemetry->incrementUnderruns();
        }
    }

    AudioCallback callback = driver->m_userCallback.load(std::memory_order_relaxed);
    void* callbackUserData = driver->m_userData.load(std::memory_order_relaxed);
    if (!callback) {
        return 0;
    }

    driver->m_stats.callbackCount++;
    int result = callback(static_cast<float*>(outputBuffer), static_cast<const float*>(inputBuffer), numFrames,
                          streamTime, callbackUserData);

    // Update telemetry (lock-free, RT-safe)
    if (driver->m_telemetry) {
        driver->m_telemetry->updateLastBufferFrames(numFrames);
        uint32_t sr = driver->m_sampleRate.load(std::memory_order_relaxed);
        if (sr > 0) {
            driver->m_telemetry->updateLastSampleRate(sr);
        }
    }

    return result;
}

#ifdef __linux__
void RtAudioDriver::startRealtimePriorityWorker() {
    stopRealtimePriorityWorker();
    m_rtPriorityWorkerStop.store(false, std::memory_order_release);
    m_rtPriorityWorker = std::thread([this]() {
        for (int attempt = 0; attempt < 100 && !m_rtPriorityWorkerStop.load(std::memory_order_acquire); ++attempt) {
            const uintptr_t audioThreadToken = m_audioThreadToken.load(std::memory_order_acquire);
            if (audioThreadToken != 0) {
                applyLinuxRealtimePriority(tokenToPthread(audioThreadToken));
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
}

void RtAudioDriver::stopRealtimePriorityWorker() {
    m_rtPriorityWorkerStop.store(true, std::memory_order_release);
    if (m_rtPriorityWorker.joinable()) {
        m_rtPriorityWorker.join();
    }
}

void RtAudioDriver::applyLinuxRealtimePriority(pthread_t audioThreadId) {
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    param.sched_priority = (param.sched_priority + sched_get_priority_min(SCHED_FIFO)) / 2;
    const int rtResult = pthread_setschedparam(audioThreadId, SCHED_FIFO, &param);
    if (m_telemetry) {
        if (rtResult == 0) {
            m_telemetry->setThreadPriorityBit(0x01);
            m_telemetry->updateLinuxRtPriorityErrno(0);
        } else {
            m_telemetry->updateLinuxRtPriorityErrno(rtResult);
        }
    }
}
#endif

bool RtAudioDriver::tryInitializeBackend(const std::vector<RtAudio::Api>& candidates) {
    for (RtAudio::Api api : candidates) {
        try {
            auto candidate =
                (api == RtAudio::UNSPECIFIED) ? std::make_unique<RtAudio>() : std::make_unique<RtAudio>(api);
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
        } catch (const std::exception&) {}
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
