// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MixerChannel.h"
#include "NomadLog.h"
#include <algorithm>
#include <cmath>

namespace Nomad {
namespace Audio {

MixerChannel::MixerChannel(const std::string& name, uint32_t channelId)
    : m_name(name)
    , m_uuid(NomadUUID::generate())
    , m_channelId(channelId)
    , m_color(0xFF4080FF)

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
    if (m_mixerBus) m_mixerBus->setGain(volume);
}

void MixerChannel::setPan(float pan) {
    m_pan.store(pan);
    if (m_mixerBus) m_mixerBus->setPan(pan);
}

void MixerChannel::setMute(bool mute) {
    m_muted.store(mute);
    if (m_mixerBus) m_mixerBus->setMute(mute);
}

void MixerChannel::setSolo(bool solo) {
    m_soloed.store(solo);
    if (m_mixerBus) m_mixerBus->setSolo(solo);
}

void MixerChannel::setSoloSafe(bool safe) {
    m_soloSafe.store(safe);
    // Solo safe doesn't affect internal bus logic directly, 
    // it's used by the AudioEngine to decide suppression.
}

void MixerChannel::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate) {
    if (!outputBuffer || numFrames == 0) return;
    if (m_muted.load()) return;
    
    // In v3.0, MixerChannel processes its internal bus/effects chain.
    // The TrackManager orchestration handles mixing clip data into appropriate channel buffers.
    if (m_mixerBus) {
        m_mixerBus->process(outputBuffer, numFrames);
    }
}



} // namespace Audio
} // namespace Nomad
