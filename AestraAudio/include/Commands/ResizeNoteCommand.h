// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to resize a MIDI note's duration (undoable)
 */
class ResizeNoteCommand : public ICommand {
public:
    ResizeNoteCommand(PatternManager& patternManager, PatternID patternId,
                      const MidiNote& originalNote, double newDurationBeats);

    void execute() override;
    void undo() override;
    void redo() override;

    std::string getName() const override { return "Resize Note"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PatternManager& m_patternManager;
    PatternID m_patternId;

    // Original note state for identification and undo
    MidiNote m_originalNote;

    // New duration
    double m_newDurationBeats;

    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
