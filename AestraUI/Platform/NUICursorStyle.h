// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// NUICursorStyle — the canonical cursor-style enumeration.
//
// Split out of NUIPlatformBridge.h so cursor consumers (the cursor registry,
// the custom-cursor renderer) can reference the canonical style set without
// pulling in the whole platform bridge. NUIPlatformBridge.h re-includes this,
// so existing `enum class NUICursorStyle` usages are unchanged.

#pragma once

#include <cstdint>

namespace AestraUI {

/**
 * Cursor styles for setCursorStyle()
 */
enum class NUICursorStyle {
    Arrow,          // Default arrow cursor
    Hand,           // Pointing hand (for clickable elements)
    IBeam,          // Text input cursor
    Wait,           // Loading/busy cursor (hourglass/spinner)
    WaitArrow,      // Arrow with loading indicator
    Crosshair,      // Precision crosshair
    ResizeNS,       // North-South resize (vertical)
    ResizeEW,       // East-West resize (horizontal)
    ResizeNESW,     // Diagonal resize (NE-SW)
    ResizeNWSE,     // Diagonal resize (NW-SE)
    ResizeAll,      // Move/all directions
    NotAllowed,     // Disabled/not allowed
    Grab,           // Open hand (ready to grab)
    Grabbing,       // Closed hand (currently grabbing)
    Hidden          // No cursor visible
};

} // namespace AestraUI
