// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Guards the timeline minimap's grab-hand cursor claim (the pointer that vanished over the transport bar).
//
// The reported failure (owner, 4-6 Nov 2026, caught live on 6 Nov): rest the pointer on the minimap's viewport
// bar, move straight up into the transport bar, and the pointer disappears there. Hovering the bar sets the
// minimap's hover flag, so the timeline claims the cursor and draws its open hand. The flag only clears when the
// minimap hears about the pointer leaving, and it hears only through the timeline. The transport bar (like a
// panel over the timeline, or another window) never routes moves to the timeline, so the flag stayed set: the
// window manager kept skipping its arrow and kept the native cursor hidden, and the only cursor drawn was a hand
// frozen on the minimap.
//
// Every other hover claim already checks isHoverPointerCurrent() (the window reports every move, so a
// disagreement means the timeline missed one). The minimap's hover claim must too. A live drag of the bar is
// different: it owns the pointer until release, so it keeps the claim.
//
// Real hierarchy and real events, as in ClipContextMenuReachabilityTest: a TrackManagerUI lays out and models
// its own minimap, and moves reach it through NUIComponent::dispatchMouseEvent.

#include "NUIComponent.h"
#include "PatternManager.h"
#include "PlaylistModel.h"
#include "TimelineMinimapBar.h"
#include "TrackManager.h"
#include "TrackManagerUI.h"

#include <chrono>
#include <thread>
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

AestraUI::NUIMouseEvent mouse(AestraUI::NUIMouseEventType type, float x, float y, bool pressed = false,
                              bool released = false) {
    AestraUI::NUIMouseEvent event;
    event.type = type;
    event.position = AestraUI::NUIPoint(x, y);
    event.button = AestraUI::NUIMouseButton::Left;
    event.pressed = pressed;
    event.released = released;
    return event;
}

/** @brief A TrackManagerUI with a clip, sitting below a transport-bar strip the timeline never hears from. */
struct Fixture {
    static constexpr float kTimelineTop = 100.0f; // y < 100 is the transport bar: outside the timeline
    std::shared_ptr<TrackManager> trackManager;
    std::shared_ptr<AestraUI::NUIComponent> root;
    std::shared_ptr<TrackManagerUI> manager;
    std::shared_ptr<AestraUI::TimelineMinimapBar> minimap;
    AestraUI::NUIPoint onBar{};

    // The window manager's half of the contract: every move is reported to the timeline, delivered or not.
    void windowMove(float x, float y) { manager->setWindowPointerPosition(x, y); }
    // A move the timeline itself receives (the pointer is over it).
    void timelineMove(float x, float y) {
        windowMove(x, y);
        AestraUI::NUIComponent::dispatchMouseEvent(manager.get(), mouse(AestraUI::NUIMouseEventType::Move, x, y));
    }

    bool build() {
        trackManager = std::make_shared<TrackManager>();
        auto& patterns = trackManager->getPatternManager();
        auto& playlist = trackManager->getPlaylistModel();
        const PatternID patternId = patterns.createMidiPattern("Minimap", 4.0, MidiPayload{});
        const PlaylistLaneID laneId = playlist.createLane("Minimap Lane");
        if (!playlist.addClipFromPattern(laneId, patternId, 0.0, 64.0).isValid()) {
            check(false, "fixture could not place a clip");
            return false;
        }

        root = std::make_shared<AestraUI::NUIComponent>();
        root->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 700.0f));
        manager = std::make_shared<TrackManagerUI>(trackManager);
        manager->setBounds(AestraUI::NUIRect(0.0f, kTimelineTop, 1400.0f, 600.0f));
        root->addChild(manager);
        manager->refreshTracks(); // its layout pass sizes the minimap and hands it the view model

        for (const auto& child : manager->getChildren()) {
            if (auto bar = std::dynamic_pointer_cast<AestraUI::TimelineMinimapBar>(child)) {
                minimap = bar;
            }
        }
        if (!minimap || !minimap->isVisible()) {
            check(false, "the timeline built no visible minimap");
            return false;
        }

        // The minimap's summary is built on TimelineSummaryCache's worker thread. Until the build lands the model
        // holds the empty starting summary (domain 0..0), and with no domain there is no viewport bar to hover.
        // Keep the timeline updating, as the frame loop does, until the built summary is in the model. Sweeping
        // straight away raced the worker: it lost on a loaded machine and on CI runners.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        auto summaryReady = [&] {
            const auto* snapshot = minimap->getModel().summary;
            return snapshot && snapshot->summary && snapshot->summary->domainEndBeat - snapshot->summary->domainStartBeat > 1e-9;
        };
        while (!summaryReady() && std::chrono::steady_clock::now() < deadline) {
            static_cast<AestraUI::NUIComponent&>(*manager).onUpdate(0.0); // public on the base, as the frame loop calls it
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!summaryReady()) {
            check(false, "the minimap never received its built summary");
            return false;
        }

        // Find the viewport bar body the way a pointer does: sweep the minimap's middle row with real moves
        // until the minimap reports viewport hover (resize edges report their own hint, not this).
        const auto b = minimap->getBounds();
        const float y = b.y + b.height * 0.5f;
        for (float x = b.x + 1.0f; x < b.x + b.width; x += 2.0f) {
            timelineMove(x, y);
            if (minimap->isViewportPanActive() && !minimap->isViewportDragActive()) {
                onBar = AestraUI::NUIPoint(x + 3.0f, y); // a little inside, clear of the left resize edge
                timelineMove(onBar.x, onBar.y);
                if (minimap->isViewportPanActive()) {
                    return true;
                }
            }
        }
        check(false, "no point on the minimap reported viewport-bar hover");
        std::cout << "        minimap bounds: " << b.x << ',' << b.y << ' ' << b.width << 'x' << b.height << '\n';
        return false;
    }
};

