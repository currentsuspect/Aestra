// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MixerChannel.h"

#include "RealtimeThreadGuard.h"
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
    if (reportRealtimeMisuse("MixerChannel::setName")) return;

    m_name = name;
}

void MixerChannel::setColor(uint32_t color) {
    if (reportRealtimeMisuse("MixerChannel::setColor")) return;

    m_color = color;
}

void MixerChannel::setVolume(float volume) {
    if (reportRealtimeMisuse("MixerChannel::setVolume")) return;

    const float previous = m_volume.exchange(volume);
    if (m_mixerBus)
        m_mixerBus->setGain(volume);
    if (m_commandSink && m_channelId > 0 && std::abs(previous - volume) > 0.0001f) {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTrackVolume;
        cmd.channelId = m_channelId;
        cmd.value1 = volume;
        if (!m_commandSink(cmd)) {
            markStateDirty();
        }
    }
}

void MixerChannel::setPan(float pan) {
    if (reportRealtimeMisuse("MixerChannel::setPan")) return;

    const float previous = m_pan.exchange(pan);
    if (m_mixerBus)
        m_mixerBus->setPan(pan);
    if (m_commandSink && m_channelId > 0 && std::abs(previous - pan) > 0.0001f) {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTrackPan;
        cmd.channelId = m_channelId;
        cmd.value1 = pan;
        if (!m_commandSink(cmd)) {
            markStateDirty();
        }
    }
}

void MixerChannel::setWidth(float width) {
    if (reportRealtimeMisuse("MixerChannel::setWidth")) return;

    m_width.store(width);
    if (m_mixerBus)
        m_mixerBus->setWidth(width);
}

void MixerChannel::setMute(bool mute) {
    if (reportRealtimeMisuse("MixerChannel::setMute")) return;

    const bool previous = m_muted.exchange(mute);
    if (m_mixerBus)
        m_mixerBus->setMute(mute);
    if (m_commandSink && m_channelId > 0 && previous != mute) {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTrackMute;
        cmd.channelId = m_channelId;
        cmd.value1 = mute ? 1.0f : 0.0f;
        if (!m_commandSink(cmd)) {
            markStateDirty();
        }
    }
}

void MixerChannel::setSolo(bool solo) {
    if (reportRealtimeMisuse("MixerChannel::setSolo")) return;

    const bool previous = m_soloed.exchange(solo);
    if (m_mixerBus)
        m_mixerBus->setSolo(solo);
    if (m_commandSink && m_channelId > 0 && previous != solo) {
        AudioQueueCommand cmd{};
        cmd.type = AudioQueueCommandType::SetTrackSolo;
        cmd.channelId = m_channelId;
        cmd.value1 = solo ? 1.0f : 0.0f;
        if (!m_commandSink(cmd)) {
            markStateDirty();
        }
    }
}

bool MixerChannel::resyncEngineState() {
    if (reportRealtimeMisuse("MixerChannel::resyncEngineState")) return false;
    if (!isStateDirty()) {
        return true;
    }
    if (!m_commandSink || m_channelId == 0) {
        return false;
    }
    const float volume = m_volume.load(std::memory_order_relaxed);
    const float pan = m_pan.load(std::memory_order_relaxed);
    const bool mute = m_muted.load(std::memory_order_relaxed);
    const bool solo = m_soloed.load(std::memory_order_relaxed);
    AudioQueueCommand cmd{};
    cmd.channelId = m_channelId;
    cmd.type = AudioQueueCommandType::SetTrackVolume;
    cmd.value1 = volume;
    if (!m_commandSink(cmd)) {
        return false;
    }
    cmd.type = AudioQueueCommandType::SetTrackPan;
    cmd.value1 = pan;
    if (!m_commandSink(cmd)) {
        return false;
    }
    cmd.type = AudioQueueCommandType::SetTrackMute;
    cmd.value1 = mute ? 1.0f : 0.0f;
    if (!m_commandSink(cmd)) {
        return false;
    }
    cmd.type = AudioQueueCommandType::SetTrackSolo;
    cmd.value1 = solo ? 1.0f : 0.0f;
    if (!m_commandSink(cmd)) {
        return false;
    }
    m_stateDirty.store(false, std::memory_order_relaxed);
    return true;
}

