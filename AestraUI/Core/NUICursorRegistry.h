// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// NUICursorRegistry — the single canonical source of cursor artwork.
//
// Every interaction cursor in the app resolves through this registry so the
// same interaction uses the same asset regardless of which editor or component
// renders it. Prefer these SVG glyphs over ad-hoc drawLine/vector cursor
// artwork.
//
// - nuiCursorSvg(style): the overlay cursor glyphs (drawn by the app's custom
//   cursor renderer). High-contrast white-on-black by default.
// - nuiTrimResizeCursorSvg(): a tintable horizontal-stretch glyph for
//   components that paint their own cursor (e.g. the track trim edge), using
//   currentColor so NUIIcon::setColor drives the tone.

#pragma once

#include "../Platform/NUICursorStyle.h"

namespace AestraUI {

/** @brief Canonical SVG for an interaction cursor style, or nullptr for styles
 *  with no dedicated glyph (the caller falls back to the default cursor). */
inline const char* nuiCursorSvg(NUICursorStyle style) {
    switch (style) {
    case NUICursorStyle::Arrow:
        return "<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
               "<path d=\"M5 2L5 18L9 14L12 21L14 20L11 13L17 13L5 2Z\" fill=\"white\" stroke=\"black\" stroke-width=\"1.5\"/></svg>";
    case NUICursorStyle::Hand:
        return "<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
               "<path d=\"M7 12.5V9.5C7 8.95 7.45 8.5 8 8.5C8.55 8.5 9 8.95 9 9.5V12.2H9.6V5C9.6 4.45 10.05 4 10.6 4C11.15 4 11.6 4.45 11.6 5V12.2H12.2V3.2C12.2 2.65 12.65 2.2 13.2 2.2C13.75 2.2 14.2 2.65 14.2 3.2V12.2H14.8V6.2C14.8 5.65 15.25 5.2 15.8 5.2C16.35 5.2 16.8 5.65 16.8 6.2V14.1C16.8 17.35 14.15 20 10.9 20H10.6C7.9 20 5.7 17.8 5.7 15.1V12.5C5.7 11.95 6.15 11.5 6.7 11.5C6.93 11.5 7.14 11.58 7.3 11.72C7.32 11.74 7.33 11.75 7.35 11.77C7.56 11.96 7.7 12.22 7.7 12.5H7Z\" fill=\"white\" stroke=\"black\" stroke-width=\"1.15\" stroke-linejoin=\"round\"/></svg>";
    case NUICursorStyle::Grab:
        return "<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
               "<path d=\"M12 6V3C12 2.45 12.45 2 13 2C13.55 2 14 2.45 14 3V10H15V4C15 3.45 15.45 3 16 3C16.55 3 17 3.45 17 4V10H18V5C18 4.45 18.45 4 19 4C19.55 4 20 4.45 20 5V15C20 18.31 17.31 21 14 21H12C8.69 21 6 18.31 6 15V12C6 11.45 6.45 11 7 11C7.55 11 8 11.45 8 12V14H9V6C9 5.45 9.45 5 10 5C10.55 5 11 5.45 11 6V10H12V6Z\" fill=\"white\" stroke=\"black\" stroke-width=\"1\"/></svg>";
    case NUICursorStyle::Grabbing:
        return "<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
               "<path d=\"M9.5 5.8C9.5 5.14 10.04 4.6 10.7 4.6C11.36 4.6 11.9 5.14 11.9 5.8V9.1H12.5V4.7C12.5 4.04 13.04 3.5 13.7 3.5C14.36 3.5 14.9 4.04 14.9 4.7V9.1H15.5V6.4C15.5 5.74 16.04 5.2 16.7 5.2C17.36 5.2 17.9 5.74 17.9 6.4V11.7C17.9 15.73 14.63 19 10.6 19C7.51 19 5 16.49 5 13.4V10.7C5 10.04 5.54 9.5 6.2 9.5C6.86 9.5 7.4 10.04 7.4 10.7V12.9H8V7C8 6.34 8.54 5.8 9.2 5.8H9.5Z\" fill=\"white\" stroke=\"black\" stroke-width=\"1.2\" stroke-linejoin=\"round\"/>"
               "<path d=\"M7.9 14.3C8.05 15.72 9.25 16.8 10.7 16.8C12.28 16.8 13.56 15.52 13.56 13.94V12.7H7.8V13.5C7.8 13.77 7.84 14.04 7.9 14.3Z\" fill=\"black\" fill-opacity=\"0.16\"/></svg>";
    case NUICursorStyle::IBeam:
        return "<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
               "<path d=\"M9 4H11M15 4H13M11 4V20M13 4V20M11 4C11 4 11 4 12 4C13 4 13 4 13 4M11 20H9M15 20H13M11 20C11 20 11 20 12 20C13 20 13 20 13 20\" stroke=\"white\" stroke-width=\"2\" stroke-linecap=\"round\"/>"
               "<path d=\"M9 4H11M15 4H13M11 4V20M13 4V20M11 4C11 4 11 4 12 4C13 4 13 4 13 4M11 20H9M15 20H13M11 20C11 20 11 20 12 20C13 20 13 20 13 20\" stroke=\"black\" stroke-width=\"3\" stroke-linecap=\"round\" opacity=\"0.3\"/></svg>";
    case NUICursorStyle::ResizeEW:
        return "<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
               "<path d=\"M18 12L22 12M22 12L19 9M22 12L19 15M6 12L2 12M2 12L5 9M2 12L5 15M12 6V18\" stroke=\"white\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
               "<path d=\"M18 12L22 12M22 12L19 9M22 12L19 15M6 12L2 12M2 12L5 9M2 12L5 15M12 6V18\" stroke=\"black\" stroke-width=\"3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.3\"/></svg>";
    case NUICursorStyle::ResizeNS:
        return "<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
               "<path d=\"M12 6L12 2M12 2L9 5M12 2L15 5M12 18L12 22M12 22L9 19M12 22L15 19M6 12H18\" stroke=\"white\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
               "<path d=\"M12 6L12 2M12 2L9 5M12 2L15 5M12 18L12 22M12 22L9 19M12 22L15 19M6 12H18\" stroke=\"black\" stroke-width=\"3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.3\"/></svg>";
    case NUICursorStyle::ResizeNESW:
        return "<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
               "<path d=\"M16 8L22 2M22 2H18M22 2V6M8 16L2 22M2 22H6M2 22V18M9 15L15 9\" stroke=\"white\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
               "<path d=\"M16 8L22 2M22 2H18M22 2V6M8 16L2 22M2 22H6M2 22V18M9 15L15 9\" stroke=\"black\" stroke-width=\"3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.3\"/></svg>";
    case NUICursorStyle::ResizeNWSE:
        return "<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
               "<path d=\"M8 8L2 2M2 2H6M2 2V6M16 16L22 22M22 22H18M22 22V18M9 9L15 15\" stroke=\"white\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
               "<path d=\"M8 8L2 2M2 2H6M2 2V6M16 16L22 22M22 22H18M22 22V18M9 9L15 15\" stroke=\"black\" stroke-width=\"3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.3\"/></svg>";
    default:
        return nullptr;
    }
}

/** @brief Tintable horizontal-stretch glyph for component-drawn cursors
 *  (currentColor follows NUIIcon::setColor). */
inline const char* nuiTrimResizeCursorSvg() {
    return "<svg viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
           "<path d=\"M18 12L22 12M22 12L19 9M22 12L19 15M6 12L2 12M2 12L5 9M2 12L5 15M12 6V18\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>";
}

} // namespace AestraUI
