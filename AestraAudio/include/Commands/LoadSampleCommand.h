// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/UnitManager.h"

#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to load an audio sample into a unit (undoable)
 *
 * Undo restores the unit's previous sample path (possibly none) through the
 * same UnitManager::setUnitAudioClip path, so the sampler plugin state and
 * preview waveform stay consistent in both directions.
 */
class LoadSampleCommand : public ICommand {
public:
    LoadSampleCommand(UnitManager& unitManager, UnitID unitId, const std::string& path)
        : m_unitManager(unitManager), m_unitId(unitId), m_path(path) {}

    /** Record an asynchronously applied load as one undoable command without decoding the sample twice. */
    void markExecuted(const std::string& previousPath, const std::string& previousName, bool previousEnabled) {
        m_previousPath = previousPath;
        m_previousName = previousName;
        m_previousEnabled = previousEnabled;
        if (const auto* unit = m_unitManager.getUnit(m_unitId)) {
            m_newName = unit->name;
            m_newEnabled = unit->isEnabled || unit->enabled;
        }
        m_executed = true;
    }

    void execute() override {
        if (m_executed) return;
        const UnitInfo* unit = m_unitManager.getUnit(m_unitId);
        if (!unit) return;
        m_previousPath = unit->audioClipPath;
        m_previousName = unit->name;
        m_previousEnabled = unit->isEnabled || unit->enabled;
        m_unitManager.setUnitAudioClip(m_unitId, m_path);
        m_unitManager.setUnitEnabled(m_unitId, true);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        m_unitManager.setUnitAudioClip(m_unitId, m_previousPath);
        m_unitManager.setUnitName(m_unitId, m_previousName);
        m_unitManager.setUnitEnabled(m_unitId, m_previousEnabled);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Load Sample"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return m_executed; }

private:
    UnitManager& m_unitManager;
    UnitID m_unitId;
    std::string m_path;
    std::string m_previousPath;
    std::string m_previousName;
    std::string m_newName;
    bool m_previousEnabled{false};
    bool m_newEnabled{true};
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
