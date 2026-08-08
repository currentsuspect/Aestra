// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <memory>
#include <unordered_set>

namespace Aestra {
namespace Audio {

namespace detail {
struct FirstFreeMixerChannelResult {
    uint32_t channelId{MASTER_MIXER_CHANNEL_ID};
    bool created{false};
};

/** Resolve the first mixer channel not owned by another source, creating one only when necessary. */
inline FirstFreeMixerChannelResult selectOrCreateFirstFreeMixerChannel(TrackManager& manager, UnitID unitId,
                                                                        const std::string& destinationName,
                                                                        uint32_t color) {
    std::unordered_set<uint32_t> usedChannelIds;
    for (const UnitID otherUnitId : manager.getUnitManager().getAllUnitIDs()) {
        if (otherUnitId == unitId)
            continue;
        const uint32_t channelId = manager.getUnitManager().getUnitMixerChannel(otherUnitId);
        if (channelId != MASTER_MIXER_CHANNEL_ID) {
            usedChannelIds.insert(channelId);
        }
    }
    for (const auto& pattern : manager.getPatternManager().getAllPatterns()) {
        if (!pattern || !pattern->isAudio())
            continue;
        const uint32_t channelId = pattern->getMixerChannelId();
        if (channelId != MASTER_MIXER_CHANNEL_ID) {
            usedChannelIds.insert(channelId);
        }
    }

    for (size_t i = 0; i < manager.getChannelCount(); ++i) {
        const auto* channel = manager.getChannel(i);
        if (channel && usedChannelIds.find(channel->getChannelId()) == usedChannelIds.end()) {
            return {channel->getChannelId(), false};
        }
    }

    auto* channel = manager.addChannel(destinationName);
    if (!channel)
        return {};
    channel->setColor(color);
    return {channel->getChannelId(), true};
}
} // namespace detail

/**
 * Route a producer-facing Arsenal unit to its default mixer destination without creating Timeline placement state.
 *
 * Unit creation is not command-backed today, so its default route belongs to the same creation operation rather than
 * becoming a separate Undo entry. Bootstrap callers can preserve the incoming dirty-state while still publishing the
 * stable mixer route.
 */
inline bool routeUnitToFirstFreeMixerChannel(TrackManager& manager, UnitID unitId, const std::string& destinationName,
                                             uint32_t color, bool preserveModifiedState = false) {
    if (!manager.getUnitManager().getUnit(unitId))
        return false;

    const bool wasModified = manager.isModified();
    const auto result = detail::selectOrCreateFirstFreeMixerChannel(manager, unitId, destinationName, color);
    if (result.channelId == MASTER_MIXER_CHANNEL_ID) {
        if (preserveModifiedState)
            manager.setModified(wasModified);
        return false;
    }

    manager.getUnitManager().setUnitMixerChannel(unitId, result.channelId);
    if (preserveModifiedState) {
        manager.setModified(wasModified);
    } else {
        manager.markModified();
    }
    return true;
}

/** Assign a unit to the first unused mixer channel. */
class AssignUnitToFirstFreeMixerChannelCommand final : public ICommand {
public:
    AssignUnitToFirstFreeMixerChannelCommand(TrackManager& manager, UnitID unitId, std::string destinationName,
                                              uint32_t color)
        : m_manager(manager), m_unitId(unitId), m_destinationName(std::move(destinationName)), m_color(color) {}

    void execute() override {
        if (m_executed || !m_manager.getUnitManager().getUnit(m_unitId))
            return;

        if (!m_capturedPreviousRoute) {
            m_previousChannelId = m_manager.getUnitManager().getUnitMixerChannel(m_unitId);
            m_capturedPreviousRoute = true;
        }

        if (m_createdDestination) {
            if (!m_manager.reinsertChannel(m_detachedChannel, m_channelIndex))
                return;
        } else if (m_destinationChannelId == MASTER_MIXER_CHANNEL_ID) {
            const size_t channelCountBefore = m_manager.getChannelCount();
            const auto result =
                detail::selectOrCreateFirstFreeMixerChannel(m_manager, m_unitId, m_destinationName, m_color);
            m_destinationChannelId = result.channelId;
            m_createdDestination = result.created;
            if (m_destinationChannelId == MASTER_MIXER_CHANNEL_ID)
                return;
            if (m_createdDestination) {
                if (m_manager.getChannelCount() <= channelCountBefore)
                    return;
                m_channelIndex = m_manager.getChannelCount() - 1;
            }
        }

        m_manager.getUnitManager().setUnitMixerChannel(m_unitId, m_destinationChannelId);
        m_manager.markModified();
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;

        m_manager.getUnitManager().setUnitMixerChannel(m_unitId, m_previousChannelId);
        if (m_createdDestination) {
            m_detachedChannel = m_manager.detachChannelById(m_destinationChannelId, m_channelIndex);
            if (!m_detachedChannel) {
                m_manager.getUnitManager().setUnitMixerChannel(m_unitId, m_destinationChannelId);
                return;
            }
        }
        m_manager.markModified();
        m_executed = false;
    }

    void redo() override { execute(); }

    std::string getName() const override { return "Assign Unit to First Free Mixer Channel"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return m_executed; }

private:
    TrackManager& m_manager;
    UnitID m_unitId;
    std::string m_destinationName;
    uint32_t m_color;
    uint32_t m_previousChannelId{MASTER_MIXER_CHANNEL_ID};
    uint32_t m_destinationChannelId{MASTER_MIXER_CHANNEL_ID};
    std::unique_ptr<MixerChannel> m_detachedChannel;
    size_t m_channelIndex{0};
    bool m_capturedPreviousRoute{false};
    bool m_createdDestination{false};
    bool m_executed{false};
};

/** Execute and record first-free mixer routing, reporting whether project state changed. */
inline bool assignUnitToFirstFreeMixerChannel(TrackManager& manager, UnitID unitId, const std::string& destinationName,
                                              uint32_t color) {
    auto command =
        std::make_shared<AssignUnitToFirstFreeMixerChannelCommand>(manager, unitId, destinationName, color);
    manager.getCommandHistory().pushAndExecute(command);
    return command->isUndoable();
}

} // namespace Audio
} // namespace Aestra
