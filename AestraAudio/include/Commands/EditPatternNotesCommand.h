// © 2026 Aestra Studios — All Rights Reserved.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Undoable whole-vector replacement of a MIDI pattern's notes
 *
 * One command per editing gesture (place/paint, erase, step velocity,
 * move/delete/duplicate selection, paste). The editor mutates the pattern
 * directly, snapshots the notes before and after, and pushes this command as
 * already executed — so a whole drag coalesces into a single undo step while
 * still routing through the shared CommandHistory (#822).
 */
class EditPatternNotesCommand final : public ICommand {
public:
    EditPatternNotesCommand(PatternManager& patternManager, PatternID patternId,
                            std::vector<MidiNote> notesBefore, std::vector<MidiNote> notesAfter,
                            std::string name = "Edit Notes")
        : m_patternManager(patternManager), m_patternId(patternId),
          m_notesBefore(std::move(notesBefore)), m_notesAfter(std::move(notesAfter)),
          m_name(std::move(name)), m_executed(true) {}

    void execute() override {
        if (m_executed)
            return;
        apply(m_notesAfter);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;
        apply(m_notesBefore);
        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;
        apply(m_notesAfter);
        m_executed = true;
    }

    std::string getName() const override { return m_name; }
    bool changesProjectState() const override { return true; }

    size_t getSizeInBytes() const override {
        // Real footprint: CommandHistory's LRU budget counts these honestly.
        return sizeof(*this) + m_notesBefore.capacity() * sizeof(MidiNote) +
               m_notesAfter.capacity() * sizeof(MidiNote) + m_name.capacity();
    }

private:
    void apply(const std::vector<MidiNote>& notes) {
        m_patternManager.applyPatch(m_patternId, [&notes](PatternSource& pattern) {
            if (!pattern.isMidi())
                return;
            std::get<MidiPayload>(pattern.payload).notes = notes;
        });
    }

    PatternManager& m_patternManager;
    PatternID m_patternId;
    std::vector<MidiNote> m_notesBefore;
    std::vector<MidiNote> m_notesAfter;
    std::string m_name;
    bool m_executed;
};

} // namespace Audio
} // namespace Aestra
