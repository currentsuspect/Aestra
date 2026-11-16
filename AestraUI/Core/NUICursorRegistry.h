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
//   Arrow (2, 2) · Hand fingertip (9, 2) · Pencil tip (3, 21) · Eraser edge (6, 19) ·
//   everything else centred at (12, 12). nuiCursorHotspot() is the table the renderer reads.

#pragma once

#include "../Platform/NUICursorStyle.h"

#include <string>

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
        // Layered: one black silhouette under one white one, so the outline is even all round and the
        // finger gaps are drawn, not stitched (owner, 2026-11-16: the old hands looked lumpy).
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
               R"SVG(<g transform="translate(0.7 1)" opacity="0.32" fill="#000" stroke="#000" stroke-linecap="round" stroke-linejoin="round"><g stroke-width="5.4" fill="none"><path d="M8.5 7.8 V13"/><path d="M11.5 6.2 V13"/><path d="M14.5 6.6 V13"/><path d="M17.5 8.6 V13"/></g>)SVG"
               R"SVG(<path d="M7 12 H19 V14.6 C19 18.4 16.4 21 13 21 C10.6 21 8.8 20 7.6 18.2 L7 17 Z" stroke-width="2.4"/><path d="M7.6 16.4 L4.9 12.4" stroke-width="5.4" fill="none"/></g>)SVG"
               R"SVG(<g fill="none" stroke="#141416" stroke-linecap="round" stroke-linejoin="round"><g stroke-width="5.4"><path d="M8.5 7.8 V13"/><path d="M11.5 6.2 V13"/><path d="M14.5 6.6 V13"/><path d="M17.5 8.6 V13"/></g>)SVG"
               R"SVG(<path d="M7 12 H19 V14.6 C19 18.4 16.4 21 13 21 C10.6 21 8.8 20 7.6 18.2 L7 17 Z" fill="#141416" stroke-width="2.4"/><path d="M7.6 16.4 L4.9 12.4" stroke-width="5.4" fill="none"/></g>)SVG"
               R"SVG(<g fill="none" stroke="#fff" stroke-linecap="round" stroke-linejoin="round"><g stroke-width="3"><path d="M8.5 7.8 V13"/><path d="M11.5 6.2 V13"/><path d="M14.5 6.6 V13"/><path d="M17.5 8.6 V13"/></g>)SVG"
               R"SVG(<path d="M7 12 H19 V14.6 C19 18.4 16.4 21 13 21 C10.6 21 8.8 20 7.6 18.2 L7 17 Z" fill="#fff" stroke-width="0.01"/><path d="M7.6 16.4 L4.9 12.4" stroke-width="3" fill="none"/></g>)SVG"
               R"SVG(<path d="M10 9 V11.4 M13 9 V11.4 M16 9.6 V11.4" stroke="#141416" stroke-width="0.9" stroke-linecap="round" fill="none"/>)SVG"
               R"SVG(</svg>)SVG";
    case NUICursorStyle::Grabbing:
        // The same hand closed: knuckles up, thumb tucked.
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
               R"SVG(<g transform="translate(0.7 1)" opacity="0.32" fill="#000" stroke="#000" stroke-linecap="round" stroke-linejoin="round"><g stroke-width="5.4" fill="none"><path d="M8.5 9.4 V10.4"/><path d="M11.5 8.6 V10.4"/><path d="M14.5 8.8 V10.4"/><path d="M17.5 9.8 V10.4"/></g>)SVG"
               R"SVG(<path d="M7 11 H19 V14.4 C19 18.4 16.4 21 13 21 H12.4 C9.4 21 7 18.6 7 15.4 Z" stroke-width="2.4"/><path d="M9.2 15 H13.4" stroke-width="5.4" fill="none"/></g>)SVG"
               R"SVG(<g fill="none" stroke="#141416" stroke-linecap="round" stroke-linejoin="round"><g stroke-width="5.4"><path d="M8.5 9.4 V10.4"/><path d="M11.5 8.6 V10.4"/><path d="M14.5 8.8 V10.4"/><path d="M17.5 9.8 V10.4"/></g>)SVG"
               R"SVG(<path d="M7 11 H19 V14.4 C19 18.4 16.4 21 13 21 H12.4 C9.4 21 7 18.6 7 15.4 Z" fill="#141416" stroke-width="2.4"/><path d="M9.2 15 H13.4" stroke-width="5.4" fill="none"/></g>)SVG"
               R"SVG(<g fill="none" stroke="#fff" stroke-linecap="round" stroke-linejoin="round"><g stroke-width="3"><path d="M8.5 9.4 V10.4"/><path d="M11.5 8.6 V10.4"/><path d="M14.5 8.8 V10.4"/><path d="M17.5 9.8 V10.4"/></g>)SVG"
               R"SVG(<path d="M7 11 H19 V14.4 C19 18.4 16.4 21 13 21 H12.4 C9.4 21 7 18.6 7 15.4 Z" fill="#fff" stroke-width="0.01"/><path d="M9.2 15 H13.4" stroke-width="3" fill="none"/></g>)SVG"
               R"SVG(<path d="M10 9.8 V11.2 M13 9.6 V11.2 M16 10 V11.2" stroke="#141416" stroke-width="0.9" stroke-linecap="round" fill="none"/>)SVG"
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
    case NUICursorStyle::Crosshair:
        // A plus with an open centre, so the exact point stays visible. It had no glyph, so
        // the overlay renderer drew the plain arrow for it (the pencil "had no cursor").
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
               R"SVG(<path d="M12 3 V9.3 M12 14.7 V21 M3 12 H9.3 M14.7 12 H21" stroke="#141416" stroke-width="3.6" stroke-linecap="round"/>)SVG"
               R"SVG(<path d="M12 3 V9.3 M12 14.7 V21 M3 12 H9.3 M14.7 12 H21" stroke="#fff" stroke-width="1.8" stroke-linecap="round"/>)SVG"
               R"SVG(</svg>)SVG";
    case NUICursorStyle::Pencil:
        // Tip at (3, 21): the note lands where the tip points.
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
               R"SVG(<path d="M3 21 L5 15.4 L16.2 4.2 C16.9 3.5 18 3.5 18.7 4.2 L19.8 5.3 C20.5 6 20.5 7.1 19.8 7.8 L8.6 19 Z" fill="#000" fill-opacity="0.32" transform="translate(0.7 1)"/>)SVG"
               R"SVG(<path d="M3 21 L5 15.4 L16.2 4.2 C16.9 3.5 18 3.5 18.7 4.2 L19.8 5.3 C20.5 6 20.5 7.1 19.8 7.8 L8.6 19 Z" fill="#fff" stroke="#141416" stroke-width="1.35" stroke-linejoin="round"/>)SVG"
               R"SVG(<path d="M5 15.4 L8.6 19 M14.4 6 L18 9.6" stroke="#141416" stroke-width="1.1" stroke-linecap="round"/>)SVG"
               R"SVG(<path d="M3 21 L4.1 17.9 L6.1 19.9 Z" fill="#141416"/>)SVG"
               R"SVG(</svg>)SVG";
    case NUICursorStyle::Eraser:
        // A slanted block; the darker band is the working end, centred on the hotspot (6, 19).
        return R"SVG(<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)SVG"
               R"SVG(<path d="M3 16 L12 7 L18.5 13.5 L9.5 22.5 Z" fill="#000" fill-opacity="0.32" transform="translate(0.7 1)"/>)SVG"
               R"SVG(<path d="M3 16 L12 7 L18.5 13.5 L9.5 22.5 Z" fill="#fff" stroke="#141416" stroke-width="1.35" stroke-linejoin="round"/>)SVG"
               R"SVG(<path d="M3 16 L7.6 11.4 L14.1 17.9 L9.5 22.5 Z" fill="#141416" fill-opacity="0.55"/>)SVG"
               R"SVG(</svg>)SVG";
    default:
        return nullptr;
    }
}

