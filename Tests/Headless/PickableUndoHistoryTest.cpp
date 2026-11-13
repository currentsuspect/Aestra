// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Pickable undo, slice U2 (SPEC-026, FD-82): undo one past step and keep everything after it. The picked undo
// is itself a step, so nothing is erased: undoing it brings the step back.

#include "Commands/CommandHistory.h"
#include "Commands/PickableUndo.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "failed: " << message << '\n';
    }
}

constexpr uint32_t kHeight = 1;
constexpr uint32_t kMute = 2;

// A tiny project: one track's height and mute, and where clip 12 is (-1 = not there).
struct World {
    int height = 100;
    bool muted = false;
    int clipAt = -1;
    std::vector<std::string> undone; // the order undo() ran in
};

class Change : public ICommand {
public:
    Change(World& w, std::string name, int World::*field, int to, Footprint fp)
        : m_w(w), m_name(std::move(name)), m_field(field), m_to(to), m_fp(std::move(fp)) {}
    void execute() override { m_from = m_w.*m_field; m_w.*m_field = m_to; }
    void undo() override {
        if (m_throwOnUndo) throw std::runtime_error("disk gone");
        m_w.*m_field = m_from;
        m_w.undone.push_back(m_name);
    }
    void redo() override { m_w.*m_field = m_to; }
    std::string getName() const override { return m_name; }
    Footprint footprint() const override { return m_fp; }
    bool m_throwOnUndo = false;

private:
    World& m_w;
    std::string m_name;
    int World::*m_field;
    int m_to;
    int m_from = 0;
    Footprint m_fp;
};

class Mute : public ICommand {
public:
    explicit Mute(World& w) : m_w(w) {}
    void execute() override { m_w.muted = true; }
    void undo() override { m_w.muted = false; m_w.undone.push_back("Mute"); }
    void redo() override { m_w.muted = true; }
    std::string getName() const override { return "Mute"; }
    Footprint footprint() const override {
        Footprint f;
        f.add(Touch::Track, AestraUUID(3), kMute);
        return f;
    }

private:
    World& m_w;
};

class OldStep : public ICommand { // says nothing about what it touched
public:
    void execute() override {}
    void undo() override {}
    void redo() override {}
    std::string getName() const override { return "Old"; }
};

Footprint touching(Touch kind, uint64_t id, uint32_t part = Footprint::kWhole) {
    Footprint f;
    f.add(kind, AestraUUID(id), part);
    return f;
}

std::shared_ptr<Change> resize(World& w) {
    return std::make_shared<Change>(w, "Resize track", &World::height, 140, touching(Touch::Track, 3, kHeight));
}
std::shared_ptr<Change> addClip(World& w) {
    return std::make_shared<Change>(w, "Add clip", &World::clipAt, 0, touching(Touch::Clip, 12));
}
std::shared_ptr<Change> moveClip(World& w) {
    return std::make_shared<Change>(w, "Move clip", &World::clipAt, 4, touching(Touch::Clip, 12));
}

void theFoundersCase() {
    World w;
    CommandHistory h;
    h.pushAndExecute(resize(w));
    h.pushAndExecute(addClip(w));
    h.pushAndExecute(std::make_shared<Mute>(w));
    check(w.height == 140 && w.clipAt == 0 && w.muted, "three steps done");

    const auto picked = h.undoJust(1);
    check(picked.result == PickResult::Done, "the clip can be undone alone");
    check(w.clipAt == -1, "the clip is gone");
    check(w.height == 140 && w.muted, "the resize and the mute stay");
    const auto& stack = h.getUndoStack();
    check(stack.size() == 3 && stack.back()->getName() == "Undid: Add clip",
          "history gains a step saying so, in place of the clip step (" + std::to_string(stack.size()) + ")");

    check(h.undo() && w.clipAt == 0 && w.height == 140 && w.muted, "undoing that step brings the clip back");
    check(h.redo() && w.clipAt == -1 && w.height == 140, "and redo takes it out again");
}

void dependantsAreNamedAndNothingChanges() {
    World w;
    CommandHistory h;
    h.pushAndExecute(resize(w));
    h.pushAndExecute(addClip(w));
    h.pushAndExecute(moveClip(w));
    const auto picked = h.undoJust(1);
    check(picked.result == PickResult::HasDependants && picked.dependants == std::vector<size_t>{2},
          "a later move of the same clip is named");
    check(w.clipAt == 4 && h.getUndoStack().size() == 3, "and nothing changed");

    const auto both = h.undoJust(1, true);
    check(both.result == PickResult::Done && w.clipAt == -1 && w.height == 140, "with them: both go, the resize stays");
    check(w.undone == std::vector<std::string>{"Move clip", "Add clip"}, "undone newest first");
    check(h.getUndoStack().size() == 2 && h.getUndoStack().back()->getName() == "Undid: Add clip and 1 more",
          "one step records both");
    check(h.undo() && w.clipAt == 4, "undoing it puts both back, in order");
}

void stepsThatSayNothingAreNeverUndoneAround() {
    World w;
    CommandHistory h;
    h.pushAndExecute(addClip(w));
    h.pushAndExecute(std::make_shared<OldStep>());
    const auto picked = h.undoJust(0);
    check(picked.result == PickResult::HasDependants && picked.dependants == std::vector<size_t>{1},
          "a later step that says nothing counts as depending");
    check(h.undoJust(7).result == PickResult::NothingThere, "a step that is not there");
}

void aFailedUndoLeavesEverythingAsItWas() {
    World w;
    CommandHistory h;
    h.pushAndExecute(addClip(w));
    auto move = moveClip(w);
    h.pushAndExecute(move);
    move->m_throwOnUndo = true;
    const auto picked = h.undoJust(0, true);
    check(picked.result == PickResult::Failed, "the failure is reported");
    check(w.clipAt == 4 && h.getUndoStack().size() == 2 && h.getUndoStack()[1] == move,
          "the project and history are as they were");
}

void thePickedStepCarriesWhatItTouched() {
    World w;
    CommandHistory h;
    h.pushAndExecute(resize(w));
    h.pushAndExecute(addClip(w));
    h.undoJust(1);
    // Moving clip 12 back in now would depend on the "Undid" step, not slip under it.
    h.pushAndExecute(addClip(w));
    check(dependantsOf(h.getUndoStack(), 1) == std::vector<size_t>{2},
          "a later step on the same clip depends on the picked undo");
}
} // namespace

int main() {
    theFoundersCase();
    dependantsAreNamedAndNothingChanges();
    stepsThatSayNothingAreNeverUndoneAround();
    aFailedUndoLeavesEverythingAsItWas();
    thePickedStepCarriesWhatItTouched();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PickableUndoHistoryTest passed\n";
    return 0;
}
