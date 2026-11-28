// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// File > Import MIDI File: every pattern the file makes is created, and undone, as ONE step.
// Redo puts the same patterns (same ids) back, so anything that remembered an id still finds it.

#include "Commands/ICommand.h"
#include "Midi/MidiPatternBridge.h"
#include "Models/PatternManager.h"

#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

class ImportMidiPatternsCommand final : public ICommand {
public:
    ImportMidiPatternsCommand(PatternManager& patterns, std::vector<Midi::ImportedPattern> plan)
        : m_patterns(patterns), m_plan(std::move(plan)) {}

    void execute() override {
        if (m_executed) return;
        if (m_detached.empty()) {
            for (const auto& p : m_plan) {
                MidiPayload payload;
                payload.notes = p.notes;
                m_ids.push_back(m_patterns.createMidiPattern(p.name, p.lengthBeats, payload));
            }
            m_executed = !m_ids.empty();
        } else {
            for (auto& d : m_detached) m_patterns.reinsertPattern(d);
            m_detached.clear();
            m_executed = true;
        }
    }
    void undo() override {
        if (!m_executed) return;
        for (auto it = m_ids.rbegin(); it != m_ids.rend(); ++it) m_detached.insert(m_detached.begin(), m_patterns.detachPattern(*it));
        m_executed = false;
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Import MIDI File"; }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_plan.size() * 64; }
    bool changesProjectState() const override { return true; }
    const std::vector<PatternID>& createdIds() const { return m_ids; }

private:
    PatternManager& m_patterns;
    std::vector<Midi::ImportedPattern> m_plan;
    std::vector<PatternID> m_ids;
    std::vector<std::unique_ptr<PatternSource>> m_detached;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
