// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Command to quantize note starts in a pattern toward a beat grid (undoable)
 *
 * strength 1.0 snaps starts exactly onto the nearest grid multiple; partial
 * strengths move them proportionally toward it. unitId 0 quantizes every
 * note, otherwise only that unit's notes. Undo restores the exact original
 * starts (a snapped grid position is not invertible arithmetically).
 */
class QuantizePatternCommand : public ICommand {
public:
    QuantizePatternCommand(PatternManager& patternManager, PatternID patternId, double gridBeats,
                           double strength, uint64_t unitId)
        : m_patternManager(patternManager), m_patternId(patternId), m_gridBeats(gridBeats),
          m_strength(strength), m_unitId(unitId) {}

    void execute() override {
        if (m_executed) return;
        m_originalStarts.clear();

        m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
            if (!pattern.isMidi()) return;
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;
            for (size_t i = 0; i < notes.size(); ++i) {
                MidiNote& note = notes[i];
                if (m_unitId != 0 && note.unitId != m_unitId) continue;
                const double snapped = std::round(note.startBeat / m_gridBeats) * m_gridBeats;
                const double moved = note.startBeat + (snapped - note.startBeat) * m_strength;
                if (moved != note.startBeat) {
                    m_originalStarts.emplace_back(i, note.startBeat);
                    note.startBeat = moved;
                }
            }
        });

        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;

        m_patternManager.applyPatch(m_patternId, [this](PatternSource& pattern) {
            if (!pattern.isMidi()) return;
            auto& notes = std::get<MidiPayload>(pattern.payload).notes;
            for (const auto& [index, start] : m_originalStarts) {
                if (index < notes.size()) {
                    notes[index].startBeat = start;
                }
            }
        });

        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        execute();
    }

    std::string getName() const override { return "Quantize Pattern"; }
    size_t getSizeInBytes() const override {
        return sizeof(*this) + m_originalStarts.size() * sizeof(std::pair<size_t, double>);
    }
    bool changesProjectState() const override { return true; }

private:
    PatternManager& m_patternManager;
    PatternID m_patternId;
    double m_gridBeats;
    double m_strength;
    uint64_t m_unitId; // 0 = all units

    // Recorded by execute() for undo: (note index, original startBeat)
    std::vector<std::pair<size_t, double>> m_originalStarts;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
