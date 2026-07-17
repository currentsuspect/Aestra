// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to transpose note pitches in a pattern (undoable)
 *
 * The factory rejects transpositions that would push any affected note
 * outside MIDI range, so execute never clamps — undo is a clean shift back.
 * unitId 0 transposes every note, otherwise only that unit's notes.
 */
class TransposePatternCommand : public ICommand {
public:
    TransposePatternCommand(PatternManager& patternManager, PatternID patternId, int semitones,
                            uint64_t unitId)
        : m_patternManager(patternManager), m_patternId(patternId), m_semitones(semitones),
          m_unitId(unitId) {}

    void execute() override {
        if (m_executed) return;
        applyShift(m_semitones);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        applyShift(-m_semitones);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Transpose Pattern"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    void applyShift(int semitones) {
        m_patternManager.applyPatch(m_patternId, [this, semitones](PatternSource& pattern) {
            if (!pattern.isMidi()) return;
            for (MidiNote& note : std::get<MidiPayload>(pattern.payload).notes) {
                if (m_unitId != 0 && note.unitId != m_unitId) continue;
                note.pitch += semitones;
            }
        });
    }

    PatternManager& m_patternManager;
    PatternID m_patternId;
    int m_semitones;
    uint64_t m_unitId; // 0 = all units

    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
