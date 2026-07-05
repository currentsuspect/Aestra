// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MixerChannel.h"

#include "AestraLog.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

MixerChannel::MixerChannel(const std::string& name, uint32_t channelId)
    // 0 = color unset: the UI derives a palette color per track instead. A
    // concrete default here would win nearestPaletteIndex() for every channel
    // and pin all tracks to the same palette entry.
    : m_name(name), m_uuid(AestraUUID::generate()), m_channelId(channelId), m_color(0)

{
    // m_uuid.low = m_channelId; // REMOVED: Do not overwrite generated UUID with 0!
    m_mixerBus = std::make_unique<MixerBus>(m_name.c_str(), 2);
    AESTRA_LOG_TRACE("MixerChannel created: " + m_name + " (ID: " + std::to_string(m_channelId) + ")");
}

MixerChannel::~MixerChannel() {
    AESTRA_LOG_TRACE("MixerChannel destroyed: " + m_name);
}

void MixerChannel::setName(const std::string& name) {
    m_name = name;
}

void MixerChannel::setColor(uint32_t color) {
    m_color = color;
}

void MixerChannel::setVolume(float volume) {
    const float previous = m_volume.exchange(volume);
    if (m_mixerBus)
        m_mixerBus->setGain(volume);
    if (m_commandSink && m_channelId > 0 && std::abs(previous - volume) > 0.0001f) {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTrackVolume;
        cmd.trackIndex = m_channelId - 1;
        cmd.value1 = volume;
        m_commandSink(cmd);
    }
}

void MixerChannel::setPan(float pan) {
    const float previous = m_pan.exchange(pan);
    if (m_mixerBus)
        m_mixerBus->setPan(pan);
    if (m_commandSink && m_channelId > 0 && std::abs(previous - pan) > 0.0001f) {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTrackPan;
        cmd.trackIndex = m_channelId - 1;
        cmd.value1 = pan;
        m_commandSink(cmd);
    }
}

void MixerChannel::setWidth(float width) {
    m_width.store(width);
    if (m_mixerBus)
        m_mixerBus->setWidth(width);
}

void MixerChannel::setMute(bool mute) {
    const bool previous = m_muted.exchange(mute);
    if (m_mixerBus)
        m_mixerBus->setMute(mute);
    if (m_commandSink && m_channelId > 0 && previous != mute) {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTrackMute;
        cmd.trackIndex = m_channelId - 1;
        cmd.value1 = mute ? 1.0f : 0.0f;
        m_commandSink(cmd);
    }
}

void MixerChannel::setSolo(bool solo) {
    const bool previous = m_soloed.exchange(solo);
    if (m_mixerBus)
        m_mixerBus->setSolo(solo);
    if (m_commandSink && m_channelId > 0 && previous != solo) {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTrackSolo;
        cmd.trackIndex = m_channelId - 1;
        cmd.value1 = solo ? 1.0f : 0.0f;
        m_commandSink(cmd);
    }
}

void MixerChannel::setSoloSafe(bool safe) {
    m_soloSafe.store(safe);
    // Solo safe doesn't affect internal bus logic directly,
    // it's used by the AudioEngine to decide suppression.
}

void MixerChannel::prepareProcessingBuffers(uint32_t maxFrames) {
    m_leftChannelBuf.resize(maxFrames);
    m_rightChannelBuf.resize(maxFrames);
    m_dryChannelBuf.resize(static_cast<size_t>(maxFrames) * 2);
}

void MixerChannel::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate) {
    if (!outputBuffer || numFrames == 0)
        return;
    if (m_muted.load())
        return;

    // In v3.0, MixerChannel processes its internal bus/effects chain.
    // The TrackManager orchestration handles mixing clip data into appropriate channel buffers.
    if (m_mixerBus) {
        m_mixerBus->process(outputBuffer, numFrames);
    }

    // Process through insert effect chain (if any plugins loaded)
    // Pass 3: Use snapshot for RT-safety when available
    auto snapshot = m_effectChainSnapshot.load(std::memory_order_acquire);
    if (snapshot && snapshot->getActiveSlotCount() > 0) {
        // Audio buffer is interleaved stereo (LRLRLRLR...)
        // Plugins expect planar format (LL...LL, RR...RR)
        // So we need to de-interleave -> process -> re-interleave

        if (m_leftChannelBuf.size() < numFrames || m_rightChannelBuf.size() < numFrames ||
            m_dryChannelBuf.size() < static_cast<size_t>(numFrames) * 2)
            return;

        for (uint32_t i = 0; i < numFrames; ++i) {
            m_leftChannelBuf[i] = outputBuffer[i * 2];
            m_rightChannelBuf[i] = outputBuffer[i * 2 + 1];
        }

        float* channels[2] = {m_leftChannelBuf.data(), m_rightChannelBuf.data()};

        snapshot->process(channels, 2, numFrames, nullptr, 0, m_dryChannelBuf.data());

        for (uint32_t i = 0; i < numFrames; ++i) {
            outputBuffer[i * 2] = m_leftChannelBuf[i];
            outputBuffer[i * 2 + 1] = m_rightChannelBuf[i];
        }
    } else if (m_effectChain.getActiveSlotCount() > 0) {
        // Fallback: direct processing when no snapshot set (should not happen in normal operation)
        if (m_leftChannelBuf.size() < numFrames || m_rightChannelBuf.size() < numFrames)
            return;

        for (uint32_t i = 0; i < numFrames; ++i) {
            m_leftChannelBuf[i] = outputBuffer[i * 2];
            m_rightChannelBuf[i] = outputBuffer[i * 2 + 1];
        }

        float* channels[2] = {m_leftChannelBuf.data(), m_rightChannelBuf.data()};

        m_effectChain.process(channels, 2, numFrames);

        for (uint32_t i = 0; i < numFrames; ++i) {
            outputBuffer[i * 2] = m_leftChannelBuf[i];
            outputBuffer[i * 2 + 1] = m_rightChannelBuf[i];
        }
    }
}

std::vector<AudioRoute> MixerChannel::getSends() const {
    // B-005: getSends() must never be called from the audio callback (RT) thread.
    // Calling from RT can cause lock contention or deadlock. Sends are only
    // accessed during graph build (main thread) or UI serialization.
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

void MixerChannel::setSendPostFader(int index, bool postFader) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (index >= 0 && index < static_cast<int>(m_sends.size())) {
        m_sends[index].postFader = postFader;
    }
}

void MixerChannel::setSendSidechainOnly(int index, bool sidechainOnly) {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    if (index >= 0 && index < static_cast<int>(m_sends.size())) {
        m_sends[index].sidechainOnly = sidechainOnly;
    }
}

} // namespace Audio
} // namespace Aestra
