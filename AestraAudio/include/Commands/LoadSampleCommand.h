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

    void execute() override {
        if (m_executed) return;
        const UnitInfo* unit = m_unitManager.getUnit(m_unitId);
        if (!unit) return;
        m_previousPath = unit->audioClipPath;
        m_unitManager.setUnitAudioClip(m_unitId, m_path);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        m_unitManager.setUnitAudioClip(m_unitId, m_previousPath);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Load Sample"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    UnitManager& m_unitManager;
    UnitID m_unitId;
    std::string m_path;
    std::string m_previousPath;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
