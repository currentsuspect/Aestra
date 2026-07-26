// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <memory>
#include <unordered_set>

namespace Aestra {
namespace Audio {

/** Assign a unit to the first unused insert, atomically creating a lane and insert when necessary. */
class AssignUnitToFirstFreeInsertCommand final : public ICommand {
public:
    AssignUnitToFirstFreeInsertCommand(TrackManager& manager, UnitID unitId, std::string destinationName,
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
            auto& playlist = m_manager.getPlaylistModel();
            const auto restoredLaneId = playlist.createLaneWithId(m_laneId, m_destinationName);
            if (restoredLaneId != m_laneId) {
                playlist.removeLane(restoredLaneId);
                return;
            }
            if (!m_manager.reinsertChannel(m_detachedChannel, m_channelIndex)) {
                playlist.removeLane(m_laneId);
                return;
            }
            if (auto* lane = playlist.getLane(m_laneId)) {
                lane->colorRGBA = m_color;
            }
        } else if (m_destinationChannelId == MASTER_MIXER_CHANNEL_ID) {
            selectOrCreateDestination();
            if (m_destinationChannelId == MASTER_MIXER_CHANNEL_ID)
                return;
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
            m_manager.getPlaylistModel().removeLane(m_laneId);
        }
        m_manager.markModified();
        m_executed = false;
    }

    void redo() override { execute(); }

    std::string getName() const override { return "Assign Unit to First Free Insert"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return m_executed; }

private:
    void selectOrCreateDestination() {
        std::unordered_set<uint32_t> usedChannelIds;
        for (const UnitID unitId : m_manager.getUnitManager().getAllUnitIDs()) {
            if (unitId == m_unitId)
                continue;
            const uint32_t channelId = m_manager.getUnitManager().getUnitMixerChannel(unitId);
            if (channelId != MASTER_MIXER_CHANNEL_ID) {
                usedChannelIds.insert(channelId);
            }
        }
        for (const auto& pattern : m_manager.getPatternManager().getAllPatterns()) {
            if (!pattern || !pattern->isAudio())
                continue;
            const uint32_t channelId = pattern->getMixerChannelId();
            if (channelId != MASTER_MIXER_CHANNEL_ID) {
                usedChannelIds.insert(channelId);
            }
        }

        for (size_t i = 0; i < m_manager.getChannelCount(); ++i) {
            const auto* channel = m_manager.getChannel(i);
            if (channel && usedChannelIds.find(channel->getChannelId()) == usedChannelIds.end()) {
                m_destinationChannelId = channel->getChannelId();
                return;
            }
        }

        auto& playlist = m_manager.getPlaylistModel();
        m_laneId = playlist.createLane(m_destinationName);
        if (!m_laneId.isValid())
            return;

        auto* channel = m_manager.addChannel(m_destinationName);
        if (!channel) {
            playlist.removeLane(m_laneId);
            m_laneId = {};
            return;
        }

        m_destinationChannelId = channel->getChannelId();
        channel->setColor(m_color);
        if (auto* lane = playlist.getLane(m_laneId)) {
            lane->colorRGBA = m_color;
        }
        m_createdDestination = true;
    }

    TrackManager& m_manager;
    UnitID m_unitId;
    std::string m_destinationName;
    uint32_t m_color;
    uint32_t m_previousChannelId{MASTER_MIXER_CHANNEL_ID};
    uint32_t m_destinationChannelId{MASTER_MIXER_CHANNEL_ID};
    PlaylistLaneID m_laneId;
    std::unique_ptr<MixerChannel> m_detachedChannel;
    size_t m_channelIndex{0};
    bool m_capturedPreviousRoute{false};
    bool m_createdDestination{false};
    bool m_executed{false};
};

/** Execute and record first-free routing, reporting whether project state changed. */
inline bool assignUnitToFirstFreeInsert(TrackManager& manager, UnitID unitId, const std::string& destinationName,
                                        uint32_t color) {
    auto command = std::make_shared<AssignUnitToFirstFreeInsertCommand>(manager, unitId, destinationName, color);
    manager.getCommandHistory().pushAndExecute(command);
    return command->isUndoable();
}

} // namespace Audio
} // namespace Aestra
