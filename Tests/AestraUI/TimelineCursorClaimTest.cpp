// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Guards the timeline's hover cursor claim against popups (the invisible cursor
// over clip context menus).
//
// The reported failure: open a clip's context menu with the pointer near the
// clip's trim edge, move onto the menu, and the pointer vanished. The menu
// consumes mouse moves inside its bounds, so the timeline's last-seen position
// froze on the trim edge. The timeline kept claiming the cursor, the window
// manager therefore skipped drawing its arrow and kept the native cursor hidden,
// and the only cursor on screen was a trim glyph frozen at the old position.
//
// isTimelineHoverPointerCurrent() is the rule that now invalidates such a claim:
// the window reports every move, so a disagreement with the timeline's position
// means something above the timeline took the event.

#include "TrackManagerUIMath.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using Aestra::Audio::isTimelineHoverPointerCurrent;

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        ++failures;
    }
}

// The timeline received the latest move, so its hover claim stands.
void testClaimFromDeliveredMoveIsCurrent() {
    check(isTimelineHoverPointerCurrent(830.0f, 360.0f, 830.0f, 360.0f, true),
          "a position the timeline received must be current");
}

// The reported case: the pointer moved from the clip's trim edge onto the
// context menu, which consumed the move before the timeline heard about it.
void testClaimFromMoveConsumedByPopupIsStale() {
    check(!isTimelineHoverPointerCurrent(830.0f, 360.0f, 842.0f, 392.0f, true),
          "a move a popup consumed must invalidate the timeline's hover claim");
}

// A single pixel is a real move, in either axis, not noise.
void testOnePixelMoveIsStillStale() {
    check(!isTimelineHoverPointerCurrent(830.0f, 360.0f, 831.0f, 360.0f, true),
          "a horizontal one-pixel move the timeline never saw must be stale");
    check(!isTimelineHoverPointerCurrent(830.0f, 360.0f, 830.0f, 361.0f, true),
          "a vertical one-pixel move the timeline never saw must be stale");
}

// Both sides come from the same integers, so only float noise may differ.
void testSubPixelNoiseDoesNotInvalidate() {
    check(isTimelineHoverPointerCurrent(830.0f, 360.0f, 830.2f, 359.8f, true),
          "sub-pixel float noise must not invalidate a current claim");
}

// Before the window has reported a position there is nothing to compare, and
// the tool cursors must keep working exactly as before.
void testNoWindowPointerYetKeepsClaim() {
    check(isTimelineHoverPointerCurrent(0.0f, 0.0f, 500.0f, 500.0f, false),
          "with no window pointer reported yet, hover claims must stand");
}

} // namespace

int main() {
    testClaimFromDeliveredMoveIsCurrent();
    testClaimFromMoveConsumedByPopupIsStale();
    testOnePixelMoveIsStillStale();
    testSubPixelNoiseDoesNotInvalidate();
    testNoWindowPointerYetKeepsClaim();

    if (failures > 0) {
        std::cerr << "[FAIL] TimelineCursorClaimTest: " << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "[PASS] TimelineCursorClaimTest\n";
    return 0;
}
