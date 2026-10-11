// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Owner report (2026-10-11): "i can't copy all tracks and do anything with them, select all is just a prosthetic
// for copying and can't do moves. we need to even incorporate moving with arrow keys."
//
// A selection of several clips now behaves as ONE thing: it copies and pastes as a block (keeping the layout and
// the lanes), it moves as a block when one of its clips is dragged, and the arrow keys nudge it; every
// operation is one undo step and clamped as a block (no clip crosses beat 0 or leaves the first/last lane).
//
// Drives the real TrackManagerUI with real lane components, exactly as ClipContextMenuReachabilityTest does:
// events go through NUIComponent::dispatchMouseEvent and the manager's own onKeyEvent.

#include "../Support/NullRenderer.h"
#include "MixerChannel.h"
#include "NUIComponent.h"
#include "PatternManager.h"
#include "PlaylistModel.h"
#include "TrackManager.h"
#include "TrackManagerUI.h"
#include "TrackUIComponent.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
bool near(double a, double b) { return std::abs(a - b) < 1.0e-6; }

AestraUI::NUIKeyEvent key(AestraUI::NUIKeyCode code, AestraUI::NUIModifiers mods = AestraUI::NUIModifiers::None) {
    AestraUI::NUIKeyEvent e;
    e.keyCode = code;
    e.modifiers = mods;
    e.pressed = true;
    return e;
}

AestraUI::NUIMouseEvent mouse(AestraUI::NUIMouseEventType type, const AestraUI::NUIPoint& p, bool down, bool up,
                              AestraUI::NUIMouseButton button) {
    AestraUI::NUIMouseEvent e;
    e.type = type;
    e.position = p;
    e.button = button;
    e.pressed = down;
    e.released = up;
    return e;
}

std::shared_ptr<TrackUIComponent> findLaneComponent(const AestraUI::NUIComponent& node, const PlaylistLaneID& id) {
    for (const auto& child : node.getChildren()) {
        if (auto lane = std::dynamic_pointer_cast<TrackUIComponent>(child)) {
            if (lane->getLaneId() == id) return lane;
        }
        if (child) {
            if (auto nested = findLaneComponent(*child, id)) return nested;
        }
    }
    return nullptr;
}

// Three lanes; A and B on lane 0 (beats 4 and 12), C on lane 1 (beat 8). Each 4 beats long.
struct Fixture {
    std::shared_ptr<TrackManager> tm;
    std::shared_ptr<AestraUI::NUIComponent> root;
    std::shared_ptr<TrackManagerUI> ui;
    PlaylistLaneID lane[3];
    ClipInstanceID a, b, c;

    Fixture() {
        tm = std::make_shared<TrackManager>();
        auto& playlist = tm->getPlaylistModel();
        const PatternID pattern = tm->getPatternManager().createMidiPattern("P", 4.0, MidiPayload{});
        for (int i = 0; i < 3; ++i) lane[i] = playlist.createLane("Lane " + std::to_string(i));
        a = playlist.addClipFromPattern(lane[0], pattern, 4.0, 4.0);
        b = playlist.addClipFromPattern(lane[0], pattern, 12.0, 4.0);
        c = playlist.addClipFromPattern(lane[1], pattern, 8.0, 4.0);
        ui = std::make_shared<TrackManagerUI>(tm);
        ui->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 600.0f));
        root = std::make_shared<AestraUI::NUIComponent>();
        root->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 600.0f));
        root->addChild(ui);
        ui->refreshTracks();
        paint();
    }
    void paint() {
        Aestra::Testing::NullRenderer renderer;
        for (int i = 0; i < 3; ++i)
            if (auto l = findLaneComponent(*ui, lane[i])) l->renderStatic(renderer);
    }
    void selectAll() { ui->onKeyEvent(key(AestraUI::NUIKeyCode::A, AestraUI::NUIModifiers::Ctrl)); }
    double start(const ClipInstanceID& id) const { const auto* x = tm->getPlaylistModel().getClip(id); return x ? x->startBeat : -1.0; }
    int laneOf(const ClipInstanceID& id) const {
        const auto l = tm->getPlaylistModel().findClipLane(id);
        for (int i = 0; i < 3; ++i) if (lane[i] == l) return i;
        return -1;
    }
    size_t clipCount() const {
        size_t n = 0;
        auto& p = tm->getPlaylistModel();
        for (size_t i = 0; i < p.getLaneCount(); ++i)
            if (const auto* l = p.getLane(p.getLaneId(i))) n += l->clips.size();
        return n;
    }
    bool atOriginal() const {
        return near(start(a), 4.0) && near(start(b), 12.0) && near(start(c), 8.0) && laneOf(a) == 0 && laneOf(b) == 0 && laneOf(c) == 1;
    }
};

