// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The pointer must never be invisible. Owner reports (2026-10-11): an invisible pointer over the file browser,
// over knobs, and in the piano roll after auditioning a one-shot. The watchdog is the net under every
// stale-claim / lost-capture cause: if the OS pointer is hidden and the app is not drawing one for a run
// of frames, show the OS pointer; a deliberate drag capture is never overridden.

#include "NUIPointerVisibilityWatchdog.h"

#include <iostream>
#include <string>

using AestraUI::NUIPointerVisibilityWatchdog;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}

} // namespace

int main() {
    constexpr int kGrace = NUIPointerVisibilityWatchdog::kGraceFrames;

    {   // The failure itself: hidden, nothing drawn, not captured.
        NUIPointerVisibilityWatchdog w;
        bool rescued = false;
        for (int i = 0; i < kGrace - 1; ++i) rescued = rescued || w.update(true, false, false);
        check(!rescued, "a pointer hidden with nothing drawn is tolerated for the grace period (no flicker on a single frame)");
        check(w.update(true, false, false), "after the grace period the OS pointer is rescued");
        check(w.update(true, false, false) && w.engaged(), "and it stays rescued while the situation lasts (no hide/show flicker)");
    }
    {   // Normal operation never trips it.
        NUIPointerVisibilityWatchdog w;
        bool rescued = false;
        for (int i = 0; i < 200; ++i) rescued = rescued || w.update(true, true, false);
        check(!rescued, "the overlay arrow or tool cursor being drawn keeps the OS pointer hidden");
        for (int i = 0; i < 200; ++i) rescued = rescued || w.update(false, false, false);
        check(!rescued, "an OS pointer that is not hidden needs no rescue");
    }
    {   // A drag capture hides the pointer on purpose.
        NUIPointerVisibilityWatchdog w;
        bool rescued = false;
        for (int i = 0; i < 500; ++i) rescued = rescued || w.update(true, false, true);
        check(!rescued, "a deliberate drag capture is never overridden, however long the drag");
    }
    {   // Recovery.
        NUIPointerVisibilityWatchdog w;
        for (int i = 0; i < kGrace + 5; ++i) w.update(true, false, false);
        check(w.engaged(), "engaged after a long invisible spell");
        check(!w.update(true, true, false) && !w.engaged(), "it lets go as soon as the app draws a pointer again");
        for (int i = 0; i < kGrace - 1; ++i) w.update(true, false, false);
        check(!w.engaged(), "and the grace period starts over (the counter was reset)");
        check(!w.update(false, false, false), "an un-hidden OS pointer clears the state");
    }

    std::cout << (g_failures == 0 ? "[PASS] NUIPointerVisibilityWatchdogTest\n" : "[FAIL] NUIPointerVisibilityWatchdogTest\n");
    return g_failures == 0 ? 0 : 1;
}
