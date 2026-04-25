// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to add a MIDI note to a pattern (undoable)
 */
class AddNoteCommand : public ICommand {
public:
    AddNoteCommand(PatternManager& patternManager, PatternID patternId, const MidiNote& note);

    void execute() override;
    void undo() override;
    void redo() override;

    std::string getName() const override { return "Add Note"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PatternManager& m_patternManager;
    PatternID m_patternId;
    MidiNote m_note;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
