// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-W2 acceptance: "clip operations are reachable from the clip, not only from
// a panel or shortcut."
//
// The clip context menu shipped without any test, so the v0.8.0 checklist read
// `not started` and the canonical plan still calls it absent. Both were stale,
// and nothing could have caught the menu regressing or losing entries.
//
// This drives the real hierarchy rather than a convenient one. TrackUIComponent
// dynamic_casts to TrackManagerUI for Cut/Copy/Duplicate, so a plain parent
// component would silently drop half the menu and the test would be asserting
// against a configuration that never occurs in the app. TrackManagerUI therefore
// builds its own lane components here, exactly as it does at runtime.
//
// Two further details are load-bearing:
//   - Clip hit-boxes are published only while painting (renderStatic fills
//     m_allClipBounds), so a paint pass through NullRenderer is required before
//     any gesture can land on a clip.
//   - Events go through NUIComponent::dispatchMouseEvent, the public entry the
//     header recommends, which raises g_eventDispatchDepth the way the real
//     runtime does.
//
// Plain right-click is deliberately the fast-path delete, so Shift is the menu
// gesture.

#include "../Support/NullRenderer.h"
#include "MixerChannel.h"
#include "NUIComponent.h"
#include "NUIContextMenu.h"
#include "PatternManager.h"
#include "PlaylistModel.h"
#include "TrackManager.h"
#include "TrackManagerUI.h"
#include "TrackUIComponent.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

/** @brief Depth-first search for the first lane component in a subtree. */
std::shared_ptr<TrackUIComponent> findLane(const AestraUI::NUIComponent& node) {
    for (const auto& child : node.getChildren()) {
        if (auto lane = std::dynamic_pointer_cast<TrackUIComponent>(child)) {
            return lane;
        }
        if (child) {
            if (auto nested = findLane(*child)) {
                return nested;
            }
        }
    }
    return nullptr;
}

/** @brief Context menus attach to the root, so that is where they surface. */
std::shared_ptr<AestraUI::NUIContextMenu> findVisibleMenu(const AestraUI::NUIComponent& root) {
    for (const auto& child : root.getChildren()) {
        auto menu = std::dynamic_pointer_cast<AestraUI::NUIContextMenu>(child);
        if (menu && menu->isVisible()) {
            return menu;
        }
    }
    return nullptr;
}

std::vector<std::string> itemLabels(const AestraUI::NUIContextMenu& menu) {
    std::vector<std::string> labels;
    for (const auto& item : menu.getItems()) {
        if (item && !item->getText().empty()) {
            labels.push_back(item->getText());
        }
    }
    return labels;
}

bool hasLabel(const std::vector<std::string>& labels, const std::string& wanted) {
    return std::find(labels.begin(), labels.end(), wanted) != labels.end();
}

void reportLabels(const std::vector<std::string>& labels) {
    std::cout << "        menu contained:";
    for (const auto& label : labels) {
        std::cout << " [" << label << ']';
    }
    std::cout << '\n';
}

AestraUI::NUIMouseEvent rightClickAt(const AestraUI::NUIPoint& position, bool withShift) {
    AestraUI::NUIMouseEvent event;
    event.type = AestraUI::NUIMouseEventType::Down;
    event.position = position;
    event.button = AestraUI::NUIMouseButton::Right;
    event.pressed = true;
    if (withShift) {
        event.modifiers = AestraUI::NUIModifiers::Shift;
    }
    return event;
}

/** @brief A real TrackManagerUI over one lane carrying one MIDI clip. */
struct Fixture {
    std::shared_ptr<TrackManager> trackManager;
    std::shared_ptr<AestraUI::NUIComponent> root;
    std::shared_ptr<TrackManagerUI> manager;
    std::shared_ptr<TrackUIComponent> lane;
    ClipInstanceID clipId;
    AestraUI::NUIRect clipBounds{};