/**
 * @brief The glyph for the surface it is drawn over: on dark surfaces fill and outline swap.
 *
 * Every glyph is a white fill with a thin near-black outline. On light surfaces the outline
 * carries the silhouette and the cursor reads crisply. On dark surfaces that outline vanishes into
 * the background, leaving only the white fill: a shape one outline narrower each side, which reads
 * small and is harder to aim (owner, 2026-09-24). Swapping the two keeps the outline, the part that
 * contrasts with the surface, as the silhouette in both themes. The drop shadow is unchanged.
 */
inline std::string nuiCursorSvgForSurface(NUICursorStyle style, bool darkSurface) {
    const char* svg = nuiCursorSvg(style);
    if (svg == nullptr) {
        return {};
    }
    std::string out(svg);
    if (!darkSurface) {
        return out;
    }
    // Exact quoted tokens only, via a placeholder, so no other colour can be touched.
    const auto replaceAll = [&out](const std::string& from, const std::string& to) {
        for (size_t pos = out.find(from); pos != std::string::npos; pos = out.find(from, pos + to.size())) {
            out.replace(pos, from.size(), to);
        }
    };
    replaceAll("\"#fff\"", "\"@INK@\"");
    replaceAll("\"#141416\"", "\"#fff\"");
    replaceAll("\"@INK@\"", "\"#141416\"");
    return out;
}

/** @brief Where the click lands inside a glyph, in its 24x24 viewBox. The overlay renderer
 *  offsets each glyph by this so the hotspot sits on the pointer. */
struct NUICursorHotspot {
    float x;
    float y;
};

inline NUICursorHotspot nuiCursorHotspot(NUICursorStyle style) {
    switch (style) {
    case NUICursorStyle::Arrow:
        return {2.0f, 2.0f};
    case NUICursorStyle::Hand:
        return {9.0f, 2.0f};
    case NUICursorStyle::Pencil:
        return {3.0f, 21.0f};
    case NUICursorStyle::Eraser:
        return {6.0f, 19.0f};
    default:
        return {12.0f, 12.0f};
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
