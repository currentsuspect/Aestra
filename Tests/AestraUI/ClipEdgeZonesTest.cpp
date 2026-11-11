// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Where a press on a clip lands: resize, fade, split, or move.
//
// Owner reports (2026-11-07, editing a voice-over):
//   A. "When I hover over that top grid and it changes to that other tool, I want to resize, which
//      should be allowed." An unset fade claimed a 26px zone inward from each edge over the whole body,
//      so the edge only resized in the header band, and under the menu glyph not at all.
//   B. "When you select the cut tool, you cannot move the clips." With Split active a press anywhere on a
//      clip split it, headers and edges included.
//
// Drives a real TrackManager + TrackManagerUI + TrackUIComponent with NullRenderer and real mouse events.

#include "../Support/NullRenderer.h"
#include "MixerChannel.h"
#include "NUIComponent.h"
#include "PatternManager.h"
#include "PlaylistModel.h"
#include "TrackManager.h"
#include "TrackManagerUI.h"
#include "TrackUIComponent.h"
#include "TimelineClipGeometry.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

std::shared_ptr<TrackUIComponent> findLane(const AestraUI::NUIComponent& node) {
    for (const auto& child : node.getChildren()) {
        if (auto lane = std::dynamic_pointer_cast<TrackUIComponent>(child)) return lane;
        if (child) {
            if (auto nested = findLane(*child)) return nested;
        }
    }
    return nullptr;
}

struct Fixture {
    std::shared_ptr<TrackManager> trackManager;
    std::shared_ptr<AestraUI::NUIComponent> root;
    std::shared_ptr<TrackManagerUI> manager;
    std::shared_ptr<TrackUIComponent> lane;
    PlaylistLaneID laneId;
    ClipInstanceID clipId;

    bool build() {
        trackManager = std::make_shared<TrackManager>();
        trackManager->setCommandSink([](const AudioQueueCommand&) { return true; });
        auto& patterns = trackManager->getPatternManager();
        auto& playlist = trackManager->getPlaylistModel();

        AudioSlicePayload payload;
        payload.durationSeconds = 4.0;
        const PatternID patternId = patterns.createAudioPattern("Vox", 8.0, payload);
        laneId = playlist.createLane("Vox Lane");
        // Starts at beat 4 so the clip's left edge is well inside the grid, away from the lane's own edge.
        clipId = playlist.addClipFromPattern(laneId, patternId, 4.0, 8.0);
        if (!clipId.isValid()) {
            check(false, "fixture could not place an audio clip");
            return false;
        }
        manager = std::make_shared<TrackManagerUI>(trackManager);
        manager->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 600.0f));
        root = std::make_shared<AestraUI::NUIComponent>();
        root->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 600.0f));
        root->addChild(manager);
        manager->refreshTracks();
        lane = findLane(*manager);
        if (!lane) {
            check(false, "TrackManagerUI built no lane component");
            return false;
        }
        paint();
        if (lane->getAllClipBounds().empty()) {
            check(false, "painting published no clip bounds");
            return false;
        }
        return true;
    }

    void paint() {
        Aestra::Testing::NullRenderer renderer;
        root->onRender(renderer);
        lane->renderStatic(renderer);
        lane->renderControlOverlay(renderer);
    }

    AestraUI::NUIRect clip() const { return lane->getAllClipBounds().begin()->second; }

    void mouse(AestraUI::NUIMouseEventType type, float x, float y) {
        AestraUI::NUIMouseEvent e;
        e.type = type;
        e.position = AestraUI::NUIPoint(x, y);
        e.button = type == AestraUI::NUIMouseEventType::Move ? AestraUI::NUIMouseButton::None
                                                              : AestraUI::NUIMouseButton::Left;
        e.pressed = type == AestraUI::NUIMouseEventType::Down;
        e.released = type == AestraUI::NUIMouseEventType::Up;
        AestraUI::NUIComponent::dispatchMouseEvent(root.get(), e);
        paint();
    }
    void down(float x, float y) { mouse(AestraUI::NUIMouseEventType::Down, x, y); }
    void move(float x, float y) { mouse(AestraUI::NUIMouseEventType::Move, x, y); }
    void up(float x, float y) { mouse(AestraUI::NUIMouseEventType::Up, x, y); }

    const ClipInstance* clipModel() const { return trackManager->getPlaylistModel().getClip(clipId); }
    size_t clipCount() const {
        const auto* l = trackManager->getPlaylistModel().getLane(laneId);
        return l ? l->clips.size() : 0;
    }
    float bodyTop() const {
        const auto c = clip();
        return c.y + ClipPaint::headerBandHeight(c.height);
    }
};

