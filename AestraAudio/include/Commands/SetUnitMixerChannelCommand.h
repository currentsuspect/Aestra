// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

namespace Aestra {
namespace Audio {

/** Route an Arsenal unit to a stable mixer destination through CommandHistory. */
class SetUnitMixerChannelCommand final : public ICommand {
public:
    SetUnitMixerChannelCommand(TrackManager& manager, UnitID unitId, uint32_t channelId)
        : m_manager(manager), m_unitId(unitId), m_channelId(channelId) {}

    void execute() override {
        if (m_executed || !m_manager.getUnitManager().getUnit(m_unitId))
            return;
        m_previousChannelId = m_manager.getUnitManager().getUnitMixerChannel(m_unitId);
        if (m_previousChannelId == m_channelId)
            return;
        m_manager.getUnitManager().setUnitMixerChannel(m_unitId, m_channelId);
        m_manager.markModified();
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;
        m_manager.getUnitManager().setUnitMixerChannel(m_unitId, m_previousChannelId);
        m_manager.markModified();
        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;
        m_manager.getUnitManager().setUnitMixerChannel(m_unitId, m_channelId);
        m_manager.markModified();
        m_executed = true;
    }

    std::string getName() const override { return "Route Unit to Mixer Insert"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return m_executed; }

private:
    TrackManager& m_manager;
    UnitID m_unitId;
    uint32_t m_channelId;
    uint32_t m_previousChannelId{MASTER_MIXER_CHANNEL_ID};
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