void testArrowNudge() {
    std::cout << "[arrow keys]\n";
    Fixture f;
    f.selectAll();
    check(f.ui->clipSelection().size() == 3, "select all selects the three clips");

    const bool consumed = f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Right));
    check(consumed && near(f.start(f.a), 8.0) && near(f.start(f.b), 16.0) && near(f.start(f.c), 12.0),
          "Right moves every selected clip by the grid step (one bar at Bar snap), layout intact");
    check(f.laneOf(f.a) == 0 && f.laneOf(f.b) == 0 && f.laneOf(f.c) == 1, "and no clip changes lane");
    check(f.tm->getCommandHistory().undo() && near(f.start(f.a), 4.0) && near(f.start(f.b), 12.0) && near(f.start(f.c), 8.0),
          "the whole nudge is ONE undo step");

    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Right, AestraUI::NUIModifiers::Shift));
    check(near(f.start(f.a), 4.25) && near(f.start(f.b), 12.25) && near(f.start(f.c), 8.25), "Shift+Right is a fine nudge (a sixteenth)");
    f.tm->getCommandHistory().undo();

    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Left));
    check(near(f.start(f.a), 0.0) && near(f.start(f.b), 8.0) && near(f.start(f.c), 4.0), "Left moves the block back one bar");
    const bool atEdge = f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Left));
    check(atEdge && near(f.start(f.a), 0.0) && near(f.start(f.b), 8.0), "at beat 0 the block stops as a whole: nothing crosses 0, layout intact");
    while (f.tm->getCommandHistory().undo()) {}

    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Down));
    check(f.laneOf(f.a) == 1 && f.laneOf(f.b) == 1 && f.laneOf(f.c) == 2, "Down moves every clip one lane, together");
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Down));
    check(f.laneOf(f.a) == 1 && f.laneOf(f.c) == 2, "at the last lane the block stops as a whole");
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Up));
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Up));
    check(f.laneOf(f.a) == 0 && f.laneOf(f.c) == 1, "Up moves it back and stops at the first lane");
    while (f.tm->getCommandHistory().undo()) {}
    check(f.atOriginal(), "undoing everything returns the clips to where they started");
}

void testSingleSelectionStillMovesAlone() {
    std::cout << "[single selection]\n";
    Fixture f;
    f.ui->selectClips({f.a}, TrackSelectionIntent::Replace);
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Right));
    check(near(f.start(f.a), 8.0) && near(f.start(f.b), 12.0) && near(f.start(f.c), 8.0), "with one clip selected only that clip nudges");
}

void testCopyPasteBlock() {
    std::cout << "[copy / paste]\n";
    Fixture f;
    f.selectAll();
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::C, AestraUI::NUIModifiers::Ctrl));
    f.tm->setPosition(10.0); // 20 beats at 120 bpm
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::V, AestraUI::NUIModifiers::Ctrl));
    check(f.clipCount() == 6, "copying the whole selection and pasting brings all three clips back, not one");

    std::vector<std::pair<double, int>> added; // (start, lane)
    auto& p = f.tm->getPlaylistModel();
    for (int i = 0; i < 3; ++i)
        if (const auto* l = p.getLane(f.lane[i]))
            for (const auto& cl : l->clips)
                if (cl.id != f.a && cl.id != f.b && cl.id != f.c) added.push_back({cl.startBeat, i});
    std::sort(added.begin(), added.end());
    check(added.size() == 3 && near(added[0].first, 20.0) && near(added[1].first, 24.0) && near(added[2].first, 28.0),
          "the block lands with its first clip at the playhead and the gaps between clips kept (20, 24, 28)");
    check(added.size() == 3 && added[0].second == 0 && added[1].second == 1 && added[2].second == 0,
          "each pasted clip is on the lane it was copied from");
    check(f.ui->clipSelection().size() == 3 && !f.ui->clipSelection().contains(f.a), "the pasted block becomes the selection");
    f.tm->getCommandHistory().undo();
    check(f.clipCount() == 3, "a block paste is ONE undo step");
}

