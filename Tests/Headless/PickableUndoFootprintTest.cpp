// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Pickable undo, slice U1 (SPEC-026, FD-82): every step says what it touched, and Aestra can tell which later
// steps depend on an earlier one. The founder's case: resize a track, then add a clip; undoing the clip alone
// keeps the resize.

#include "Commands/PickableUndo.h"

#include <iostream>
#include <memory>
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

// A step that only says what it touched; nothing here runs.
class Step : public ICommand {
public:
    explicit Step(Footprint footprint) : m_footprint(std::move(footprint)) {}
    void execute() override {}
    void undo() override {}
    void redo() override {}
    std::string getName() const override { return "Step"; }
    Footprint footprint() const override { return m_footprint; }

private:
    Footprint m_footprint;
};

// A step from a command that has not said what it touches yet.
class OldStep : public ICommand {
public:
    void execute() override {}
    void undo() override {}
    void redo() override {}
    std::string getName() const override { return "Old"; }
};

std::shared_ptr<ICommand> touching(Touch kind, AestraUUID id, uint32_t field = Footprint::kWhole) {
    Footprint f;
    f.add(kind, id, field);
    return std::make_shared<Step>(f);
}

constexpr uint32_t kHeight = 1;
constexpr uint32_t kMute = 2;

void aCommandThatSaysNothingTouchesEverything() {
    OldStep old;
    check(old.footprint().isEverything(), "a command with no footprint counts as touching everything");
    Footprint clip;
    clip.add(Touch::Clip, AestraUUID(12));
    check(old.footprint().overlaps(clip) && clip.overlaps(old.footprint()), "so it overlaps anything, both ways");
    check(!Footprint().overlaps(clip), "an empty footprint overlaps nothing");
}

void overlapIsBySameThing() {
    Footprint a, b, c;
    a.add(Touch::Clip, AestraUUID(12));
    b.add(Touch::Clip, AestraUUID(13));
    c.add(Touch::Track, AestraUUID(12));
    check(!a.overlaps(b), "two different clips do not overlap");
    check(!a.overlaps(c), "a clip and a track with the same number do not overlap");
    Footprint height, mute, whole;
    height.add(Touch::Track, AestraUUID(3), kHeight);
    mute.add(Touch::Track, AestraUUID(3), kMute);
    whole.add(Touch::Track, AestraUUID(3));
    check(!height.overlaps(mute), "a track's height and its mute are different things");
    check(height.overlaps(whole) && whole.overlaps(mute), "the whole track overlaps any part of it");
    check(height.overlaps(height), "the same part overlaps itself");
}

void theFoundersCase() {
    // 0: resize track 3   1: add clip 12   2: move clip 12   3: mute track 3
    std::vector<std::shared_ptr<ICommand>> history{
        touching(Touch::Track, AestraUUID(3), kHeight), touching(Touch::Clip, AestraUUID(12)),
        touching(Touch::Clip, AestraUUID(12)), touching(Touch::Track, AestraUUID(3), kMute)};
    check(dependantsOf(history, 0).empty(), "undoing the resize alone touches nothing later");
    const auto onClip = dependantsOf(history, 1);
    check(onClip == std::vector<size_t>{2}, "undoing the clip names the later move of that clip, and only it");
    check(dependantsOf(history, 2).empty(), "the move itself has nothing after it on that clip");
    check(dependantsOf(history, 3).empty(), "the last step has nothing after it");
}

void dependantsAreFollowedThrough() {
    // 0: clip 12   1: clip 12 and clip 20 (moved together)   2: clip 20
    Footprint both;
    both.add(Touch::Clip, AestraUUID(12));
    both.add(Touch::Clip, AestraUUID(20));
    std::vector<std::shared_ptr<ICommand>> history{touching(Touch::Clip, AestraUUID(12)), std::make_shared<Step>(both),
                                                   touching(Touch::Clip, AestraUUID(20))};
    check(dependantsOf(history, 0) == std::vector<size_t>{1, 2},
          "a step that depends on a dependant is named too (clip 20 moved after moving both)");
}

void oldCommandsAreNeverPickedAround() {
    std::vector<std::shared_ptr<ICommand>> history{touching(Touch::Clip, AestraUUID(12)), std::make_shared<OldStep>(),
                                                   touching(Touch::Clip, AestraUUID(40))};
    check(dependantsOf(history, 0) == std::vector<size_t>{1, 2},
          "a later step that says nothing counts as depending, and so does everything after it");
    check(dependantsOf(history, 1) == std::vector<size_t>{2}, "undoing a step that says nothing takes all later ones");
    check(dependantsOf(history, 5).empty(), "a step that does not exist has no dependants");
}
} // namespace

int main() {
    aCommandThatSaysNothingTouchesEverything();
    overlapIsBySameThing();
    theFoundersCase();
    dependantsAreFollowedThrough();
    oldCommandsAreNeverPickedAround();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PickableUndoFootprintTest passed\n";
    return 0;
}