    bool build() {
        trackManager = std::make_shared<TrackManager>();
        auto& patterns = trackManager->getPatternManager();
        auto& playlist = trackManager->getPlaylistModel();

        const PatternID patternId = patterns.createMidiPattern("Clip Menu", 4.0, MidiPayload{});
        const PlaylistLaneID laneId = playlist.createLane("Clip Menu Lane");
        clipId = playlist.addClipFromPattern(laneId, patternId, 0.0, 4.0);
        if (!clipId.isValid()) {
            check(false, "fixture could not place a clip on the lane");
            return false;
        }

        manager = std::make_shared<TrackManagerUI>(trackManager);
        manager->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 600.0f));

        root = std::make_shared<AestraUI::NUIComponent>();
        root->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 600.0f));
        root->addChild(manager);

        // Lane components are lazy: the constructor defers this to first render.
        manager->refreshTracks();

        lane = findLane(*manager);
        if (!lane) {
            check(false, "TrackManagerUI built no lane component for the playlist lane");
            return false;
        }

        const auto laneBounds = lane->getBounds();
        if (laneBounds.width <= 0.0f || laneBounds.height <= 0.0f) {
            check(false, "the lane component has degenerate bounds, so nothing can be hit-tested");
            std::cout << "        lane bounds: " << laneBounds.x << ',' << laneBounds.y << ' ' << laneBounds.width
                      << 'x' << laneBounds.height << '\n';
            return false;
        }

        // The paint pass is what publishes clip geometry for hit-testing.
        Aestra::Testing::NullRenderer renderer;
        lane->renderStatic(renderer);

        const auto& bounds = lane->getAllClipBounds();
        const auto it = bounds.find(clipId);
        if (it == bounds.end()) {
            check(false, "the paint pass published no bounds for the clip, so no gesture can reach it");
            std::cout << "        published " << bounds.size() << " clip bound(s); lane bounds: " << laneBounds.x << ','
                      << laneBounds.y << ' ' << laneBounds.width << 'x' << laneBounds.height << '\n';
            return false;
        }
        clipBounds = it->second;
        if (clipBounds.width <= 0.0f || clipBounds.height <= 0.0f) {
            check(false, "the clip's published bounds are degenerate");
            return false;
        }
        return true;
    }

    AestraUI::NUIPoint clipCentre() const {
        return AestraUI::NUIPoint(clipBounds.x + clipBounds.width * 0.5f, clipBounds.y + clipBounds.height * 0.5f);
    }
};

// The acceptance criterion itself.
void testShiftRightClickOnClipOpensMenu() {
    Fixture fixture;
    if (!fixture.build()) {
        return;
    }

    check(findVisibleMenu(*fixture.root) == nullptr, "no context menu is open before the gesture");

    const bool handled = AestraUI::NUIComponent::dispatchMouseEvent(
        fixture.lane.get(), rightClickAt(fixture.clipCentre(), /*withShift=*/true));
    check(handled, "Shift+right-click on a clip is consumed by the lane");

    auto menu = findVisibleMenu(*fixture.root);
    check(menu != nullptr, "Shift+right-click on a clip opens a visible context menu on the root");
    if (!menu) {
        return;
    }

    // Delete is the entry the criterion is really about: it must not be
    // shortcut-only. Cut/Copy/Duplicate only appear when the parent really is a
    // TrackManagerUI, which is why the fixture builds the true hierarchy.
    const auto labels = itemLabels(*menu);
    const int before = g_failures;
    check(hasLabel(labels, "Delete Clip"), "the clip menu offers Delete Clip");
    check(hasLabel(labels, "Cut"), "the clip menu offers Cut");
    check(hasLabel(labels, "Copy"), "the clip menu offers Copy");
    check(hasLabel(labels, "Duplicate"), "the clip menu offers Duplicate");
    check(hasLabel(labels, "Open in Piano Roll"), "a MIDI clip's menu offers Open in Piano Roll");
    if (g_failures != before) {
        reportLabels(labels);
    }
}

// Without this, a menu that opened on any click anywhere would satisfy the test
// above while the gesture was not clip-targeted at all.
void testGestureIsClipTargeted() {
    Fixture fixture;
    if (!fixture.build()) {
        return;
    }

    const AestraUI::NUIPoint emptySpace(fixture.clipBounds.x + fixture.clipBounds.width + 200.0f,
                                        fixture.clipBounds.y + fixture.clipBounds.height * 0.5f);

    const auto& bounds = fixture.lane->getAllClipBounds();
    const bool overAnyClip =
        std::any_of(bounds.begin(), bounds.end(), [&](const auto& entry) { return entry.second.contains(emptySpace); });
    check(!overAnyClip, "the empty-space probe is genuinely not over any clip");

    AestraUI::NUIComponent::dispatchMouseEvent(fixture.lane.get(), rightClickAt(emptySpace, /*withShift=*/true));
    check(findVisibleMenu(*fixture.root) == nullptr, "Shift+right-click on empty lane space opens no clip menu");
}

} // namespace

int main() {
    testShiftRightClickOnClipOpensMenu();
    testGestureIsClipTargeted();
    AestraUI::NUIComponent::clearFocusedComponent();

    if (g_failures == 0) {
        std::cout << "Clip context menu reachability tests passed\n";
        return 0;
    }
    std::cout << g_failures << " clip context menu reachability test(s) failed\n";
    return 1;
}
