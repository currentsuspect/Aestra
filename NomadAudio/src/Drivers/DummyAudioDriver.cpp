// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "DummyAudioDriver.h"
#include <chrono>

namespace Nomad {
namespace Audio {

DummyAudioDriver::DummyAudioDriver() = default;

DummyAudioDriver::~DummyAudioDriver() {
    stopStream();
}

std::vector<AudioDeviceInfo> DummyAudioDriver::getDevices() {
    AudioDeviceInfo info;
    info.id = 0;
    info.name = "Dummy Output";
    info.maxInputChannels = 0;
    info.maxOutputChannels = 2;
    info.supportedSampleRates = { 44100, 48000, 88200, 96000 };
    info.isDefaultOutput = true;
    return { info };
}

bool DummyAudioDriver::openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) {
    m_config = config;
    m_callback = callback;
    m_userData = userData;
    m_sampleRate = config.sampleRate;
    m_bufferSize = config.bufferSize;
    m_latency = static_cast<double>(m_bufferSize) / m_sampleRate;
    m_silentBuffer.assign(m_bufferSize * 2, 0.0f);
    return true;
}

void DummyAudioDriver::closeStream() {
    stopStream();
    m_callback = nullptr;
}

bool DummyAudioDriver::startStream() {
    if (m_running) return true;
    m_stopRequested = false;
    m_running = true;
    m_thread = std::thread(&DummyAudioDriver::threadFunc, this);
    return true;
}

void DummyAudioDriver::stopStream() {
    m_stopRequested = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_running = false;
}

void DummyAudioDriver::threadFunc() {
    auto frameDuration = std::chrono::microseconds(static_cast<long long>(1000000.0 * m_bufferSize / m_sampleRate));
    auto nextTick = std::chrono::steady_clock::now();

    while (!m_stopRequested) {
        if (m_callback) {
            m_callback(m_silentBuffer.data(), nullptr, m_bufferSize, 0.0, m_userData);
        }
        
        m_stats.callbackCount++;
        
        nextTick += frameDuration;
        std::this_thread::sleep_until(nextTick);
    }
}

} // namespace Audio
} // namespace Nomad
