// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/AddNoteCommand.h"

#include <algorithm>

namespace Aestra {
namespace Audio {

AddNoteCommand::AddNoteCommand(PatternManager& patternManager, PatternID patternId, const MidiNote& note)
    : m_patternManager(patternManager), m_patternId(patternId), m_note(note) {}

void AddNoteCommand::execute() {
    if (m_executed)
        return;

    m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
        if (pattern.isMidi()) {
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;
            notes.push_back(m_note);
        }
    });

    m_executed = true;
}

void AddNoteCommand::undo() {
    if (!m_executed)
        return;

    m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
        if (pattern.isMidi()) {
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                [this](const MidiNote& n) {
                    return n.pitch == m_note.pitch &&
                           n.startBeat == m_note.startBeat &&
                           n.durationBeats == m_note.durationBeats &&
                           n.unitId == m_note.unitId;
                }), notes.end());
        }
    });

    m_executed = false;
}

void AddNoteCommand::redo() {
    if (m_executed)
        return;

    execute();
}

} // namespace Audio
} // namespace Aestra
