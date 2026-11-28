// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// When keyboard focus goes back from an embedded plugin editor to Aestra.
//
// A plugin's editor is a child X window, and clicking it gives that window keyboard focus. Nothing returned it, so Space
// and every other Aestra key did nothing until the next click on Aestra. The rule: while the pointer is over the plugin,
// keys go to it (typing a preset name must work); a moment after the pointer leaves, with no button held, Aestra takes
// focus back. The pause keeps a slip off the edge from stealing focus mid-typing, and a held button means a knob is being
// dragged out of the window. Pure, so a test can pin it; the panel supplies the facts.

namespace AestraUI {
namespace EditorFocus {

/// How long the pointer must be away from the plugin before focus returns.
constexpr double kReturnGraceSeconds = 0.25;

inline bool shouldReturnFocusToApp(bool pluginHoldsFocus, bool pointerOverPlugin, bool buttonDown,
                                   double secondsPointerAway) {
    if (!pluginHoldsFocus) return false;
    if (pointerOverPlugin || buttonDown) return false;
    return secondsPointerAway >= kReturnGraceSeconds;
}

/// Seconds the pointer has been away, after another @p dt: counting only while it is away.
inline double awaySeconds(double previous, double dt, bool pointerOverPlugin) {
    return pointerOverPlugin ? 0.0 : previous + dt;
}

} // namespace EditorFocus
} // namespace AestraUI
