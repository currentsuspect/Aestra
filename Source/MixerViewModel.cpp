// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "MixerViewModel.h"
#include "../NomadCore/include/NomadLog.h"

namespace Nomad {

MixerViewModel::MixerViewModel() {
    // Create master channel
    m_master = std::make_unique<ChannelViewModel>();
    m_master->id = 0;
    m_master->slotIndex = Audio::ChannelSlotMap::MASTER_SLOT_INDEX;
    m_master->name = "MASTER";
    m_master->routeName = "Output";
    m_master->trackColor = 0xFF8B7FFF; // Nomad purple
}

void MixerViewModel::updateMeters(const Audio::MeterSnapshotBuffer& snapshots, double deltaTime) {
    // Update channel meters
    for (auto& channel : m_channels) {
        if (!channel) continue;

        if (auto mc = channel->channel.lock()) {
            channel->muted = mc->isMuted();
            channel->soloed = mc->isSoloed();
            // channel->armed = mc->isRecording(); // MixerChannel has no recording state
        }

        const auto snapshot = snapshots.readSnapshot(channel->slotIndex);
        smoothMeterChannel(*channel, snapshot, deltaTime);
    }

    // Update master meter
    if (m_master) {
        const auto snapshot = snapshots.readSnapshot(m_master->slotIndex);
        smoothMeterChannel(*m_master, snapshot, deltaTime);
    }
}

void MixerViewModel::syncFromEngine(const Audio::TrackManager& trackManager,
                                     const Audio::ChannelSlotMap& slotMap) {
    // Build set of current track IDs for quick lookup
    std::unordered_map<uint32_t, size_t> existingIds;
    for (size_t i = 0; i < m_channels.size(); ++i) {
        if (m_channels[i]) {
            existingIds[m_channels[i]->id] = i;
        }
    }

    // Collect channel info from engine
    struct ChannelInfo {
        uint32_t id{0};
        std::string name;
        uint32_t color{0};
        uint32_t slot{0};
        std::weak_ptr<Audio::MixerChannel> channel;
        bool muted{false};
        bool soloed{false};
        bool armed{false};
    };
    std::vector<ChannelInfo> channelInfo;
    auto channels = trackManager.getChannelsSnapshot();
    for (const auto& channel : channels) {
        if (!channel) continue;

        uint32_t id = channel->getChannelId();
        uint32_t slot = slotMap.getSlotIndex(id);
        if (slot != Audio::ChannelSlotMap::INVALID_SLOT) {
            ChannelInfo info;
            info.id = id;
            info.name = channel->getName();
            info.color = channel->getColor();
            info.slot = slot;
            info.channel = channel;
            info.muted = channel->isMuted();
            info.soloed = channel->isSoloed();
            info.armed = false; // Stubbed for v3.0 Mixer separation
            channelInfo.push_back(std::move(info));
        }
    }

    // Rebuild channel list to match tracks
    std::vector<std::unique_ptr<ChannelViewModel>> newChannels;
    newChannels.reserve(channelInfo.size());

    for (const auto& info : channelInfo) {
        const auto it = existingIds.find(info.id);
        if (it != existingIds.end() && m_channels[it->second]) {
            // Reuse existing channel (preserves meter state)
            auto& existing = m_channels[it->second];
            existing->id = info.id;
            existing->name = info.name;
            existing->trackColor = info.color;
            existing->slotIndex = info.slot;
            existing->channel = info.channel;
            existing->muted = info.muted;
            existing->soloed = info.soloed;
            existing->armed = info.armed;
            newChannels.push_back(std::move(existing));
        } else {
            // Create new channel
            auto channel = std::make_unique<ChannelViewModel>();
            channel->id = info.id;
            channel->slotIndex = info.slot;
            channel->channel = info.channel;
            channel->name = info.name;
            channel->trackColor = info.color;
            channel->muted = info.muted;
            channel->soloed = info.soloed;
            channel->armed = info.armed;
            newChannels.push_back(std::move(channel));
        }
        
        // Sync Sends from Engine (Persistence Fix)
        if (auto* ch = newChannels.back().get()) {
            if (auto mc = ch->channel.lock()) {
               auto engineSends = mc->getSends();
               ch->sends.clear();
               for (const auto& route : engineSends) {
                   ChannelViewModel::SendViewModel uiSend;
                   // Handle Legacy Master ID
                   if (route.targetChannelId == 0xFFFFFFFF) {
                       uiSend.targetId = 0;
                       uiSend.targetName = "Master";
                   } else {
                       uiSend.targetId = route.targetChannelId;
                       // Try to resolve name from current snapshot info
                       auto targetIt = std::find_if(channelInfo.begin(), channelInfo.end(), 
                           [&](const ChannelInfo& ci){ return ci.id == route.targetChannelId; });
                       if (targetIt != channelInfo.end()) {
                           uiSend.targetName = targetIt->name;
                       } else if (route.targetChannelId == 0) {
                           uiSend.targetName = "Master";
                       } else {
                           uiSend.targetName = "Unknown (" + std::to_string(route.targetChannelId) + ")";
                       }
                   }
                   uiSend.gain = route.gain;
                   uiSend.pan = route.pan;
                   // uiSend.postFader = ... engine doesn't have this yet, assume true
                   ch->sends.push_back(uiSend);
               }
            }
        }
    }

    m_channels = std::move(newChannels);
    rebuildIdMap();

    // Validate selection
    if (m_selectedChannelId >= 0 && !getChannelById(static_cast<uint32_t>(m_selectedChannelId))) {
        m_selectedChannelId = -1;
    }
}

ChannelViewModel* MixerViewModel::getChannelById(uint32_t id) {
    if (id == 0) return m_master.get();
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end() || it->second >= m_channels.size()) {
        return nullptr;
    }
    return m_channels[it->second].get();
}

