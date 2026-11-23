// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Selection System Spec, section 2.2: SelectionLook.h in every kit theme and every state. A table, not a screenshot:
// a label on a selected fill reaches 4.5:1; on the Ink themes the lit fill reaches 3:1 on the bed and on a card (the
// rule that makes selection a mark you can find); every fill is opaque; the hover state is distinct from the selected
// one; and an item that is not selected carries no look at all.

#include "ContrastRatio.h"
#include "NUIThemeSystem.h"
#include "SelectionLook.h"

#include <iostream>
#include <string>

using namespace AestraUI;

namespace {
int g_failures = 0;
int g_checks = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::cout << "  FAIL " << what << "\n";
        ++g_failures;
    }
}
float ratio(const NUIColor& a, const NUIColor& b) {
    return Aestra::ContrastRatio::ratio(a.r, a.g, a.b, b.r, b.g, b.b);
}
void expectRatio(const NUIColor& fg, const NUIColor& bg, float floor, const std::string& what) {
    ++g_checks;
    const float r = ratio(fg, bg);
    if (r < floor) {
        std::cout << "  FAIL " << what << ": " << r << ":1 (needs " << floor << ")\n";
        ++g_failures;
    }
}
bool same(const NUIColor& a, const NUIColor& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }
SelectionState state(int bits) {
    SelectionState s;
    s.selected = bits & 1;
    s.hovered = bits & 2;
    s.pressed = bits & 4;
    s.disabled = bits & 8;
    s.keyOwner = bits & 16;
    s.multi = bits & 32;
    return s;
}
} // namespace

int main() {
    constexpr float kText = 4.5f, kPart = 3.0f;
    int themes = 0;
    for (const auto& kit : NUIThemePresets::kitThemes()) {
        ++themes;
        const std::string key = kit.key;
        const NUIThemeProperties t = NUIThemePresets::createKitTheme(key);
        const bool ink = key.rfind("ink-", 0) == 0;
        // The selected fill is flattened over the card, which is what SelectionLook does for a translucent token.
        const NUIColor lit = selectionLook(t, state(1)).fill;

        for (int bits = 0; bits < 64; ++bits) {
            const SelectionState s = state(bits);
            const SelectionColors c = selectionLook(t, s);
            const std::string at = key + " [sel=" + std::to_string(s.selected) + " hov=" + std::to_string(s.hovered) +
                                   " press=" + std::to_string(s.pressed) + " dis=" + std::to_string(s.disabled) +
                                   " key=" + std::to_string(s.keyOwner) + " multi=" + std::to_string(s.multi) + "]: ";

            check(c.fill.a == 0.0f || c.fill.a == 1.0f, at + "the fill is opaque or absent, never a wash");
            check(c.hoverFill.a == 1.0f, at + "the hover fill is opaque");
            check(s.keyOwner ? c.rail.a == 1.0f : c.rail.a == 0.0f, at + "the key-owner tick shows exactly for the key owner");
            if (s.keyOwner) {
                expectRatio(c.rail, t.backgroundPrimary, kText, at + "the tick reads on the bed");
                expectRatio(c.rail, t.backgroundSecondary, kText, at + "the tick reads on a card");
            }

            if (!s.selected) {
                check(c.text.a == 0.0f && c.edge.a == 0.0f && c.handle.a == 0.0f,
                      at + "an item that is not selected has no text, edge or handle look");
                const bool wantsFill = !s.disabled && (s.hovered || s.pressed);
                check((c.fill.a == 1.0f) == wantsFill, at + "an unselected item is filled only under the pointer or a press");
                continue;
            }

            check(c.fill.a == 1.0f, at + "a selected item always has a fill");
            if (s.multi) check(c.edge.a == 1.0f, at + "members of a multi-selection are outlined");
            // The mark is the fill or, where the fill alone is not 3:1 on the bed and a card, the edge.
            if (!s.disabled && !s.hovered && !s.pressed) {
                const bool fillMark = ratio(c.fill, t.backgroundPrimary) >= kPart && ratio(c.fill, t.backgroundSecondary) >= kPart;
                const bool edgeMark = c.edge.a == 1.0f && ratio(c.edge, t.backgroundPrimary) >= kPart &&
                                      ratio(c.edge, t.backgroundSecondary) >= kPart;
                if (ink) check(fillMark || edgeMark, at + "a selected item is a 3:1 mark by its fill or its edge");
                check(!(fillMark && !s.multi) || c.edge.a == 0.0f, at + "a single item whose fill is already a mark needs no outline");
            }
            if (!s.disabled) {
                expectRatio(c.text, c.fill, kText, at + "the label on the selected fill");
                expectRatio(c.handle, c.fill, kText, at + "the handle on the selected fill");
            }
            if (s.hovered && !s.pressed && !s.disabled)
                check(!same(c.fill, lit), at + "hover is distinct from the selected state");
        }

        // The Ink rule (spec rule 3): the lit fill is a mark you can find on the bed, on a card and, in dark, on the
        // outer bed. The legacy kits keep their own, softer selection and are held to readability only.
        if (ink && kit.mode != 2) {
            expectRatio(lit, t.backgroundPrimary, kPart, key + ": the selected fill on the bed");
            expectRatio(lit, t.backgroundSecondary, kPart, key + ": the selected fill on a card");
            if (kit.mode == 0) expectRatio(lit, t.timelineBed, kPart, key + ": the selected fill on the outer bed");
        }
        if (ink && kit.mode == 2) { // high contrast: translucent fill, strong edge; the edge is the mark
            const NUIColor edge = selectionLook(t, state(1)).edge;
            check(edge.a == 1.0f, key + ": the high-contrast kit outlines a single selected item");
            expectRatio(edge, t.backgroundPrimary, kPart, key + ": the high-contrast edge on the bed");
            expectRatio(edge, t.backgroundSecondary, kPart, key + ": the high-contrast edge on a card");
        }

        // Pressing a selected item and hovering an unselected one each have a look of their own.
        check(!same(selectionLook(t, state(1 | 4)).fill, lit), key + ": a pressed selected item differs from a resting one");
        check(!same(selectionLook(t, state(2)).fill, selectionLook(t, state(4)).fill),
              key + ": hovering and pressing an unselected item look different");
    }
    check(themes == 15, "the kit has fifteen variants");
    std::cout << "SelectionLookTest: " << g_checks << " checks, " << g_failures << " failure(s)\n";
    if (g_failures == 0) std::cout << "ALL PASS\n";
    return g_failures == 0 ? 0 : 1;
}
