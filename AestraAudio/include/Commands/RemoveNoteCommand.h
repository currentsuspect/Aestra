// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to remove a MIDI note from a pattern (undoable)
 */
class RemoveNoteCommand : public ICommand {
public:
    RemoveNoteCommand(PatternManager& patternManager, PatternID patternId, const MidiNote& note);

    void execute() override;
    void undo() override;
    void redo() override;

    std::string getName() const override { return "Remove Note"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PatternManager& m_patternManager;
    PatternID m_patternId;
    MidiNote m_note; // Full copy of the note for undo
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
