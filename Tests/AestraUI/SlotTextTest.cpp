// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Mixer rework slice 6: where a label sits in its slot. Pure header.

#include "SlotText.h"

#include <iostream>
#include <string>

using namespace AestraUI;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}
struct Rect { float x, y, width, height; };
struct Size { float width, height; };
struct Metrics { float ascent, descent, lineHeight, capHeight; };
struct FakeRenderer {
    Metrics m;
    float drawnLine; // what measureText() reports: the line at the size actually drawn
    Metrics getFontMetrics(float) const { return m; }
    Size measureText(const std::string&, float) { return {0.0f, drawnLine}; }
};
} // namespace

int main() {
    std::cout << "[1] the cap height is centred on the slot, whole pixels\n";
    // ascent 10, cap 7: the capitals' middle is 6.5 below the line top.
    check(SlotText::slotTextY(263.0f, 20.0f, 10.0f, 7.0f) == 267.0f, "a 7px cap in a 20px slot at 263: line top 267 (caps 273.5-6.5..)");
    check(SlotText::slotTextY(0.0f, 16.0f, 10.0f, 7.0f) == 2.0f, "16px slot: 2");
    check(SlotText::slotTextY(10.0f, 23.0f, 11.0f, 8.0f) == 15.0f, "23px pill: 15");
    check(SlotText::slotTextY(10.0f, 14.0f, 10.0f, 7.0f) == 11.0f || SlotText::slotTextY(10.0f, 14.0f, 10.0f, 7.0f) == 10.0f,
          "a 14px line slot rounds to a whole pixel");
    {
        const float y = SlotText::slotTextY(100.0f, 20.0f, 10.0f, 7.0f);
        check(y == std::round(y), "never a half pixel");
        // the caps then span [y + ascent - cap, y + ascent]: centred on the slot to within half a pixel
        const float capTop = y + 10.0f - 7.0f, capBottom = y + 10.0f;
        check(std::fabs((capTop + capBottom) * 0.5f - 110.0f) <= 0.5f, "the caps' middle is within half a pixel of the slot's");
    }
    check(SlotText::slotTextY(5.0f, 8.0f, 12.0f, 9.0f) == std::round(5.0f + 4.0f + 4.5f - 12.0f), "a line taller than the slot overhangs evenly");

    std::cout << "[2] it does not depend on what else is in the slot\n";
    check(SlotText::slotTextY(100.0f, 20.0f, 10.0f, 7.0f) - 100.0f == SlotText::slotTextY(200.0f, 20.0f, 10.0f, 7.0f) - 200.0f,
          "the offset inside the slot is the same wherever the slot is");
    check(SlotText::slotTextY(0.0f, 20.0f, 10.0f, 7.0f) < SlotText::slotTextY(0.0f, 24.0f, 10.0f, 7.0f), "a taller slot puts the line lower");

    std::cout << "[3] with a renderer's metrics\n";
    {
        FakeRenderer r{{10.0f, 3.0f, 13.0f, 7.0f}, 13.0f};
        check(SlotText::slotTextY(r, Rect{0, 263, 80, 20}, 10.0f) == 267.0f, "metrics that match what is drawn are used as they are");
        FakeRenderer floored{{10.0f, 3.0f, 13.0f, 7.0f}, 26.0f}; // drawn twice as big (a readability floor)
        check(SlotText::slotTextY(floored, Rect{0, 263, 80, 20}, 10.0f) == std::round(263.0f + 10.0f + 7.0f - 20.0f),
              "a size the renderer raises is centred at the size that is drawn");
        FakeRenderer none{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f};
        check(SlotText::slotTextY(none, Rect{0, 0, 80, 20}, 10.0f) == std::round(10.0f + 3.5f - 8.0f), "no metrics falls back to 0.8 / 0.7 of the size");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
