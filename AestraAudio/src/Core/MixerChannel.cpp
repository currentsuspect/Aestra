// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MixerChannel.h"

#include "AestraLog.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

MixerChannel::MixerChannel(const std::string& name, uint32_t channelId)
    : m_name(name), m_uuid(AestraUUID::generate()), m_channelId(channelId), m_color(0xFF4080FF)

{
    // m_uuid.low = m_channelId; // REMOVED: Do not overwrite generated UUID with 0!
    m_mixerBus = std::make_unique<MixerBus>(m_name.c_str(), 2);
    Log::info("MixerChannel created: " + m_name + " (ID: " + std::to_string(m_channelId) + ")");
}

MixerChannel::~MixerChannel() {
    Log::info("MixerChannel destroyed: " + m_name);
}

void MixerChannel::setName(const std::string& name) {
    m_name = name;
}

void MixerChannel::setColor(uint32_t color) {
    m_color = color;
}

void MixerChannel::setVolume(float volume) {
    m_volume.store(volume);
    if (m_mixerBus)
        m_mixerBus->setGain(volume);
}

void MixerChannel::setPan(float pan) {
    m_pan.store(pan);
    if (m_mixerBus)
        m_mixerBus->setPan(pan);
}

void MixerChannel::setWidth(float width) {
    m_width.store(width);
    if (m_mixerBus)
        m_mixerBus->setWidth(width);
}

void MixerChannel::setMute(bool mute) {
    m_muted.store(mute);
    if (m_mixerBus)
        m_mixerBus->setMute(mute);
}

void MixerChannel::setSolo(bool solo) {
    m_soloed.store(solo);
    if (m_mixerBus)
        m_mixerBus->setSolo(solo);
}

void MixerChannel::setSoloSafe(bool safe) {
    m_soloSafe.store(safe);
    // Solo safe doesn't affect internal bus logic directly,
    // it's used by the AudioEngine to decide suppression.
}

void MixerChannel::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate) {
    if (!outputBuffer || numFrames == 0)
        return;
    if (m_muted.load())
        return;

    // Prepare effect chain with actual stream sample rate (once)
    static bool chainPrepared = false;
    if (!chainPrepared && m_effectChain.getActiveSlotCount() > 0) {
        Log::info("[MixerChannel] Preparing chain for ch " + std::to_string(m_channelId) + " at " +
                  std::to_string(outputSampleRate) + " Hz");
        m_effectChain.prepare(outputSampleRate, numFrames * 2); // Max block size
        chainPrepared = true;
    }

    // In v3.0, MixerChannel processes its internal bus/effects chain.
    // The TrackManager orchestration handles mixing clip data into appropriate channel buffers.
    if (m_mixerBus) {
        m_mixerBus->process(outputBuffer, numFrames);
    }

    // Process through insert effect chain (if any plugins loaded)
    if (m_effectChain.getActiveSlotCount() > 0) {
        // Audio buffer is interleaved stereo (LRLRLRLR...)
        // Plugins expect planar format (LL...LL, RR...RR)
        // So we need to de-interleave -> process -> re-interleave

        // Allocate temporary planar buffers
        std::vector<float> leftChannel(numFrames);
        std::vector<float> rightChannel(numFrames);

        // De-interleave: split interleaved stereo into separate L/R channels
        for (uint32_t i = 0; i < numFrames; ++i) {
            leftChannel[i] = outputBuffer[i * 2];      // L samples at even indices
            rightChannel[i] = outputBuffer[i * 2 + 1]; // R samples at odd indices
        }

        // Process through effect chain in planar format
        float* channels[2] = {leftChannel.data(), rightChannel.data()};

        // Debug: Print first sample before processing
        // if (numFrames > 0 && m_channelId == 1) Log::info("Pre-FX val: " + std::to_string(channels[0][0]));

        m_effectChain.process(channels, 2, numFrames);

        // Debug: Print first sample after processing
        // if (numFrames > 0 && m_channelId == 1) Log::info("Post-FX val: " + std::to_string(channels[0][0]));

        // Re-interleave: merge processed L/R channels back to interleaved format
        for (uint32_t i = 0; i < numFrames; ++i) {
            outputBuffer[i * 2] = leftChannel[i];      // L to even indices
            outputBuffer[i * 2 + 1] = rightChannel[i]; // R to odd indices
        }
    }
}

std::vector<AudioRoute> MixerChannel::getSends() const {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    return m_sends;
}

void MixerChannel::addSend(const AudioRoute& route) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    m_sends.push_back(route);
}

void MixerChannel::removeSend(int index) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (index >= 0 && index < static_cast<int>(m_sends.size())) {
        m_sends.erase(m_sends.begin() + index);
    }
}

void MixerChannel::setSendLevel(int index, float level) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (index >= 0 && index < static_cast<int>(m_sends.size())) {
        m_sends[index].gain = level;
    }
}

void MixerChannel::setSendPan(int index, float pan) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (index >= 0 && index < static_cast<int>(m_sends.size())) {
        m_sends[index].pan = pan;
    }
}

void MixerChannel::setSendDestination(int index, uint32_t destId) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (index >= 0 && index < static_cast<int>(m_sends.size())) {
        m_sends[index].targetChannelId = destId;
    }
}

} // namespace Audio
} // namespace Aestra
