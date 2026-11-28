// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Who may own the cursor right now. Header-only so the rule is testable without a window.
//
// A panel's edge band turns the cursor into a resize arrow, but only while the pointer is really on the panel. A
// modal dialog or an open menu/dropdown/picker covers it: the pointer belongs to that cover, and the band underneath
// is behind glass. The window used to count only modal dialogs, so opening the File menu with the pointer over a
// panel's top edge left the resize arrow on the menu (28 Nov 2026).

namespace AestraUI {

/// True when a panel's edge band may set the resize cursor: nothing is covering the panels.
inline bool panelEdgesMayOwnCursor(bool modalOpen, bool popupOpen) {
    return !modalOpen && !popupOpen;
}

/// True on the frame a cover (modal or popup) appears. The cursor is handed back to the arrow once then: a menu
/// opened from the keyboard, or with the pointer still, produces no move to recompute it, and whatever resize or tool
/// cursor was showing would otherwise sit on top of the menu until the pointer moves.
inline bool shouldHandCursorBack(bool nowOpen, bool wasOpen) {
    return nowOpen && !wasOpen;
}

} // namespace AestraUI