// Sanity: hovering the bar is a legitimate claim (the open hand), and the timeline makes it.
void testHoveringTheBarClaimsTheCursor() {
    Fixture f;
    if (!f.build())
        return;
    check(f.manager->isCustomCursorActive(), "hovering the minimap's viewport bar must claim the cursor (grab hand)");
}

// The reported case: from the bar straight up into the transport bar. The window reports the move, the
// timeline never receives it, so its claim must lapse and the window manager's arrow come back.
void testLeavingUpIntoTheTransportReleasesTheClaim() {
    Fixture f;
    if (!f.build())
        return;
    f.windowMove(f.onBar.x, Fixture::kTimelineTop - 40.0f);
    check(!f.manager->isCustomCursorActive(),
          "after leaving the minimap into the transport bar (a move the timeline never saw), the timeline must "
          "not keep the cursor: with the claim stuck the pointer is invisible there");
}

// Coming back over the timeline resolves normally from a fresh move.
void testComingBackResolvesFromTheNewMove() {
    Fixture f;
    if (!f.build())
        return;
    f.windowMove(f.onBar.x, Fixture::kTimelineTop - 40.0f);
    f.timelineMove(f.onBar.x, f.onBar.y);
    check(f.manager->isCustomCursorActive(), "back on the bar, the claim returns from the new move");
    f.timelineMove(700.0f, 500.0f);
    check(!f.minimap->isViewportPanActive(), "a move over the lanes clears the minimap's hover");
}

// A live drag of the bar owns the pointer until release, wherever it goes: it keeps the claim (closed hand).
void testDraggingTheBarKeepsTheClaim() {
    Fixture f;
    if (!f.build())
        return;
    f.windowMove(f.onBar.x, f.onBar.y);
    AestraUI::NUIComponent::dispatchMouseEvent(
        f.manager.get(), mouse(AestraUI::NUIMouseEventType::Down, f.onBar.x, f.onBar.y, /*pressed=*/true));
    if (!f.minimap->isViewportDragActive()) {
        check(false, "a press on the viewport bar must start a viewport drag");
        return;
    }
    f.windowMove(f.onBar.x, Fixture::kTimelineTop - 40.0f);
    check(f.manager->isCustomCursorActive(), "while the bar is being dragged, the drag keeps the cursor");
    AestraUI::NUIComponent::dispatchMouseEvent(
        f.manager.get(), mouse(AestraUI::NUIMouseEventType::Up, f.onBar.x, f.onBar.y, false, /*released=*/true));
}

// A drag of the bar released over the transport bar: the release never reaches the timeline, only the window
// hears it. The window passes it on, and the drag, its claim and the closed hand must end with it.
void testDragReleasedOverTheTransportEnds() {
    Fixture f;
    if (!f.build())
        return;
    f.windowMove(f.onBar.x, f.onBar.y);
    AestraUI::NUIComponent::dispatchMouseEvent(
        f.manager.get(), mouse(AestraUI::NUIMouseEventType::Down, f.onBar.x, f.onBar.y, /*pressed=*/true));
    if (!f.minimap->isViewportDragActive()) {
        check(false, "a press on the viewport bar must start a viewport drag");
        return;
    }
    f.windowMove(f.onBar.x, Fixture::kTimelineTop - 40.0f);
    f.manager->onWindowMouseReleased(); // what the window manager does on every left release
    check(!f.minimap->isViewportDragActive(), "a release over the transport bar must end the minimap drag");
    check(!f.manager->isCustomCursorActive(), "with the drag over, the timeline must give the cursor back");
}

} // namespace

int main() {
    testHoveringTheBarClaimsTheCursor();
    testLeavingUpIntoTheTransportReleasesTheClaim();
    testComingBackResolvesFromTheNewMove();
    testDraggingTheBarKeepsTheClaim();
    testDragReleasedOverTheTransportEnds();
    if (g_failures > 0) {
        std::cout << "[FAIL] TimelineMinimapCursorClaimTest: " << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "[PASS] TimelineMinimapCursorClaimTest\n";
    return 0;
}