const ChannelViewModel* MixerViewModel::getChannelById(uint32_t id) const {
    if (id == 0) return m_master.get();
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end() || it->second >= m_channels.size()) {
        return nullptr;
    }
    return m_channels[it->second].get();
}

ChannelViewModel* MixerViewModel::getSelectedChannel() {
    if (m_selectedChannelId < 0) return nullptr;
    return getChannelById(static_cast<uint32_t>(m_selectedChannelId));
}

const ChannelViewModel* MixerViewModel::getSelectedChannel() const {
    if (m_selectedChannelId < 0) return nullptr;
    return getChannelById(static_cast<uint32_t>(m_selectedChannelId));
}

ChannelViewModel* MixerViewModel::getChannelByIndex(size_t index) {
    if (index >= m_channels.size()) return nullptr;
    return m_channels[index].get();
}

const ChannelViewModel* MixerViewModel::getChannelByIndex(size_t index) const {
    if (index >= m_channels.size()) return nullptr;
    return m_channels[index].get();
}

void MixerViewModel::clearClipLatch(uint32_t id) {
    auto* channel = getChannelById(id);
    if (channel) {
        channel->clipLatchL = false;
        channel->clipLatchR = false;
    }
}

void MixerViewModel::clearMasterClipLatch() {
    if (m_master) {
        m_master->clipLatchL = false;
        m_master->clipLatchR = false;
    }
}

void MixerViewModel::rebuildIdMap() {
    m_idToIndex.clear();
    for (size_t i = 0; i < m_channels.size(); ++i) {
        if (m_channels[i]) {
            m_idToIndex[m_channels[i]->id] = i;
        }
    }
}