void testCutPasteMovesTheBlock() {
    std::cout << "[cut / paste]\n";
    Fixture f;
    f.selectAll();
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::X, AestraUI::NUIModifiers::Ctrl));
    check(f.clipCount() == 0, "cut removes every selected clip");
    f.tm->setPosition(0.0);
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::V, AestraUI::NUIModifiers::Ctrl));
    check(f.clipCount() == 3, "paste brings the whole block back");
}

void testGroupDrag() {
    std::cout << "[group drag]\n";
    Fixture f;
    f.selectAll();
    auto laneA = findLaneComponent(*f.ui, f.lane[0]);
    const auto& boundsMap = laneA->getAllClipBounds();
    const auto it = boundsMap.find(f.a);
    check(it != boundsMap.end(), "the paint pass published the clip's bounds");
    if (it == boundsMap.end()) return;
    const auto r = it->second;
    const float pxPerBeat = r.width / 4.0f;
    const AestraUI::NUIPoint grab(r.x + r.width * 0.5f, r.y + r.height * 0.5f);

    f.ui->startInstantClipDrag(laneA.get(), f.a, grab);
    f.ui->updateInstantClipDrag(AestraUI::NUIPoint(grab.x + 4.0f * pxPerBeat, grab.y)); // one bar to the right
    f.ui->finishInstantClipDrag();
    check(near(f.start(f.a), 8.0) && near(f.start(f.b), 16.0) && near(f.start(f.c), 12.0),
          "dragging one selected clip moves the whole selection by the same amount");
    check(f.laneOf(f.a) == 0 && f.laneOf(f.b) == 0 && f.laneOf(f.c) == 1, "and the lanes are untouched on a horizontal drag");
    check(f.tm->getCommandHistory().undo() && f.atOriginal(), "the group drag is ONE undo step");

    // Cancel restores everyone.
    f.ui->startInstantClipDrag(laneA.get(), f.a, grab);
    f.ui->updateInstantClipDrag(AestraUI::NUIPoint(grab.x + 8.0f * pxPerBeat, grab.y));
    f.ui->cancelInstantClipDrag();
    check(f.atOriginal(), "cancelling the drag puts every clip back");
}

void testPressOnSelectedClipKeepsGroupThenCollapsesOnClick() {
    std::cout << "[press and click]\n";
    Fixture f;
    f.selectAll();
    auto laneA = findLaneComponent(*f.ui, f.lane[0]);
    const auto r = laneA->getAllClipBounds().at(f.a);
    const AestraUI::NUIPoint centre(r.x + r.width * 0.5f, r.y + r.height * 0.5f);

    AestraUI::NUIComponent::dispatchMouseEvent(laneA.get(), mouse(AestraUI::NUIMouseEventType::Down, centre, true, false, AestraUI::NUIMouseButton::Left));
    check(f.ui->clipSelection().size() == 3, "pressing a clip that is part of the selection keeps the selection (so it can be dragged as a group)");
    AestraUI::NUIComponent::dispatchMouseEvent(laneA.get(), mouse(AestraUI::NUIMouseEventType::Up, centre, false, true, AestraUI::NUIMouseButton::Left));
    check(f.ui->clipSelection().size() <= 1, "a click that does not drag collapses the multi-selection");
    f.ui->onKeyEvent(key(AestraUI::NUIKeyCode::Right));
    check(near(f.start(f.a), 8.0) && near(f.start(f.b), 12.0) && near(f.start(f.c), 8.0), "and the clicked clip is the only one that moves afterwards");
}

} // namespace

int main() {
    testArrowNudge();
    testSingleSelectionStillMovesAlone();
    testCopyPasteBlock();
    testCutPasteMovesTheBlock();
    testGroupDrag();
    testPressOnSelectedClipKeepsGroupThenCollapsesOnClick();
    AestraUI::NUIComponent::clearFocusedComponent();
    std::cout << (g_failures == 0 ? "[PASS] ClipGroupOpsTest\n" : "[FAIL] ClipGroupOpsTest\n");
    return g_failures == 0 ? 0 : 1;
}
