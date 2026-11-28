// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// CursorOwnershipTest — who may own the cursor right now (the "File menu over the piano roll's title bar" report,
// 28 Nov 2026).
//
// A panel's edge band turns the cursor into a resize arrow. That is only true while the pointer is really on the
// panel. With a menu or dropdown open, the pointer belongs to the popup, and the band underneath it is behind glass:
// opening File with the pointer over the piano roll's top edge left the resize arrow on the menu, because the window
// only counted modal dialogs, not popups, when it decided whether the panel edges still owned the cursor.

#include "CursorOwnership.h"

#include <iostream>
#include <string>

namespace {
int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    }
}
} // namespace

int main() {
    using AestraUI::panelEdgesMayOwnCursor;
    using AestraUI::shouldHandCursorBack;

    check(panelEdgesMayOwnCursor(false, false), "with nothing open the panel edges own the cursor");
    check(!panelEdgesMayOwnCursor(true, false), "a modal dialog covers the panel edges");
    check(!panelEdgesMayOwnCursor(false, true), "an open menu or dropdown covers the panel edges");
    check(!panelEdgesMayOwnCursor(true, true), "a modal and a popup together cover them too");

    // The hand-back happens once, when the cover appears, not every frame (a component may set its own cursor
    // over the popup afterwards) and not when it closes.
    check(shouldHandCursorBack(/*nowOpen=*/true, /*wasOpen=*/false), "a popup opening hands the cursor back once");
    check(!shouldHandCursorBack(true, true), "an already-open popup does not hand it back again");
    check(!shouldHandCursorBack(false, true), "closing a popup does not hand the cursor back");
    check(!shouldHandCursorBack(false, false), "nothing open, nothing to hand back");

    if (g_failures > 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "CursorOwnershipTest: all checks passed\n";
    return 0;
}
