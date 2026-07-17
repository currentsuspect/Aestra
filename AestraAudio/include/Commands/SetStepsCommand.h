// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to write one drum row of a pattern from a step string (undoable)
 *
 * Row semantics, not append semantics: the step string defines the entire
 * row for this (unit, pitch) — every existing note of that identity is
 * replaced, so re-issuing the verb rewrites the groove (and a shorter string
 * cannot leave a stale tail behind). Notes of other pitches/units are
 * untouched.
 *
 * If the row extends past the pattern length, the pattern grows to fit
 * (restored on undo).
 */
class SetStepsCommand : public ICommand {
public:
    SetStepsCommand(PatternManager& patternManager, PatternID patternId,
                    std::vector<MidiNote> rowNotes, uint64_t unitId, int pitch,
                    double rowLengthBeats)
        : m_patternManager(patternManager), m_patternId(patternId),
          m_rowNotes(std::move(rowNotes)), m_unitId(unitId), m_pitch(pitch),
          m_rowLengthBeats(rowLengthBeats) {}

    void execute() override {
        if (m_executed) return;
        m_replacedNotes.clear();

        m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
            if (!pattern.isMidi()) return;
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;

            // Capture and remove the entire existing row for this identity.
            for (const MidiNote& note : notes) {
                if (note.unitId == m_unitId && note.pitch == m_pitch) {
                    m_replacedNotes.push_back(note);
                }
            }
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                                       [this](const MidiNote& n) {
                                           return n.unitId == m_unitId && n.pitch == m_pitch;
                                       }),
                        notes.end());

            for (const MidiNote& note : m_rowNotes) {
                notes.push_back(note);
            }

            m_previousLengthBeats = pattern.lengthBeats;
            if (m_rowLengthBeats > pattern.lengthBeats) {
                pattern.lengthBeats = m_rowLengthBeats;
            }
        });

        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;

        m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
            if (!pattern.isMidi()) return;
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;

            notes.erase(std::remove_if(notes.begin(), notes.end(),
                                       [this](const MidiNote& n) {
                                           return n.unitId == m_unitId && n.pitch == m_pitch;
                                       }),
                        notes.end());
            for (const MidiNote& note : m_replacedNotes) {
                notes.push_back(note);
            }
            pattern.lengthBeats = m_previousLengthBeats;
        });

        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Set Steps"; }
    size_t getSizeInBytes() const override {
        return sizeof(*this) + (m_rowNotes.size() + m_replacedNotes.size()) * sizeof(MidiNote);
    }
    bool changesProjectState() const override { return true; }

private:
    PatternManager& m_patternManager;
    PatternID m_patternId;
    std::vector<MidiNote> m_rowNotes;
    uint64_t m_unitId;
    int m_pitch;
    double m_rowLengthBeats;

    // Recorded by execute() for undo
    std::vector<MidiNote> m_replacedNotes;
    double m_previousLengthBeats = 0.0;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
