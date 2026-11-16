// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

namespace Aestra {
namespace Audio {

/** Move an Arsenal unit to another group (Synth / Drums), one undo step (#1400, FD-96). The type is unchanged. */
class SetUnitGroupCommand final : public ICommand {
public:
    SetUnitGroupCommand(TrackManager& manager, UnitID unitId, UnitGroup group)
        : m_manager(manager), m_unitId(unitId), m_group(group) {}

    void execute() override {
        const auto* unit = m_manager.getUnitManager().getUnit(m_unitId);
        if (m_executed || !unit || unit->group == m_group) return;
        m_previous = unit->group;
        m_manager.getUnitManager().setUnitGroup(m_unitId, m_group);
        m_manager.markModified();
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        m_manager.getUnitManager().setUnitGroup(m_unitId, m_previous);
        m_manager.markModified();
        m_executed = false;
    }

    void redo() override { execute(); }

    std::string getName() const override { return "Move Unit to Group"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return m_executed; }

private:
    TrackManager& m_manager;
    UnitID m_unitId;
    UnitGroup m_group;
    UnitGroup m_previous{UnitGroup::Unknown};
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
