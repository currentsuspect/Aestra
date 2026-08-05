// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/UnitManager.h"

#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to set a unit's linear output gain (undoable)
 */
class SetUnitGainCommand : public ICommand {
public:
    SetUnitGainCommand(UnitManager& unitManager, UnitID unitId, float gain)
        : m_unitManager(unitManager), m_unitId(unitId), m_gain(gain) {}

    void execute() override {
        if (m_executed) return;
        const UnitInfo* unit = m_unitManager.getUnit(m_unitId);
        if (!unit) return;
        m_previousGain = unit->gain;
        m_unitManager.setUnitGain(m_unitId, m_gain);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        m_unitManager.setUnitGain(m_unitId, m_previousGain);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Set Unit Gain"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    UnitManager& m_unitManager;
    UnitID m_unitId;
    float m_gain;
    float m_previousGain = 1.0f;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
