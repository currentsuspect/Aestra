// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"

#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to clone a pattern (undoable)
 *
 * Undo removes the clone. Like AddChannelCommand, redo after undo mints a
 * fresh pattern id. getClonedPatternId() is valid after execute().
 */
class ClonePatternCommand : public ICommand {
public:
    ClonePatternCommand(PatternManager& patternManager, PatternID sourceId)
        : m_patternManager(patternManager), m_sourceId(sourceId) {}

    void execute() override {
        if (m_executed) return;
        m_clonedId = m_patternManager.clonePattern(m_sourceId);
        m_executed = m_clonedId.isValid();
    }

    void undo() override {
        if (!m_executed) return;
        m_patternManager.removePattern(m_clonedId);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Clone Pattern"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

    PatternID getClonedPatternId() const { return m_clonedId; }

private:
    PatternManager& m_patternManager;
    PatternID m_sourceId;
    PatternID m_clonedId;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
