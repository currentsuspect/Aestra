// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Mute/solo/record/monitor glyphs, authored on a 24x24 grid and shared by every
// surface that offers those controls — the mixer strips (UIMixerButtonRow) and the
// arrangement track headers (Source/Components/TrackUIComponent.cpp). They lived as
// byte-identical copies in both files, which made "one icon language" a promise the
// comments made and nothing enforced; a stroke-width nudge in one would silently
// drift from the other. One definition, one place to edit.
//
// Colours are `currentColor` on purpose: NUISVGRenderer tints by overwriting the RGB
// of every opaque pixel, so any literal colour here would be discarded anyway, and
// transparent regions (alpha 0) survive as holes.

namespace AestraUI {

inline constexpr const char* kMuteIconSvg =
    R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M4 9v6h4l5 4.5v-15L8 9H4z" fill="currentColor"/><path d="M16 9.5 21 14.5 M21 9.5 16 14.5" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" fill="none"/></svg>)";

inline constexpr const char* kSoloIconSvg =
    R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M12 4.5a7.5 7.5 0 0 0-7.5 7.5v5.2a1.8 1.8 0 0 0 1.8 1.8h1.2a1 1 0 0 0 1-1v-4.2a1 1 0 0 0-1-1H6.5V12a5.5 5.5 0 0 1 11 0v.8h-1a1 1 0 0 0-1 1V18a1 1 0 0 0 1 1h1.2a1.8 1.8 0 0 0 1.8-1.8V12A7.5 7.5 0 0 0 12 4.5z" fill="currentColor"/></svg>)";

inline constexpr const char* kRecordIconSvg =
    R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="7.2" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="12" cy="12" r="3.4" fill="currentColor"/></svg>)";

// Track headers only — the mixer strips have no monitor toggle.
inline constexpr const char* kMonitorIconSvg =
    R"(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M3 12h3.2l2.2-5.5 3.4 11 2.2-5.5H21" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>)";

} // namespace AestraUI
