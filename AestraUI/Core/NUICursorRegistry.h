// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// NUICursorRegistry — the single canonical source of cursor artwork.
//
// Every interaction cursor in the app resolves through this registry so the
// same interaction uses the same asset regardless of which editor or component
// renders it. Prefer these SVG glyphs over ad-hoc drawLine/vector cursor
// artwork.
//
// - nuiCursorSvg(style): the overlay cursor glyphs (drawn by the app's custom
//   cursor renderer). White fill, near-black outline and a soft drop shadow, so
//   every glyph reads on dark, mid and light surfaces alike.
// - nuiTrimResizeCursorSvg(): a tintable horizontal-stretch glyph for
//   components that paint their own cursor (e.g. the track trim edge), using
//   currentColor so NUIIcon::setColor drives the tone.
//
// Glyphs mean what they say:
//   Hand     — a pointing hand: something clickable.
//   Grab     — an open hand: something draggable, hovered.
//   Grabbing — the same hand closed: something being dragged right now.
//
// Hotspots (24x24 viewBox; the renderer offsets each glyph so the hotspot sits
// on the pointer — keep AestraWindowManager::renderCustomCursor in step):
//   Arrow (2, 2) · Hand fingertip (9, 2) · everything else centred at (12, 12).

#pragma once

#include "../Platform/NUICursorStyle.h"

