// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Pickable undo (SPEC-026, FD-82): which later steps depend on an earlier one, read from what each touched.
#pragma once

#include "ICommand.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

// The later steps that depend on step @p index of @p history (oldest first): those that touched what it
// touched, followed through, so a step depending on a dependant is named too. In history order.
inline std::vector<size_t> dependantsOf(const std::vector<std::shared_ptr<ICommand>>& history, size_t index) {
    std::vector<size_t> dependants;
    if (index >= history.size()) return dependants;
    // What has to come undone with it: its own footprint, grown by each dependant found.
    std::vector<Footprint> reached{history[index] ? history[index]->footprint() : Footprint::everything()};
    for (size_t j = index + 1; j < history.size(); ++j) {
        const Footprint later = history[j] ? history[j]->footprint() : Footprint::everything();
        for (const Footprint& f : reached) {
            if (later.overlaps(f)) {
                dependants.push_back(j);
                reached.push_back(later);
                break;
            }
        }
    }
    return dependants;
}

enum class PickResult {
    Done,          // the step (and any dependants asked for) is undone; history gained one step saying so
    HasDependants, // later steps depend on it; nothing changed; they are listed
    NothingThere,  // no such step
    Failed,        // an undo failed; the project and history are as they were
    Busy,          // a transaction is open
};

struct PickedUndo {
    PickResult result = PickResult::NothingThere;
    std::vector<size_t> dependants; // indices into the undo stack (0 = oldest), in history order
};

// The step a picked undo leaves in history: it holds the steps it took out, oldest first, already undone.
// Undoing it brings them back (oldest first); redoing it takes them out again (newest first).
class PickedSteps : public ICommand {
public:
    explicit PickedSteps(std::vector<std::shared_ptr<ICommand>> steps) : m_steps(std::move(steps)) {}
    void execute() override {} // made after the steps were undone
    void undo() override {
        for (auto& step : m_steps) step->redo();
    }
    void redo() override {
        for (auto it = m_steps.rbegin(); it != m_steps.rend(); ++it) (*it)->undo();
    }
    std::string getName() const override {
        std::string name = "Undid: " + (m_steps.empty() ? std::string() : m_steps.front()->getName());
        if (m_steps.size() > 1) name += " and " + std::to_string(m_steps.size() - 1) + " more";
        return name;
    }
    size_t getSizeInBytes() const override {
        size_t bytes = sizeof(*this);
        for (const auto& step : m_steps) bytes += step->getSizeInBytes();
        return bytes;
    }
    Footprint footprint() const override {
        Footprint f;
        for (const auto& step : m_steps) f.merge(step->footprint());
        return f;
    }
    const std::vector<std::shared_ptr<ICommand>>& steps() const { return m_steps; }

private:
    std::vector<std::shared_ptr<ICommand>> m_steps;
};

} // namespace Audio
} // namespace Aestra
