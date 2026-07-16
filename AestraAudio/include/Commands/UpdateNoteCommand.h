// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to update a MIDI note's expression (velocity/pan) in place (undoable)
 *
 * Identifies the target note by pitch + startBeat + unitId, like
 * ResizeNoteCommand — position and duration are unchanged by this command.
 */
class UpdateNoteCommand : public ICommand {
public:
    UpdateNoteCommand(PatternManager& patternManager, PatternID patternId,
                      const MidiNote& originalNote, float newVelocity, float newPan);

    void execute() override;
    void undo() override;
    void redo() override;

    std::string getName() const override { return "Update Note"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PatternManager& m_patternManager;
    PatternID m_patternId;

    // Original note state for identification and undo
    MidiNote m_originalNote;

    // New expression values
    float m_newVelocity;
    float m_newPan;

    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
