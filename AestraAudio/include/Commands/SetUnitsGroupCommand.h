// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/UnitManager.h"

#include <utility>
#include <vector>

namespace Aestra::Audio {

/** Set the Arsenal rack group for one or more units as one undoable edit. */
class SetUnitsGroupCommand final : public ICommand {
public:
    SetUnitsGroupCommand(UnitManager& units, std::vector<UnitID> ids, UnitGroup group)
        : m_units(units), m_ids(std::move(ids)), m_group(group) {}

    void execute() override {
        if (m_executed) return;
        if (m_before.empty()) {
            for (const UnitID id : m_ids) {
                if (const auto* unit = m_units.getUnit(id); unit && unit->group != m_group)
                    m_before.emplace_back(id, unit->group);
            }
        }
        for (const auto& [id, _] : m_before) m_units.setUnitGroup(id, m_group);
        m_executed = !m_before.empty();
    }

    void undo() override {
        if (!m_executed) return;
        for (const auto& [id, group] : m_before) m_units.setUnitGroup(id, group);
        m_executed = false;
    }

    void redo() override { execute(); }
    std::string getName() const override { return "Move Arsenal Unit(s) to Group"; }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_before.size() * sizeof(m_before.front()); }
    bool isUndoable() const override { return m_executed; }

private:
    UnitManager& m_units;
    std::vector<UnitID> m_ids;
    UnitGroup m_group;
    std::vector<std::pair<UnitID, UnitGroup>> m_before;
    bool m_executed{false};
};

} // namespace Aestra::Audio
