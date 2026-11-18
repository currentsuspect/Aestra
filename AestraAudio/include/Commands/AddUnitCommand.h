// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/UnitManager.h"
#include "Models/PatternManager.h"

#include <memory>
#include <optional>
#include <algorithm>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to add an Arsenal unit (undoable)
 *
 * Creating a non-audio unit also creates its default MIDI pattern (via
 * UnitManager). Undo removes the unit; the default pattern is left behind,
 * matching how the Arsenal panel removes units. Like AddChannelCommand,
 * redo after undo mints a fresh unit ID.
 */
class AddUnitCommand : public ICommand {
public:
    AddUnitCommand(UnitManager& unitManager, const std::string& name, UnitType type)
        : m_unitManager(unitManager), m_name(name), m_type(type) {}

    void execute() override {
        if (m_executed) return;
        if (m_snapshot.has_value()) {
            if (!m_unitManager.restoreUnit(*m_snapshot, m_orderIndex)) return;
            if (m_detachedPattern && m_unitManager.getPatternManager() &&
                !m_unitManager.getPatternManager()->reinsertPattern(m_detachedPattern)) {
                m_unitManager.removeUnit(m_snapshot->id);
                return;
            }
            m_createdUnitId = m_snapshot->id;
        } else {
            m_createdUnitId = m_unitManager.createUnit(m_name, m_type);
            if (m_createdUnitId == 0) return;
            m_unitManager.setUnitEnabled(m_createdUnitId, true);
            if (const auto* unit = m_unitManager.getUnit(m_createdUnitId)) {
                m_snapshot = *unit;
                m_orderIndex = m_unitManager.getAllUnitIDs().size() - 1;
            }
        }
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        const auto* unit = m_unitManager.getUnit(m_createdUnitId);
        if (!unit) return;
        m_snapshot = *unit;
        const auto ids = m_unitManager.getAllUnitIDs();
        const auto it = std::find(ids.begin(), ids.end(), m_createdUnitId);
        if (it != ids.end()) m_orderIndex = static_cast<size_t>(std::distance(ids.begin(), it));

        if (m_snapshot->defaultPatternId.isValid() && m_unitManager.getPatternManager()) {
            m_detachedPattern = m_unitManager.getPatternManager()->detachPattern(m_snapshot->defaultPatternId);
        }
        if (!m_unitManager.removeUnit(m_createdUnitId)) return;
        m_executed = false;
    }

    void redo() override { execute(); }

    std::string getName() const override { return "Add Unit"; }
    UnitID getCreatedUnitId() const { return m_executed ? m_createdUnitId : UnitID{0}; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return m_executed; }

private:
    UnitManager& m_unitManager;
    std::string m_name;
    UnitType m_type;
    UnitID m_createdUnitId{0};
    size_t m_orderIndex{0};
    std::optional<UnitInfo> m_snapshot;
    std::unique_ptr<PatternSource> m_detachedPattern;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
