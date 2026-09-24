// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// SPEC 3 §3.4: the cursor names the active tool.
//
// Owner report: "when I'm using the pencil tool, I don't have a pencil or pointer to represent
// it." The piano roll did request a cursor for the pencil (Crosshair), but the app draws its own
// overlay cursor from NUICursorRegistry, Crosshair had no glyph there, and the renderer's
// hand-kept switch fell back to the arrow. This pins the registry side: every style has a glyph
// unless it is deliberately listed as unused, and every hotspot sits inside its glyph.

#include "NUICursorRegistry.h"

#include <cstring>
#include <iostream>
#include <string>

using namespace AestraUI;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

// Styles no code requests today. A style listed here draws the arrow; requesting one means
// giving it a glyph first (and removing it from this list).
bool deliberatelyGlyphless(NUICursorStyle style) {
    switch (style) {
    case NUICursorStyle::Hidden: // draws nothing by definition
    case NUICursorStyle::Wait:
    case NUICursorStyle::WaitArrow:
    case NUICursorStyle::ResizeAll:
    case NUICursorStyle::NotAllowed:
        return true;
    default:
        return false;
    }
}

void testEveryRequestableStyleHasAGlyph() {
    for (int i = 0; i < kNUICursorStyleCount; ++i) {
        const auto style = static_cast<NUICursorStyle>(i);
        if (deliberatelyGlyphless(style)) {
            continue;
        }
        check(nuiCursorSvg(style) != nullptr,
              "cursor style " + std::to_string(i) + " has a glyph (else it silently draws the arrow)");
    }
}

void testToolCursorsExist() {
    check(nuiCursorSvg(NUICursorStyle::Pencil) != nullptr, "the Draw tool has a pencil glyph");
    check(nuiCursorSvg(NUICursorStyle::Eraser) != nullptr, "the Erase tool has an eraser glyph");
    check(nuiCursorSvg(NUICursorStyle::Crosshair) != nullptr, "Crosshair has a glyph (it drew the arrow)");
}

void testHotspotsSitInsideTheirGlyphs() {
    for (int i = 0; i < kNUICursorStyleCount; ++i) {
        const auto h = nuiCursorHotspot(static_cast<NUICursorStyle>(i));
        check(h.x >= 0.0f && h.x <= 24.0f && h.y >= 0.0f && h.y <= 24.0f,
              "cursor style " + std::to_string(i) + " has its hotspot inside the 24x24 glyph");
    }
    // The pencil lands where its tip points: the glyph's outline starts at the hotspot.
    const auto pencil = nuiCursorHotspot(NUICursorStyle::Pencil);
    check(pencil.x == 3.0f && pencil.y == 21.0f, "the pencil's hotspot is its tip");
    check(std::strstr(nuiCursorSvg(NUICursorStyle::Pencil), "M3 21 ") != nullptr,
          "and the pencil outline is drawn from that tip");
}

} // namespace

int main() {
    testEveryRequestableStyleHasAGlyph();
    testToolCursorsExist();
    testHotspotsSitInsideTheirGlyphs();
    if (g_failures == 0) {
        std::cout << "Cursor registry tests passed\n";
        return 0;
    }
    std::cout << g_failures << " cursor registry test(s) failed\n";
    return 1;
}