// A1 / A2: the edge strip resizes at body height, hover and press agreeing.
void testEdgeStripResizesInTheBody() {
    for (const bool right : {true, false}) {
        Fixture f;
        if (!f.build()) return;
        const auto c = f.clip();
        const std::string side = right ? "right edge" : "left edge";
        const float x = right ? c.x + c.width - 3.0f : c.x + 3.0f;
        const float y = c.y + c.height * 0.5f;

        f.move(x, y);
        check(f.lane->isHoveringTrimEdge(), side + " mid-height, no fade: hover shows the resize cursor");
        f.down(x, y);
        check(f.lane->isTrimming(), side + " mid-height, no fade: press starts a trim");
        f.up(x, y);
    }
}

// A3: the top corner of the body, just inside the trim strip, still grabs the fade-in.
void testTopCornerGrabsFade() {
    Fixture f;
    if (!f.build()) return;
    const auto c = f.clip();
    const float x = c.x + 14.0f;
    const float y = c.y + 24.0f; // 6px into the fade band, which starts 18px below the clip top
    f.move(x, y);
    check(!f.lane->isHoveringTrimEdge(), "top corner 14px in: not a trim hover");
    f.down(x, y);
    check(!f.lane->isTrimming(), "top corner 14px in: not a trim");
    f.move(x + 40.0f, y);
    f.up(x + 40.0f, y);
    check(f.clipModel() && f.clipModel()->edits.fadeInBeats > 0.0, "top corner 14px in: dragging sets a fade-in");
}

// An unset fade below the corner band must not grab: that was the body-wide zone.
void testLowerBodyDoesNotGrabFade() {
    Fixture f;
    if (!f.build()) return;
    const auto c = f.clip();
    const float x = c.x + 14.0f;
    const float y = c.y + c.height - 6.0f;
    f.down(x, y);
    f.move(x + 40.0f, y);
    f.up(x + 40.0f, y);
    check(f.clipModel() && f.clipModel()->edits.fadeInBeats == 0.0, "lower body 14px in: no fade is grabbed");
}

// A4: the outermost pixels of the left edge resize even beside the menu glyph.
void testLeftEdgeBesideMenuGlyphResizes() {
    Fixture f;
    if (!f.build()) return;
    const auto c = f.clip();
    const float x = c.x + 2.0f;
    const float y = c.y + 4.0f;
    f.move(x, y);
    check(f.lane->isHoveringTrimEdge(), "left edge outermost px beside the glyph: hover shows resize");
    f.down(x, y);
    check(f.lane->isTrimming(), "left edge outermost px beside the glyph: press starts a trim");
    f.up(x, y);
}

// B1: Split on the body splits.
void testSplitToolSplitsTheBody() {
    Fixture f;
    if (!f.build()) return;
    f.manager->setCurrentTool(PlaylistTool::Split);
    const auto c = f.clip();
    const float x = c.x + c.width * 0.5f;
    f.down(x, f.bodyTop() + 10.0f);
    f.up(x, f.bodyTop() + 10.0f);
    check(f.clipCount() == 2, "split tool, press in the body: the clip is cut in two");
}

// B2: Split on the header drags the clip.
void testSplitToolHeaderMovesTheClip() {
    Fixture f;
    if (!f.build()) return;
    f.manager->setCurrentTool(PlaylistTool::Split);
    const double before = f.clipModel()->startBeat;
    const auto c = f.clip();
    const float x = c.x + c.width * 0.5f;
    const float y = c.y + 4.0f;
    f.down(x, y);
    for (int i = 1; i <= 6; ++i) f.move(x + 25.0f * i, y);
    f.up(x + 150.0f, y);
    check(f.clipCount() == 1, "split tool, press on the header: no split");
    check(f.clipModel() && f.clipModel()->startBeat > before, "split tool, drag on the header: the clip moves");
}

// B3: Split on a trim edge trims.
void testSplitToolEdgeTrims() {
    Fixture f;
    if (!f.build()) return;
    f.manager->setCurrentTool(PlaylistTool::Split);
    const auto c = f.clip();
    const float x = c.x + c.width - 3.0f;
    const float y = c.y + c.height * 0.5f;
    f.down(x, y);
    check(f.lane->isTrimming(), "split tool, press on a trim edge: trim starts");
    check(f.clipCount() == 1, "split tool, press on a trim edge: no split");
    f.up(x, y);
}

} // namespace

int main() {
    testEdgeStripResizesInTheBody();
    testTopCornerGrabsFade();
    testLowerBodyDoesNotGrabFade();
    testLeftEdgeBesideMenuGlyphResizes();
    testSplitToolSplitsTheBody();
    testSplitToolHeaderMovesTheClip();
    testSplitToolEdgeTrims();
    if (g_failures == 0) {
        std::cout << "Clip edge zone tests passed\n";
        return 0;
    }
    std::cout << g_failures << " clip edge zone test(s) failed\n";
    return 1;
}
