// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/ResizeNoteCommand.h"

namespace Aestra {
namespace Audio {

ResizeNoteCommand::ResizeNoteCommand(PatternManager& patternManager, PatternID patternId,
                                     const MidiNote& originalNote, double newDurationBeats)
    : m_patternManager(patternManager), m_patternId(patternId),
      m_originalNote(originalNote), m_newDurationBeats(newDurationBeats) {}

void ResizeNoteCommand::execute() {
    if (m_executed)
        return;

    m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
        if (pattern.isMidi()) {
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;
            for (auto& note : notes) {
                if (note.pitch == m_originalNote.pitch &&
                    note.startBeat == m_originalNote.startBeat &&
                    note.unitId == m_originalNote.unitId) {
                    note.durationBeats = m_newDurationBeats;
                    break;
                }
            }
        }
    });

    m_executed = true;
}

void ResizeNoteCommand::undo() {
    if (!m_executed)
        return;

    m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
        if (pattern.isMidi()) {
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;
            for (auto& note : notes) {
                if (note.pitch == m_originalNote.pitch &&
                    note.startBeat == m_originalNote.startBeat &&
                    note.unitId == m_originalNote.unitId) {
                    note.durationBeats = m_originalNote.durationBeats;
                    break;
                }
            }
        }
    });

    m_executed = false;
}

void ResizeNoteCommand::redo() {
    if (m_executed)
        return;

    execute();
}

} // namespace Audio
} // namespace Aestra
