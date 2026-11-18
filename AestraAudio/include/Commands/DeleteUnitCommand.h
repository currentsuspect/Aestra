// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Delete an Arsenal unit as one reversible state transition.
 *
 * The command restores the exact unit identity/order, all notes owned by the unit, and an unplaced
 * default pattern. A default pattern already used on the timeline is retained because it has an
 * independent arrangement owner and must not be orphaned by unit deletion.
 */
class DeleteUnitCommand final : public ICommand {
public:
    DeleteUnitCommand(TrackManager& manager, UnitID unitId, size_t orderIndex)
        : m_manager(manager), m_unitId(unitId), m_orderIndex(orderIndex) {
        const auto* unit = m_manager.getUnitManager().getUnit(unitId);
        if (!unit) return;
        m_unit = *unit;

        auto& patterns = m_manager.getPatternManager();
        for (const auto& pattern : patterns.getAllPatterns()) {
            if (!pattern || !pattern->isMidi()) continue;
            auto notes = std::get<MidiPayload>(pattern->payload).notes;
            auto it = std::remove_if(notes.begin(), notes.end(), [unitId](const MidiNote& n) { return n.unitId == unitId; });
            if (it != notes.end()) {
                notes.erase(it, notes.end());
                m_notes.emplace_back(pattern->id, std::move(notes));
            }
        }
    }

    void execute() override {
        if (m_executed || !m_unit.has_value()) return;
        auto& units = m_manager.getUnitManager();
        if (!units.getUnit(m_unitId)) return;

        auto& patterns = m_manager.getPatternManager();
        if (m_unit->defaultPatternId.isValid() &&
            !m_manager.getPlaylistModel().isPatternUsed(m_unit->defaultPatternId)) {
            m_detachedDefaultPattern = patterns.detachPattern(m_unit->defaultPatternId);
        }

        removeOwnedNotes();
        if (!units.removeUnit(m_unitId)) {
            if (m_detachedDefaultPattern) patterns.reinsertPattern(m_detachedDefaultPattern);
            return;
        }
        m_manager.markModified();
        m_executed = true;
    }

    void undo() override {
        if (!m_executed || !m_unit.has_value()) return;
        auto& units = m_manager.getUnitManager();
        if (!units.restoreUnit(*m_unit, m_orderIndex)) return;

        auto& patterns = m_manager.getPatternManager();
        if (m_detachedDefaultPattern) {
            if (!patterns.reinsertPattern(m_detachedDefaultPattern)) return;
        }
        restoreOwnedNotes();
        m_manager.markModified();
        m_executed = false;
    }

    void redo() override { execute(); }

    std::string getName() const override { return "Delete Arsenal Unit"; }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_notes.size() * sizeof(NoteSnapshot); }
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return m_executed; }

private:
    struct NoteSnapshot {
        PatternID patternId;
        std::vector<MidiNote> notes;
    };

    void removeOwnedNotes() {
        auto& patterns = m_manager.getPatternManager();
        for (const auto& snapshot : m_notes) {
            if (auto* pattern = patterns.getPattern(snapshot.patternId)) {
                auto& notes = std::get<MidiPayload>(pattern->payload).notes;
                notes.erase(std::remove_if(notes.begin(), notes.end(), [this](const MidiNote& n) {
                    return n.unitId == m_unitId;
                }), notes.end());
            }
        }
    }

    void restoreOwnedNotes() {
        auto& patterns = m_manager.getPatternManager();
        for (const auto& snapshot : m_notes) {
            if (auto* pattern = patterns.getPattern(snapshot.patternId)) {
                auto& notes = std::get<MidiPayload>(pattern->payload).notes;
                notes.erase(std::remove_if(notes.begin(), notes.end(), [this](const MidiNote& n) {
                    return n.unitId == m_unitId;
                }), notes.end());
                notes.insert(notes.end(), snapshot.notes.begin(), snapshot.notes.end());
            }
        }
    }

    TrackManager& m_manager;
    UnitID m_unitId{0};
    size_t m_orderIndex{0};
    std::optional<UnitInfo> m_unit;
    std::vector<NoteSnapshot> m_notes;
    std::unique_ptr<PatternSource> m_detachedDefaultPattern;
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
