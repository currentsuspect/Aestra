// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/MoveNoteCommand.h"

namespace Aestra {
namespace Audio {

MoveNoteCommand::MoveNoteCommand(PatternManager& patternManager, PatternID patternId,
                                 const MidiNote& originalNote, double newStartBeat, int newPitch)
    : m_patternManager(patternManager), m_patternId(patternId),
      m_originalNote(originalNote), m_newStartBeat(newStartBeat), m_newPitch(newPitch) {}

void MoveNoteCommand::execute() {
    if (m_executed)
        return;

    m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
        if (pattern.isMidi()) {
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;
            for (auto& note : notes) {
                if (note.pitch == m_originalNote.pitch &&
                    note.startBeat == m_originalNote.startBeat &&
                    note.unitId == m_originalNote.unitId) {
                    note.startBeat = m_newStartBeat;
                    note.pitch = m_newPitch;
                    break;
                }
            }
        }
    });

    m_executed = true;
}

void MoveNoteCommand::undo() {
    if (!m_executed)
        return;

    m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
        if (pattern.isMidi()) {
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;
            for (auto& note : notes) {
                // Match by new position (where the note was moved to)
                if (note.pitch == m_newPitch &&
                    note.startBeat == m_newStartBeat &&
                    note.unitId == m_originalNote.unitId) {
                    note.startBeat = m_originalNote.startBeat;
                    note.pitch = m_originalNote.pitch;
                    break;
                }
            }
        }
    });

    m_executed = false;
}

void MoveNoteCommand::redo() {
    if (m_executed)
        return;

    execute();
}

} // namespace Audio
} // namespace Aestra
