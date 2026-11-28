// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PressPairerTest — the platform bridge sets NUIMouseEvent::doubleClick, so controls that read only the flag work.
//
// Reported by the founder: double-click did nothing on Sat, Comp or Verb. Nothing in the tree ever assigned the flag (the
// header of NUIDoubleClickTracker says so), so every control that read only the flag was dead: every NUISlider, the Comp,
// Verb and EQ editors, the Ember knobs, the audio settings page. NUIPressPairer pairs presses in the one place events are
// made. A knob needs a special case: the first click of its double-click starts a capture, and ending it warps the cursor to
// the knob's centre, so the second press lands somewhere else than the first.

#include "../../AestraUI/Core/NUIPressPairer.h"

#include <iostream>
#include <string>

using namespace AestraUI;

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}
constexpr auto L = NUIMouseButton::Left;
constexpr auto R = NUIMouseButton::Right;
} // namespace

int main() {
    {
        NUIPressPairer p;
        check(!p.registerPress(L, 100, 100, 1000), "a first press is a single press");
        check(p.registerPress(L, 101, 100, 1150), "a second press soon after, in the same place, is a double-click");
        check(!p.registerPress(L, 101, 100, 1200), "a third rapid press is single again (the double consumed the pair)");
    }
    {
        NUIPressPairer p;
        p.registerPress(L, 100, 100, 1000);
        check(!p.registerPress(L, 100, 100, 1000 + NUIPressPairer::kWindowMs + 1), "too slow is not a double-click");
    }
    {
        NUIPressPairer p;
        p.registerPress(L, 100, 100, 1000);
        check(!p.registerPress(L, 160, 100, 1100), "a press on a neighbouring control is not a double-click");
    }
    {
        NUIPressPairer p;
        p.registerPress(L, 100, 100, 1000);
        check(!p.registerPress(R, 100, 100, 1100), "a different button does not pair");
        check(!p.registerPress(L, 100, 100, 1150), "and the left press after it starts afresh");
    }
    {
        // The knob case: capture ends, the cursor is warped to the knob's centre, the second press lands there.
        NUIPressPairer p;
        p.registerPress(L, 210, 130, 1000);
        p.noteCursorWarp(200, 150); // the knob's centre
        check(p.registerPress(L, 200, 151, 1200), "a second press at the warp target pairs with the first");
    }
    {
        // Where the warp did nothing (native Wayland) the pointer is still where the first press left it.
        NUIPressPairer p;
        p.registerPress(L, 210, 130, 1000);
        p.noteCursorWarp(200, 150);
        check(p.registerPress(L, 211, 131, 1200), "a second press where the pointer stayed also pairs");
    }
    {
        NUIPressPairer p;
        p.registerPress(L, 210, 130, 1000);
        p.noteCursorWarp(200, 150);
        check(!p.registerPress(L, 300, 300, 1200), "a press elsewhere still does not pair after a warp");
    }
    {
        NUIPressPairer p;
        p.noteCursorWarp(200, 150); // no first press pending: nothing to pair with
        check(!p.registerPress(L, 200, 150, 1000), "a warp alone makes the next press a single press");
    }
    {
        NUIPressPairer p;
        p.registerPress(L, 100, 100, 1000);
        p.reset();
        check(!p.registerPress(L, 100, 100, 1100), "reset forgets the pending press");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "press pairer passed\n";
    return 0;
}