void MixerViewModel::smoothMeterChannel(ChannelViewModel& channel,
                                        const Audio::MeterSnapshotBuffer::MeterReadout& snapshot,
                                        double deltaTime) {
    // Convert LINEAR to dB (UI mapping is log-space).
    const float peakDbL = MixerMath::linearToDb(snapshot.peakL);
    const float peakDbR = MixerMath::linearToDb(snapshot.peakR);
    const float energyDbL = MixerMath::linearToDb(snapshot.rmsL);
    const float energyDbR = MixerMath::linearToDb(snapshot.rmsR);
    const float lowDbL = MixerMath::linearToDb(snapshot.lowL);
    const float lowDbR = MixerMath::linearToDb(snapshot.lowR);

    auto smoothDb = [&](float current, float target, float attackMs, float releaseMs) -> float {
        const float ms = static_cast<float>(deltaTime * 1000.0);
        const float tau = (target > current) ? attackMs : releaseMs;
        const float coeff = 1.0f - std::exp(-ms / std::max(1e-3f, tau));
        return current + (target - current) * coeff;
    };

    // Analysis envelopes (never drawn directly).
    channel.envPeakL = smoothDb(channel.envPeakL, peakDbL, PEAK_ATTACK_MS, PEAK_RELEASE_MS);
    channel.envPeakR = smoothDb(channel.envPeakR, peakDbR, PEAK_ATTACK_MS, PEAK_RELEASE_MS);
    channel.envEnergyL = smoothDb(channel.envEnergyL, energyDbL, ENERGY_ATTACK_MS, ENERGY_RELEASE_MS);
    channel.envEnergyR = smoothDb(channel.envEnergyR, energyDbR, ENERGY_ATTACK_MS, ENERGY_RELEASE_MS);
    channel.envLowEnergyL = smoothDb(channel.envLowEnergyL, lowDbL, LOW_ATTACK_MS, LOW_RELEASE_MS);
    channel.envLowEnergyR = smoothDb(channel.envLowEnergyR, lowDbR, LOW_ATTACK_MS, LOW_RELEASE_MS);

    // Perceptual mapping (Musical meters are interpretive, not purely peak).
    auto computeMusicalDb = [&](float peakEnvDb, float energyEnvDb, float lowEnvDb) -> float {
        // Transient strength: how much peak stands above energy (in dB).
        const float transientDb = std::max(0.0f, peakEnvDb - energyEnvDb);
        float peakWeight = std::clamp(transientDb / 12.0f, 0.0f, 1.0f);

        // Bass-heavy material shouldn't visually "spike" like transients.
        const float bassProximity = std::clamp((lowEnvDb - (energyEnvDb - 6.0f)) / 12.0f, 0.0f, 1.0f);
        peakWeight *= (1.0f - 0.65f * bassProximity);

        return energyEnvDb + peakWeight * (peakEnvDb - energyEnvDb);
    };

    // Dual-Bar Metering (Ableton Style):
    // 1. Peak Bar (Fast/Technical): Targets pure peak envelope.
    channel.smoothedPeakL = smoothDb(channel.smoothedPeakL, channel.envPeakL, DISPLAY_ATTACK_MS, DISPLAY_RELEASE_MS);
    channel.smoothedPeakR = smoothDb(channel.smoothedPeakR, channel.envPeakR, DISPLAY_ATTACK_MS, DISPLAY_RELEASE_MS);

    // 2. RMS Bar (Average/Body): Targets energy envelope.
    channel.smoothedRmsL = smoothDb(channel.smoothedRmsL, channel.envEnergyL, ENERGY_ATTACK_MS, ENERGY_RELEASE_MS);
    channel.smoothedRmsR = smoothDb(channel.smoothedRmsR, channel.envEnergyR, ENERGY_ATTACK_MS, ENERGY_RELEASE_MS);

    channel.smoothedPeakL = std::max(channel.smoothedPeakL, MixerMath::DB_MIN);
    channel.smoothedPeakR = std::max(channel.smoothedPeakR, MixerMath::DB_MIN);
    channel.smoothedRmsL = std::max(channel.smoothedRmsL, MixerMath::DB_MIN);
    channel.smoothedRmsR = std::max(channel.smoothedRmsR, MixerMath::DB_MIN);

    channel.smoothedPeakL = std::max(channel.smoothedPeakL, MixerMath::DB_MIN);
    channel.smoothedPeakR = std::max(channel.smoothedPeakR, MixerMath::DB_MIN);

    // Peak hold uses true peak (for gain-staging confidence).
    if (peakDbL > channel.peakHoldL) {
        channel.peakHoldL = peakDbL;
        channel.peakHoldTimerL = 0.0;
    } else {
        channel.peakHoldTimerL += deltaTime;
        if (channel.peakHoldTimerL > PEAK_HOLD_MS / 1000.0) {
            float decayCoeff = 1.0f - std::exp(static_cast<float>(-deltaTime * 1000.0 / PEAK_DECAY_MS));
            channel.peakHoldL += (MixerMath::DB_MIN - channel.peakHoldL) * decayCoeff;
        }
    }

    if (peakDbR > channel.peakHoldR) {
        channel.peakHoldR = peakDbR;
        channel.peakHoldTimerR = 0.0;
    } else {
        channel.peakHoldTimerR += deltaTime;
        if (channel.peakHoldTimerR > PEAK_HOLD_MS / 1000.0) {
            float decayCoeff = 1.0f - std::exp(static_cast<float>(-deltaTime * 1000.0 / PEAK_DECAY_MS));
            channel.peakHoldR += (MixerMath::DB_MIN - channel.peakHoldR) * decayCoeff;
        }
    }

    // Clip latch (sticky until cleared by user).
    if (snapshot.clipL) channel.clipLatchL = true;
    if (snapshot.clipR) channel.clipLatchR = true;
}

