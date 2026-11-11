// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/UpdateNoteCommand.h"

namespace Aestra {
namespace Audio {

UpdateNoteCommand::UpdateNoteCommand(PatternManager& patternManager, PatternID patternId,
                                     const MidiNote& originalNote, float newVelocity, float newPan)
    : m_patternManager(patternManager), m_patternId(patternId),
      m_originalNote(originalNote), m_newVelocity(newVelocity), m_newPan(newPan), m_newChance(originalNote.chance),
      m_newRatchet(originalNote.ratchet) {}

UpdateNoteCommand::UpdateNoteCommand(PatternManager& patternManager, PatternID patternId, const MidiNote& originalNote,
                                     float newVelocity, float newPan, float newChance, uint8_t newRatchet)
    : m_patternManager(patternManager), m_patternId(patternId), m_originalNote(originalNote),
      m_newVelocity(newVelocity), m_newPan(newPan), m_newChance(newChance), m_newRatchet(newRatchet) {}

void UpdateNoteCommand::execute() {
    if (m_executed)
        return;

    m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
        if (pattern.isMidi()) {
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;
            for (auto& note : notes) {
                if (note.pitch == m_originalNote.pitch &&
                    note.startBeat == m_originalNote.startBeat &&
                    note.unitId == m_originalNote.unitId) {
                    note.velocity = m_newVelocity;
                    note.pan = m_newPan;
                    note.chance = m_newChance;
                    note.ratchet = m_newRatchet;
                    break;
                }
            }
        }
    });

    m_executed = true;
}

void UpdateNoteCommand::undo() {
    if (!m_executed)
        return;

    m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
        if (pattern.isMidi()) {
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;
            for (auto& note : notes) {
                if (note.pitch == m_originalNote.pitch &&
                    note.startBeat == m_originalNote.startBeat &&
                    note.unitId == m_originalNote.unitId) {
                    note.velocity = m_originalNote.velocity;
                    note.pan = m_originalNote.pan;
                    note.chance = m_originalNote.chance;
                    note.ratchet = m_originalNote.ratchet;
                    break;
                }
            }
        }
    });

    m_executed = false;
}

void UpdateNoteCommand::redo() {
    if (m_executed)
        return;

    execute();
}

} // namespace Audio
} // namespace Aestra
