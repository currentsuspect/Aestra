// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "ChannelSlotMap.h"
#include "MixerChannel.h"

namespace Nomad {
namespace Audio {

void ChannelSlotMap::rebuild(const std::vector<std::shared_ptr<MixerChannel>>& channels) {
    m_idToSlot.clear();
    m_slotToId.clear();
    m_channelCount = 0;

    uint32_t slot = 0;
    for (const auto& channel : channels) {
        if (channel && slot < MAX_CHANNEL_SLOTS) {
            uint32_t channelId = channel->getChannelId();
            m_idToSlot[channelId] = slot;
            m_slotToId[slot] = channelId;
            ++slot;
        }
    }
    m_channelCount = slot;
}


uint32_t ChannelSlotMap::getSlotIndex(uint32_t channelId) const {
    auto it = m_idToSlot.find(channelId);
    return (it != m_idToSlot.end()) ? it->second : INVALID_SLOT;
}

uint32_t ChannelSlotMap::getChannelId(uint32_t slotIndex) const {
    auto it = m_slotToId.find(slotIndex);
    return (it != m_slotToId.end()) ? it->second : INVALID_SLOT;
}

bool ChannelSlotMap::hasChannel(uint32_t channelId) const {
    return m_idToSlot.find(channelId) != m_idToSlot.end();
}

void ChannelSlotMap::clear() {
    m_idToSlot.clear();
    m_slotToId.clear();
    m_channelCount = 0;
}

} // namespace Audio
} // namespace Nomad