std::vector<MixerViewModel::Destination> MixerViewModel::getAvailableDestinations(uint32_t excludeId) const
{
    std::vector<Destination> dests;
    
    // Always add Master if we aren't Master
    if (excludeId != 0) {
        dests.push_back({0, "Master"});
    }

    // Add other channels (e.g. Buses/Returns/Tracks)
    // Note: In a real matrix, might filter to only Buses, but Nomad allows Track-to-Track sends.
    for (const auto& ch : m_channels) {
        if (!ch) continue;
        if (ch->id == excludeId) continue;
        if (ch->id == 0) continue; // Handled above

        dests.push_back({ch->id, ch->name});
    }

    return dests;
}

void MixerViewModel::addSend(uint32_t channelId) {
    auto* ch = getChannelById(channelId);
    if (!ch) return;
    
    // Create new SendViewModel
    ChannelViewModel::SendViewModel send;
    send.targetId = 0; // Default to Master
    send.targetName = "Master";
    send.gain = 1.0f; // 0dB
    ch->sends.push_back(send);

    // Update Engine
    if (auto mc = ch->channel.lock()) {
        Audio::AudioRoute route;
        route.targetChannelId = 0xFFFFFFFF; // Master
        route.gain = 1.0f;
        mc->addSend(route);
        
        if (m_onGraphDirty) m_onGraphDirty();
        if (m_onProjectModified) m_onProjectModified();
    }
}

void MixerViewModel::removeSend(uint32_t channelId, int sendIndex) {
    auto* ch = getChannelById(channelId);
    if (!ch || sendIndex < 0 || sendIndex >= static_cast<int>(ch->sends.size())) return;

    // Update Local Model
    ch->sends.erase(ch->sends.begin() + sendIndex);

    // Update Engine
    if (auto mc = ch->channel.lock()) {
        mc->removeSend(sendIndex);
        
        if (m_onGraphDirty) m_onGraphDirty();
        if (m_onProjectModified) m_onProjectModified();
    }
}

void MixerViewModel::setSendLevel(uint32_t channelId, int sendIndex, float linearGain) {
    auto* ch = getChannelById(channelId);
    if (!ch || sendIndex < 0 || sendIndex >= static_cast<int>(ch->sends.size())) return;

    ch->sends[sendIndex].gain = linearGain;

    // Update Engine
    if (auto mc = ch->channel.lock()) {
        mc->setSendLevel(sendIndex, linearGain);
    }
}

void MixerViewModel::setSendDestination(uint32_t channelId, int sendIndex, uint32_t targetId) {
    auto* ch = getChannelById(channelId);
    if (!ch || sendIndex < 0 || sendIndex >= static_cast<int>(ch->sends.size())) return;

    ch->sends[sendIndex].targetId = targetId;
    
    // Resolve name
    if (targetId == 0) {
        ch->sends[sendIndex].targetName = "Master";
    } else {
        auto* target = getChannelById(targetId);
        ch->sends[sendIndex].targetName = target ? target->name : "Unknown";
    }

    // Update Engine
    if (auto mc = ch->channel.lock()) {
        // Normalize 0 to 0xFFFFFFFF for engine master
        uint32_t engineId = (targetId == 0) ? 0xFFFFFFFF : targetId;
        mc->setSendDestination(sendIndex, engineId);
        
        if (m_onGraphDirty) m_onGraphDirty();
        if (m_onProjectModified) m_onProjectModified();
    }
}

} // namespace Nomad
