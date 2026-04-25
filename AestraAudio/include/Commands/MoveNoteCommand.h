// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to move a MIDI note to a new position and/or pitch (undoable)
 */
class MoveNoteCommand : public ICommand {
public:
    MoveNoteCommand(PatternManager& patternManager, PatternID patternId,
                    const MidiNote& originalNote, double newStartBeat, int newPitch);

    void execute() override;
    void undo() override;
    void redo() override;

    std::string getName() const override { return "Move Note"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PatternManager& m_patternManager;
    PatternID m_patternId;

    // Original state for undo
    MidiNote m_originalNote;

    // New state
    double m_newStartBeat;
    int m_newPitch;

    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
