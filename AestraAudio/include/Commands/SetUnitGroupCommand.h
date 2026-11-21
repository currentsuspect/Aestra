// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <utility>
#include <vector>

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

/** Regroup every sound from its name at once (unitGroupForSample), one undo step: for projects saved before the
 *  first guess existed, where every sampler sat under Drums (#1400). Audio units are left alone. */
class RegroupUnitsByNameCommand final : public ICommand {
public:
    explicit RegroupUnitsByNameCommand(TrackManager& manager) : m_manager(manager) {}

    void execute() override {
        auto& units = m_manager.getUnitManager();
        m_changed.clear();
        for (const UnitID id : units.getAllUnitIDs()) {
            const auto* unit = units.getUnit(id);
            if (!unit || unit->type == UnitType::Audio) continue;
            const UnitGroup guess = unitGroupForSample(unit->type, unit->name);
            if (guess == unit->group) continue;
            m_changed.push_back({id, unit->group});
            units.setUnitGroup(id, guess);
        }
        if (!m_changed.empty()) m_manager.markModified();
    }

    void undo() override {
        for (const auto& [id, previous] : m_changed) m_manager.getUnitManager().setUnitGroup(id, previous);
        if (!m_changed.empty()) m_manager.markModified();
    }

    void redo() override { execute(); }

    std::string getName() const override { return "Regroup Sounds by Name"; }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_changed.size() * sizeof(m_changed[0]); }
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return !m_changed.empty(); }
    size_t changedCount() const { return m_changed.size(); }

private:
    TrackManager& m_manager;
    std::vector<std::pair<UnitID, UnitGroup>> m_changed;
};

} // namespace Audio
} // namespace Aestra
