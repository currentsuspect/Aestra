// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "ChannelSlotMap.h"

#include "MixerChannel.h"

namespace Aestra {
namespace Audio {

void ChannelSlotMap::rebuild(const std::vector<std::unique_ptr<MixerChannel>>& channels) {
    m_slotToId.fill(INVALID_SLOT);
    m_channelCount = 0;

    uint32_t slot = 0;
    for (const auto& channel : channels) {
        if (channel && slot < MAX_CHANNEL_SLOTS) {
            uint32_t channelId = channel->getChannelId();
            m_slotToId[slot] = channelId;
            ++slot;
        }
    }
    m_channelCount = slot;
}

uint32_t ChannelSlotMap::getSlotIndex(uint32_t channelId) const {
    // Linear scan of at most 127 entries. Deterministic, cache-friendly,
    // no hash computation. Called from RT path but n is tiny.
    const uint32_t count = m_channelCount;
    for (uint32_t i = 0; i < count; ++i) {
        if (m_slotToId[i] == channelId)
            return i;
    }
    return INVALID_SLOT;
}

uint32_t ChannelSlotMap::getChannelId(uint32_t slotIndex) const {
    if (slotIndex >= ARRAY_SIZE)
        return INVALID_SLOT;
    return m_slotToId[slotIndex];
}

bool ChannelSlotMap::hasChannel(uint32_t channelId) const {
    return getSlotIndex(channelId) != INVALID_SLOT;
}

void ChannelSlotMap::clear() {
    m_slotToId.fill(INVALID_SLOT);
    m_channelCount = 0;
}

} // namespace Audio
} // namespace Aestra
