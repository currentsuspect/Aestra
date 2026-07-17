// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"

#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to set a pattern's length in beats (undoable)
 *
 * Notes past the new length are kept (they simply stop being scheduled), so
 * shortening then restoring the length loses nothing.
 */
class SetPatternLengthCommand : public ICommand {
public:
    SetPatternLengthCommand(PatternManager& patternManager, PatternID patternId, double beats)
        : m_patternManager(patternManager), m_patternId(patternId), m_beats(beats) {}

    void execute() override {
        if (m_executed) return;
        m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
            m_previousBeats = pattern.lengthBeats;
            pattern.lengthBeats = m_beats;
        });
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
            pattern.lengthBeats = m_previousBeats;
        });
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Set Pattern Length"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PatternManager& m_patternManager;
    PatternID m_patternId;
    double m_beats;
    double m_previousBeats = 0.0;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
