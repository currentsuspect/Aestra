// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The transport's pills (TransportPillLook.h): one height, one inset, a glyph at the centre of its cell, a thumb with
// the same space on all four sides. Headless numbers; the real-renderer pixel measurement of the glyphs is in the
// screenshots the change was judged from.

#include "TransportPillLook.h"

#include <cmath>
#include <iostream>
#include <string>

namespace TP = Aestra::TransportPill;
using AestraUI::NUIRect;

namespace {
int g_failures = 0, g_checks = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::cout << "  FAIL " << what << "\n";
        ++g_failures;
    }
}
} // namespace

int main() {
    // A glyph is centred in its cell within half a pixel, for every cell width and glyph size the transport uses, at
    // fractional positions too.
    for (const float size : {13.0f, 15.0f, 18.0f}) {
        for (float w = 24.0f; w <= 120.0f; w += 1.0f) {
            for (const float x : {0.0f, 0.25f, 0.5f, 0.75f, 301.0f, 301.5f}) {
                const NUIRect cell(x, 10.5f, w, TP::kHeight);
                const NUIRect g = TP::centredIcon(cell, size);
                const float dx = (g.x + g.width * 0.5f) - (cell.x + cell.width * 0.5f);
                const float dy = (g.y + g.height * 0.5f) - (cell.y + cell.height * 0.5f);
                check(std::fabs(dx) <= 0.5f + 1e-4f && std::fabs(dy) <= 0.5f + 1e-4f,
                      "glyph " + std::to_string(size) + " in a " + std::to_string(w) + " px cell is centred within 0.5 px");
                check(std::fabs(g.x - std::round(g.x)) < 1e-4f && std::fabs(g.y - std::round(g.y)) < 1e-4f, "and sits on whole pixels");
            }
        }
    }
    // The thumb has the same space to the bed on every side, for a chip and for a panel cell.
    for (const NUIRect pill : {NUIRect(10, 20, 28, TP::kHeight), NUIRect(50, 20, 84, TP::kHeight), NUIRect(0, 0, 110, TP::kHeight)}) {
        const NUIRect t = TP::thumb(pill);
        check(std::fabs((t.x - pill.x) - TP::kInset) < 1e-4f && std::fabs((pill.right() - t.right()) - TP::kInset) < 1e-4f &&
                  std::fabs((t.y - pill.y) - TP::kInset) < 1e-4f && std::fabs((pill.bottom() - t.bottom()) - TP::kInset) < 1e-4f,
              "the thumb is inset equally on all four sides");
        check(std::fabs(TP::radius(t) - t.height * 0.5f) < 1e-4f && std::fabs(TP::radius(pill) - pill.height * 0.5f) < 1e-4f,
              "pill and thumb are capsules");
    }
    // A chip's content is centred: the same space to the left and right of the lamp + word, whatever the chip's width.
    for (const float chipW : {60.0f, 84.0f, 97.5f, 120.0f}) {
        for (const float content : {20.0f, 41.3f, 60.0f}) {
            const NUIRect pill(317.5f, 40.0f, chipW, TP::kHeight);
            const float x = TP::groupStartX(pill, content);
            const float left = x - pill.x, right = pill.right() - (x + content);
            // whole pixels: the two sides may differ by one
            check(std::fabs(left - right) <= 1.0f + 1e-4f, "content " + std::to_string(content) + " in a " + std::to_string(chipW) + " px chip has equal space either side");
        }
    }
    // The group bed spans the row exactly, and keeps the pill height.
    const NUIRect a(100, 30, 28, TP::kHeight), c(100 + 2 * 32, 30, 28, TP::kHeight);
    const NUIRect bed = TP::groupBed(a, c);
    check(bed.x == a.x && bed.right() == c.right() && bed.height == TP::kHeight, "the Panels bed covers the three cells and is one pill high");
    check(TP::kHeight == 24.0f && TP::kInset == 3.0f, "pill height 24, inset 3 (the view switcher's pad)");

    std::cout << g_checks << " checks, " << g_failures << " failure(s)\n";
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
