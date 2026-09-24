// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// SPEC 3 §3.4: "when I drag [a note], it changes back to the normal cursor and does not
// maintain the drag state."
//
// Observed with a [setCursorStyle] backtrace during a real note drag: the piano-roll minimap,
// which sits in front of the note layer, reset the cursor to the arrow on EVERY move outside
// itself, overriding the note layer's Grabbing hand. NUIHoverCursorClaim is the rule it now
// follows: set the cursor only while hovered, hand it back once on the way out, and only if
// it set it.

#include "NUIHoverCursorClaim.h"

#include <iostream>
#include <string>

using namespace AestraUI;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

// The drag case: the pointer never entered this widget, so it must never touch the cursor.
void testAWidgetThatNeverClaimedLeavesTheCursorAlone() {
    NUIHoverCursorClaim claim;
    for (int move = 0; move < 5; ++move) {
        check(!claim.outside().has_value(), "moves elsewhere never reset another widget's cursor");
    }
}

void testLeavingHandsTheCursorBackExactlyOnce() {
    NUIHoverCursorClaim claim;
    const auto over = claim.inside(NUICursorStyle::Grab);
    check(over && *over == NUICursorStyle::Grab, "while hovered the widget sets its cursor");
    const auto leave = claim.outside();
    check(leave && *leave == NUICursorStyle::Arrow, "leaving hands the cursor back as the arrow");
    check(!claim.outside().has_value(), "and only once: later moves elsewhere leave it alone");
}

void testReenteringClaimsAgain() {
    NUIHoverCursorClaim claim;
    claim.inside(NUICursorStyle::ResizeEW);
    claim.outside();
    claim.inside(NUICursorStyle::Arrow);
    check(claim.claimed(), "hovering again re-claims, even with the arrow");
    check(claim.outside().has_value(), "so the next exit hands back again");
}

} // namespace

int main() {
    testAWidgetThatNeverClaimedLeavesTheCursorAlone();
    testLeavingHandsTheCursorBackExactlyOnce();
    testReenteringClaimsAgain();
    if (g_failures == 0) {
        std::cout << "Hover cursor claim tests passed\n";
        return 0;
    }
    std::cout << g_failures << " hover cursor claim test(s) failed\n";
    return 1;
}
