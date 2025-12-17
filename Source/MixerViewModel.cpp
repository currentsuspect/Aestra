// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "MixerViewModel.h"

namespace Nomad {

MixerViewModel::MixerViewModel() {
    // Create master channel
    m_master = std::make_unique<ChannelViewModel>();
    m_master->id = 0;
    m_master->slotIndex = Audio::ChannelSlotMap::MASTER_SLOT_INDEX;
    m_master->name = "Master";
    m_master->trackColor = 0xFF8B7FFF; // Nomad purple
}

void MixerViewModel::updateMeters(const Audio::MeterSnapshotBuffer& snapshots, double deltaTime) {
    // Update channel meters
    for (auto& channel : m_channels) {
        if (!channel) continue;

        float linearL, linearR;
        bool clipL, clipR;
        snapshots.readSnapshot(channel->slotIndex, linearL, linearR, clipL, clipR);
        smoothMeterChannel(*channel, linearL, linearR, clipL, clipR, deltaTime);
    }

    // Update master meter
    if (m_master) {
        float linearL, linearR;
        bool clipL, clipR;
        snapshots.readSnapshot(m_master->slotIndex, linearL, linearR, clipL, clipR);
        smoothMeterChannel(*m_master, linearL, linearR, clipL, clipR, deltaTime);
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

    // Collect track info from engine
    std::vector<std::tuple<uint32_t, std::string, uint32_t, uint32_t>> trackInfo;
    for (size_t i = 0; i < trackManager.getTrackCount(); ++i) {
        auto track = trackManager.getTrack(i);
        if (track) {
            uint32_t id = track->getTrackId();
            uint32_t slot = slotMap.getSlotIndex(id);
            if (slot != Audio::ChannelSlotMap::INVALID_SLOT) {
                trackInfo.emplace_back(id, track->getName(), track->getColor(), slot);
            }
        }
    }

    // Rebuild channel list to match tracks
    std::vector<std::unique_ptr<ChannelViewModel>> newChannels;
    newChannels.reserve(trackInfo.size());

    for (const auto& [id, name, color, slot] : trackInfo) {
        auto it = existingIds.find(id);
        if (it != existingIds.end() && m_channels[it->second]) {
            // Reuse existing channel (preserves meter state)
            auto& existing = m_channels[it->second];
            existing->name = name;
            existing->trackColor = color;
            existing->slotIndex = slot;
            newChannels.push_back(std::move(existing));
        } else {
            // Create new channel
            auto channel = std::make_unique<ChannelViewModel>();
            channel->id = id;
            channel->slotIndex = slot;
            channel->name = name;
            channel->trackColor = color;
            newChannels.push_back(std::move(channel));
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
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end() || it->second >= m_channels.size()) {
        return nullptr;
    }
    return m_channels[it->second].get();
}

const ChannelViewModel* MixerViewModel::getChannelById(uint32_t id) const {
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
                                         float linearL, float linearR,
                                         bool clipL, bool clipR,
                                         double deltaTime) {
    // Convert LINEAR to dB
    float dbL = MixerMath::linearToDb(linearL);
    float dbR = MixerMath::linearToDb(linearR);

    // Calculate smoothing coefficients
    // coeff = 1 - exp(-deltaTime / tau), where tau = time_ms / 1000
    float attackCoeff = 1.0f - std::exp(static_cast<float>(-deltaTime * 1000.0 / METER_ATTACK_MS));
    float releaseCoeff = 1.0f - std::exp(static_cast<float>(-deltaTime * 1000.0 / METER_RELEASE_MS));

    // Apply attack/release smoothing (in dB space)
    // Attack: fast rise to new peak
    // Release: slow decay to new level
    if (dbL > channel.smoothedPeakL) {
        channel.smoothedPeakL += (dbL - channel.smoothedPeakL) * attackCoeff;
    } else {
        channel.smoothedPeakL += (dbL - channel.smoothedPeakL) * releaseCoeff;
    }

    if (dbR > channel.smoothedPeakR) {
        channel.smoothedPeakR += (dbR - channel.smoothedPeakR) * attackCoeff;
    } else {
        channel.smoothedPeakR += (dbR - channel.smoothedPeakR) * releaseCoeff;
    }

    // Clamp to valid range
    channel.smoothedPeakL = std::max(channel.smoothedPeakL, MixerMath::DB_MIN);
    channel.smoothedPeakR = std::max(channel.smoothedPeakR, MixerMath::DB_MIN);

    // Update peak hold (left)
    if (dbL > channel.peakHoldL) {
        channel.peakHoldL = dbL;
        channel.peakHoldTimerL = 0.0;
    } else {
        channel.peakHoldTimerL += deltaTime;
        if (channel.peakHoldTimerL > PEAK_HOLD_MS / 1000.0) {
            // Decay peak hold
            float decayCoeff = 1.0f - std::exp(static_cast<float>(-deltaTime * 1000.0 / PEAK_DECAY_MS));
            channel.peakHoldL += (MixerMath::DB_MIN - channel.peakHoldL) * decayCoeff;
        }
    }

    // Update peak hold (right)
    if (dbR > channel.peakHoldR) {
        channel.peakHoldR = dbR;
        channel.peakHoldTimerR = 0.0;
    } else {
        channel.peakHoldTimerR += deltaTime;
        if (channel.peakHoldTimerR > PEAK_HOLD_MS / 1000.0) {
            // Decay peak hold
            float decayCoeff = 1.0f - std::exp(static_cast<float>(-deltaTime * 1000.0 / PEAK_DECAY_MS));
            channel.peakHoldR += (MixerMath::DB_MIN - channel.peakHoldR) * decayCoeff;
        }
    }

    // Update clip latch (sticky until cleared by user)
    if (clipL) channel.clipLatchL = true;
    if (clipR) channel.clipLatchR = true;
}

} // namespace Nomad