namespace AestraUI {

/** @brief Canonical SVG for an interaction cursor style, or nullptr for styles
 *  with no dedicated glyph (the caller falls back to the default cursor). */
inline const char* nuiCursorSvg(NUICursorStyle style) {
    switch (style) {
    case NUICursorStyle::Arrow:
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
               R"SVG(<path d="M2 2 L2 17.6 L6.2 13.7 L9 20.1 C9.22 20.62 9.8 20.86 10.32 20.64 L12.08 19.9 C12.6 19.68 12.84 19.1 12.62 18.58 L9.92 12.2 L15.6 12.2 Z" fill="#000" fill-opacity="0.32" transform="translate(0.7 1)"/>)SVG"
               R"SVG(<path d="M2 2 L2 17.6 L6.2 13.7 L9 20.1 C9.22 20.62 9.8 20.86 10.32 20.64 L12.08 19.9 C12.6 19.68 12.84 19.1 12.62 18.58 L9.92 12.2 L15.6 12.2 Z" fill="#fff" stroke="#141416" stroke-width="1.35" stroke-linejoin="round"/>)SVG"
               R"SVG(</svg>)SVG";
    case NUICursorStyle::Hand:
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
               R"SVG(<path d="M9 2.2 C9.9 2.2 10.6 2.9 10.6 3.8 V9.6 C10.9 9.2 11.4 9 12 9 C12.8 9 13.4 9.6 13.4 10.4 C13.7 10 14.2 9.8 14.7 9.8 C15.5 9.8 16.1 10.4 16.1 11.2 C16.4 10.9 16.8 10.8 17.2 10.8 C18 10.8 18.6 11.4 18.6 12.2 V15.6 C18.6 18.6 16.2 21 13.2 21 H11.6 C9.6 21 8.1 20.1 7 18.6 L4.3 14.9 C3.8 14.2 4 13.3 4.7 12.9 C5.4 12.5 6.3 12.7 6.8 13.3 L7.4 14.1 V3.8 C7.4 2.9 8.1 2.2 9 2.2 Z" fill="#000" fill-opacity="0.32" transform="translate(0.7 1)"/>)SVG"
               R"SVG(<path d="M9 2.2 C9.9 2.2 10.6 2.9 10.6 3.8 V9.6 C10.9 9.2 11.4 9 12 9 C12.8 9 13.4 9.6 13.4 10.4 C13.7 10 14.2 9.8 14.7 9.8 C15.5 9.8 16.1 10.4 16.1 11.2 C16.4 10.9 16.8 10.8 17.2 10.8 C18 10.8 18.6 11.4 18.6 12.2 V15.6 C18.6 18.6 16.2 21 13.2 21 H11.6 C9.6 21 8.1 20.1 7 18.6 L4.3 14.9 C3.8 14.2 4 13.3 4.7 12.9 C5.4 12.5 6.3 12.7 6.8 13.3 L7.4 14.1 V3.8 C7.4 2.9 8.1 2.2 9 2.2 Z" fill="#fff" stroke="#141416" stroke-width="1.35" stroke-linejoin="round"/>)SVG"
               R"SVG(<path d="M10.6 9.6 V12.8 M13.4 10.4 V13 M16.1 11.2 V13.2" stroke="#141416" stroke-width="1" stroke-linecap="round"/>)SVG"
               R"SVG(</svg>)SVG";
    case NUICursorStyle::Grab:
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
               R"SVG(<path d="M8.4 14.2 V6.3 C8.4 5.5 9 4.9 9.8 4.9 C10.6 4.9 11.2 5.5 11.2 6.3 V4.9 C11.2 4.1 11.8 3.5 12.6 3.5 C13.4 3.5 14 4.1 14 4.9 V5.8 C14 5 14.6 4.4 15.4 4.4 C16.2 4.4 16.8 5 16.8 5.8 V7.6 C16.8 6.9 17.3 6.4 18 6.4 C18.7 6.4 19.2 6.9 19.2 7.6 V14.6 C19.2 18.2 16.6 21 13.2 21 H12.4 C10.4 21 8.8 20 7.7 18.4 L4.9 14.3 C4.5 13.7 4.6 13 5.3 12.6 C6 12.2 6.9 12.4 7.4 13 Z" fill="#000" fill-opacity="0.32" transform="translate(0.7 1)"/>)SVG"
               R"SVG(<path d="M8.4 14.2 V6.3 C8.4 5.5 9 4.9 9.8 4.9 C10.6 4.9 11.2 5.5 11.2 6.3 V4.9 C11.2 4.1 11.8 3.5 12.6 3.5 C13.4 3.5 14 4.1 14 4.9 V5.8 C14 5 14.6 4.4 15.4 4.4 C16.2 4.4 16.8 5 16.8 5.8 V7.6 C16.8 6.9 17.3 6.4 18 6.4 C18.7 6.4 19.2 6.9 19.2 7.6 V14.6 C19.2 18.2 16.6 21 13.2 21 H12.4 C10.4 21 8.8 20 7.7 18.4 L4.9 14.3 C4.5 13.7 4.6 13 5.3 12.6 C6 12.2 6.9 12.4 7.4 13 Z" fill="#fff" stroke="#141416" stroke-width="1.35" stroke-linejoin="round"/>)SVG"
               R"SVG(<path d="M11.2 6.3 V11.6 M14 5.8 V11.4 M16.8 7.6 V11.7" stroke="#141416" stroke-width="1" stroke-linecap="round"/>)SVG"
               R"SVG(</svg>)SVG";
    case NUICursorStyle::Grabbing:
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
               R"SVG(<path d="M5.6 12.2 V9.4 C5.6 8.46 6.36 7.7 7.3 7.7 C8.24 7.7 9 8.46 9 9.4 V8.3 C9 7.36 9.76 6.6 10.7 6.6 C11.64 6.6 12.4 7.36 12.4 8.3 V8 C12.4 7.06 13.16 6.3 14.1 6.3 C15.04 6.3 15.8 7.06 15.8 8 V8.8 C15.8 7.86 16.56 7.1 17.5 7.1 C18.44 7.1 19.2 7.86 19.2 8.8 V14.4 C19.2 18.3 16.3 21.2 12.6 21.2 H11.8 C8.4 21.2 5.6 18.5 5.6 15.2 Z" fill="#000" fill-opacity="0.32" transform="translate(0.7 1)"/>)SVG"
               R"SVG(<path d="M5.6 12.2 V9.4 C5.6 8.46 6.36 7.7 7.3 7.7 C8.24 7.7 9 8.46 9 9.4 V8.3 C9 7.36 9.76 6.6 10.7 6.6 C11.64 6.6 12.4 7.36 12.4 8.3 V8 C12.4 7.06 13.16 6.3 14.1 6.3 C15.04 6.3 15.8 7.06 15.8 8 V8.8 C15.8 7.86 16.56 7.1 17.5 7.1 C18.44 7.1 19.2 7.86 19.2 8.8 V14.4 C19.2 18.3 16.3 21.2 12.6 21.2 H11.8 C8.4 21.2 5.6 18.5 5.6 15.2 Z" fill="#fff" stroke="#141416" stroke-width="1.35" stroke-linejoin="round"/>)SVG"
               R"SVG(<path d="M9 9.4 V11.8 M12.4 8.3 V11.6 M15.8 8 V11.8" stroke="#141416" stroke-width="1.05" stroke-linecap="round"/>)SVG"
               R"SVG(<path d="M5.9 14.6 C7.5 13.1 10 12.8 12.2 13.5" stroke="#141416" stroke-width="1.15" stroke-linecap="round"/>)SVG"
               R"SVG(</svg>)SVG";
    case NUICursorStyle::IBeam:
        // Wide white stem with a thin outline: the white has to dominate or the
        // beam reads as a faint grey line on dark surfaces.
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
               R"SVG(<path d="M8 3 H9.9 C10.75 3 11.5 3.3 12 3.8 C12.5 3.3 13.25 3 14.1 3 H16 V4.9 H14.2 C13.7 4.9 13.3 5.3 13.3 5.8 V18.2 C13.3 18.7 13.7 19.1 14.2 19.1 H16 V21 H14.1 C13.25 21 12.5 20.7 12 20.2 C11.5 20.7 10.75 21 9.9 21 H8 V19.1 H9.8 C10.3 19.1 10.7 18.7 10.7 18.2 V5.8 C10.7 5.3 10.3 4.9 9.8 4.9 H8 Z" fill="#000" fill-opacity="0.32" transform="translate(0.6 0.9)"/>)SVG"
               R"SVG(<path d="M8 3 H9.9 C10.75 3 11.5 3.3 12 3.8 C12.5 3.3 13.25 3 14.1 3 H16 V4.9 H14.2 C13.7 4.9 13.3 5.3 13.3 5.8 V18.2 C13.3 18.7 13.7 19.1 14.2 19.1 H16 V21 H14.1 C13.25 21 12.5 20.7 12 20.2 C11.5 20.7 10.75 21 9.9 21 H8 V19.1 H9.8 C10.3 19.1 10.7 18.7 10.7 18.2 V5.8 C10.7 5.3 10.3 4.9 9.8 4.9 H8 Z" fill="#fff" stroke="#141416" stroke-width="1" stroke-linejoin="round"/>)SVG"
               R"SVG(</svg>)SVG";
    case NUICursorStyle::ResizeEW:
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
               R"SVG(<path d="M2.2 12 L6.8 7.4 V10.3 H17.2 V7.4 L21.8 12 L17.2 16.6 V13.7 H6.8 V16.6 Z" fill="#000" fill-opacity="0.32" transform="translate(0.7 1)"/>)SVG"
               R"SVG(<path d="M2.2 12 L6.8 7.4 V10.3 H17.2 V7.4 L21.8 12 L17.2 16.6 V13.7 H6.8 V16.6 Z" fill="#fff" stroke="#141416" stroke-width="1.35" stroke-linejoin="round"/>)SVG"
               R"SVG(</svg>)SVG";
    case NUICursorStyle::ResizeNS:
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><g transform="rotate(90 12 12)">)SVG"
               R"SVG(<path d="M2.2 12 L6.8 7.4 V10.3 H17.2 V7.4 L21.8 12 L17.2 16.6 V13.7 H6.8 V16.6 Z" fill="#000" fill-opacity="0.32" transform="translate(1 -0.7)"/>)SVG"
               R"SVG(<path d="M2.2 12 L6.8 7.4 V10.3 H17.2 V7.4 L21.8 12 L17.2 16.6 V13.7 H6.8 V16.6 Z" fill="#fff" stroke="#141416" stroke-width="1.35" stroke-linejoin="round"/>)SVG"
               R"SVG(</g></svg>)SVG";
    case NUICursorStyle::ResizeNESW:
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><g transform="rotate(-45 12 12)">)SVG"
               R"SVG(<path d="M2.2 12 L6.8 7.4 V10.3 H17.2 V7.4 L21.8 12 L17.2 16.6 V13.7 H6.8 V16.6 Z" fill="#000" fill-opacity="0.32" transform="translate(-0.2 1.2)"/>)SVG"
               R"SVG(<path d="M2.2 12 L6.8 7.4 V10.3 H17.2 V7.4 L21.8 12 L17.2 16.6 V13.7 H6.8 V16.6 Z" fill="#fff" stroke="#141416" stroke-width="1.35" stroke-linejoin="round"/>)SVG"
               R"SVG(</g></svg>)SVG";
    case NUICursorStyle::ResizeNWSE:
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><g transform="rotate(45 12 12)">)SVG"
               R"SVG(<path d="M2.2 12 L6.8 7.4 V10.3 H17.2 V7.4 L21.8 12 L17.2 16.6 V13.7 H6.8 V16.6 Z" fill="#000" fill-opacity="0.32" transform="translate(1.2 0.2)"/>)SVG"
               R"SVG(<path d="M2.2 12 L6.8 7.4 V10.3 H17.2 V7.4 L21.8 12 L17.2 16.6 V13.7 H6.8 V16.6 Z" fill="#fff" stroke="#141416" stroke-width="1.35" stroke-linejoin="round"/>)SVG"
               R"SVG(</g></svg>)SVG";
    default:
        return nullptr;
    }
}

/** @brief Tintable clip-edge trim glyph for component-drawn cursors: an edge bar
 *  with solid arrowheads either side (currentColor follows NUIIcon::setColor). */
inline const char* nuiTrimResizeCursorSvg() {
    return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
           R"SVG(<path d="M12 4.5V19.5" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"/>)SVG"
           R"SVG(<path d="M2.6 12 L7.2 8.2 V10.9 H9.8 V13.1 H7.2 V15.8 Z M21.4 12 L16.8 8.2 V10.9 H14.2 V13.1 H16.8 V15.8 Z" fill="currentColor"/>)SVG"
           R"SVG(</svg>)SVG";
}

} // namespace AestraUI
