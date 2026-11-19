// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// One mouse-wheel notch scrolls a list by the same distance everywhere. Panels had drifted to 15, 20, 30, 36, 40,
// 48 and 60 px per notch (and the file browser to 96), so the same flick moved the Arsenal, the Audition files, the
// plugin browser and the mixer inspector by different amounts. A panel multiplies the event's wheelDelta by this.
//
// The file browser keeps its own step: it scrolls three whole rows a notch, so a row never lands half cut.

#pragma once

namespace AestraUI {

inline constexpr float kWheelNotchPx = 40.0f;

} // namespace AestraUI
