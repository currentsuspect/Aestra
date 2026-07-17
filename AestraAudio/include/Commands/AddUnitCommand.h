// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/UnitManager.h"

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
        m_createdUnitId = m_unitManager.createUnit(m_name, m_type);
        m_unitManager.setUnitEnabled(m_createdUnitId, true);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        if (m_unitManager.removeUnit(m_createdUnitId)) {
            m_executed = false;
        }
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Add Unit"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    UnitManager& m_unitManager;
    std::string m_name;
    UnitType m_type;
    UnitID m_createdUnitId{0};
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
