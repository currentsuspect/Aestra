// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Replace a MIDI pattern's notes, and optionally its length, as one undo step (V82-M3)
 *
 * The Muse piano-roll verbs (add_notes, delete_notes, duplicate_notes) compute the new note list up front and hand it
 * here. Unlike EditPatternNotesCommand, which the editor pushes already applied, this one applies on execute(), the
 * way CommandHistory runs a verb. The previous notes and length are captured at execute, so undo restores exactly what
 * was there. A duplicate that runs past the pattern's end grows the pattern in the same step.
 */
class ReplacePatternNotesCommand final : public ICommand {
public:
    /// @param newLengthBeats the pattern's new length, or 0 to leave it alone.
    ReplacePatternNotesCommand(PatternManager& patternManager, PatternID patternId, std::vector<MidiNote> notesAfter,
                               double newLengthBeats, std::string name)
        : m_patternManager(patternManager), m_patternId(patternId), m_notesAfter(std::move(notesAfter)),
          m_newLengthBeats(newLengthBeats), m_name(std::move(name)) {}

    void execute() override {
        if (m_executed)
            return;
        m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
            if (!pattern.isMidi())
                return;
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;
            m_notesBefore = notes;
            m_lengthBefore = pattern.lengthBeats;
            notes = m_notesAfter;
            if (m_newLengthBeats > 0.0)
                pattern.lengthBeats = m_newLengthBeats;
        });
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;
        m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
            if (!pattern.isMidi())
                return;
            std::get<MidiPayload>(pattern.payload).notes = m_notesBefore;
            pattern.lengthBeats = m_lengthBefore;
        });
        m_executed = false;
    }

    void redo() override { execute(); }

    std::string getName() const override { return m_name; }
    bool changesProjectState() const override { return true; }

    size_t getSizeInBytes() const override {
        return sizeof(*this) + m_notesBefore.capacity() * sizeof(MidiNote) +
               m_notesAfter.capacity() * sizeof(MidiNote) + m_name.capacity();
    }

private:
    PatternManager& m_patternManager;
    PatternID m_patternId;
    std::vector<MidiNote> m_notesAfter;
    std::vector<MidiNote> m_notesBefore;
    double m_newLengthBeats;
    double m_lengthBefore = 0.0;
    std::string m_name;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