void MixerChannel::setSoloSafe(bool safe) {
    if (reportRealtimeMisuse("MixerChannel::setSoloSafe")) return;

    m_soloSafe.store(safe);
    // Solo safe doesn't affect internal bus logic directly,
    // it's used by the AudioEngine to decide suppression.
}

std::vector<AudioRoute> MixerChannel::getSends() const {
    // B-005: getSends() must never be called from the audio callback (RT) thread.
    // Calling from RT can cause lock contention or deadlock. Sends are only
    // accessed during graph build (main thread) or UI serialization.
    std::lock_guard<std::mutex> lock(m_sendMutex);
    return m_sends;
}

namespace {
AudioRoute sanitizeRoute(const AudioRoute& route) {
    AudioRoute sanitized = route;
    sanitized.gain = std::isfinite(route.gain) ? std::clamp(route.gain, 0.0f, 4.0f) : 0.0f;
    sanitized.pan = std::isfinite(route.pan) ? std::clamp(route.pan, -1.0f, 1.0f) : 0.0f;
    return sanitized;
}
} // namespace

void MixerChannel::addSend(const AudioRoute& route) {
    if (reportRealtimeMisuse("MixerChannel::addSend")) return;
    std::lock_guard<std::mutex> lock(m_sendMutex);
    AudioRoute sanitized = sanitizeRoute(route);
    if (sanitized.sendId == 0) {
        sanitized.sendId = m_nextSendId++;
    }
    m_sends.push_back(sanitized);
}

void MixerChannel::insertSend(int index, const AudioRoute& route) {
    if (reportRealtimeMisuse("MixerChannel::insertSend")) return;
    std::lock_guard<std::mutex> lock(m_sendMutex);
    AudioRoute sanitized = sanitizeRoute(route);
    if (sanitized.sendId == 0) {
        sanitized.sendId = m_nextSendId++;
    }
    const size_t clampedIndex = std::min(static_cast<size_t>(std::max(index, 0)), m_sends.size());
    m_sends.insert(m_sends.begin() + static_cast<ptrdiff_t>(clampedIndex), sanitized);
}

void MixerChannel::setSend(uint64_t sendId, const AudioRoute& route) {
    if (reportRealtimeMisuse("MixerChannel::setSend")) return;

    std::lock_guard<std::mutex> lock(m_sendMutex);
    const int index = findSendIndexLocked(sendId);
    if (index < 0) {
        return;
    }
    AudioRoute sanitized = sanitizeRoute(route);
    // Identity is owned by the channel: the replacement keeps the sendId.
    sanitized.sendId = m_sends[static_cast<size_t>(index)].sendId;
    m_sends[static_cast<size_t>(index)] = sanitized;
}

void MixerChannel::replaceSends(const std::vector<AudioRoute>& routes) {
    if (reportRealtimeMisuse("MixerChannel::replaceSends")) return;

    std::lock_guard<std::mutex> lock(m_sendMutex);
    m_sends.clear();
    m_sends.reserve(routes.size());
    for (const auto& route : routes) {
        AudioRoute sanitized = sanitizeRoute(route);
        if (sanitized.sendId == 0) {
            sanitized.sendId = m_nextSendId++;
        }
        m_sends.push_back(sanitized);
    }
}

void MixerChannel::removeSend(uint64_t sendId) {
    if (reportRealtimeMisuse("MixerChannel::removeSend")) return;

    std::lock_guard<std::mutex> lock(m_sendMutex);
    const int index = findSendIndexLocked(sendId);
    if (index >= 0) {
        m_sends.erase(m_sends.begin() + index);
    }
}

int MixerChannel::findSendIndex(uint64_t sendId) const {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    return findSendIndexLocked(sendId);
}

int MixerChannel::findSendIndexLocked(uint64_t sendId) const {
    for (size_t i = 0; i < m_sends.size(); ++i) {
        if (m_sends[i].sendId == sendId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace Audio
} // namespace Aestra
