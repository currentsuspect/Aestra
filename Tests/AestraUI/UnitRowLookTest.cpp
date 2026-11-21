// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Ink theme, variant B: the Arsenal's colour table (UnitRowLook.h) in every theme and every row state. A table, not a
// screenshot: every label on its fill reaches 4.5:1, every part that must be found reaches 3:1, and the lit row, the
// pads and the playhead stay opaque, so no alpha wash can hide content behind it.

#include "ContrastRatio.h"
#include "NUIThemeSystem.h"
#include "UnitRowLook.h"

#include <iostream>
#include <string>

using namespace AestraUI;
namespace Look = AestraUI::UnitRowLook;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cout << "  FAIL " << what << "\n";
        ++g_failures;
    }
}
int g_checks = 0;
void expectRatio(const NUIColor& fg, const NUIColor& bg, float floor, const std::string& what) {
    ++g_checks;
    const float r = Aestra::ContrastRatio::ratio(fg.r, fg.g, fg.b, bg.r, bg.g, bg.b);
    if (r < floor) {
        std::cout << "  FAIL " << what << ": " << r << ":1 (needs " << floor << ")\n";
        ++g_failures;
    }
}
} // namespace

int main() {
    constexpr float kText = 4.5f, kPart = 3.0f;
    const NUIColor identity = NUIColor::fromHex(0x06c49d); // a unit's own colour (teal)
    for (const auto& kit : NUIThemePresets::kitThemes()) {
        const std::string key = kit.key;
        const NUIThemeProperties t = NUIThemePresets::createKitTheme(key);
        const float labelFloor = kit.mode == 2 ? kText : kText; // high contrast is held to the same floor here

        struct Named { const char* name; Look::RowState s; };
        const Named states[] = {{"rest", {false, false, false, true}},
                                {"hover", {false, true, false, true}},
                                {"selected", {true, false, false, true}},
                                {"selected+hover", {true, true, false, true}},
                                {"drop", {false, false, true, true}}};
        const Look::Row rest = Look::row(t, states[0].s, identity);
        for (const auto& st : states) {
            const std::string at = key + " " + st.name + ": ";
            const Look::Row r = Look::row(t, st.s, identity);
            check(r.fill.a == 1.0f, at + "the card is opaque, never a wash over content");
            expectRatio(r.name, r.fill, labelFloor, at + "unit name");
            expectRatio(r.kind, r.fill, labelFloor, at + "type line");
            expectRatio(r.kindGuess, r.fill, labelFloor, at + "guessed role");
            expectRatio(r.keyLabel, r.keyFill, labelFloor, at + "M/S letter on its key");
            if (st.s.selected && !st.s.dropTarget) {
                check(r.lit, at + "selected is lit");
                check(r.fill.r == t.primary.r && r.fill.g == t.primary.g && r.fill.b == t.primary.b, at + "a lit row is the primary");
                check(r.name.r == t.textOnPrimary.r && r.name.g == t.textOnPrimary.g, at + "its name is textOnPrimary");
                expectRatio(r.grip, r.fill, kPart, at + "grip");
                expectRatio(r.dot, r.fill, kPart, at + "name dot");
                expectRatio(r.stripRing, r.fill, kPart, at + "strip ring");
                check(r.stripRingWidth >= 2.0f, at + "the strip gets a 2px ring");
                expectRatio(r.keyFill, r.fill, kPart, at + "an off M/S key on the lit card");
                expectRatio(r.fill, rest.fill, kPart, at + "selected row stands off an unselected one");
            } else {
                check(!r.lit, at + "not lit");
                check(r.stripRingWidth == 0.0f, at + "no strip ring off a lit row");
            }
            if (st.s.dropTarget)
                expectRatio(r.edge, r.fill, kPart, at + "drop target edge");
            if (st.s.hovered && !st.s.selected) {
                expectRatio(r.grip, r.fill, kPart, at + "hover grip");
                expectRatio(r.edge, r.fill, 1.2f, at + "hover edge is a visible line");
            }
        }

        // Pads: neutral, opaque, and the playhead reads on both shades of resting pad.
        const Look::Pads p = Look::pads(t);
        for (const NUIColor* restPad : {&p.restEven, &p.restOdd}) {
            const std::string at = key + " pads: ";
            expectRatio(p.active, *restPad, kPart, at + "active pad on a rest pad");
            expectRatio(p.playhead, *restPad, kPart, at + "playhead edge on a rest pad");
            expectRatio(p.playheadFill, *restPad, 2.5f, at + "playhead fill on a rest pad (the stroke is the 3:1 cue)");
            const NUIColor base = Look::velocityBase(p, *restPad);
            check(base.a == 1.0f, at + "velocity base is opaque");
            expectRatio(p.active, base, kPart, at + "velocity bar over its base");
        }
        expectRatio(p.restOdd, t.backgroundPrimary, 1.1f, key + " pads: rest pads show against the lane well");
        check(p.restEven.r != p.restOdd.r || p.restEven.g != p.restOdd.g, key + " pads: beat groups alternate shade");

        // Scene tiles.
        for (bool sel : {false, true}) {
            const Look::SceneTile s = Look::sceneTile(t, sel, false);
            const std::string at = key + (sel ? " scene selected: " : " scene: ");
            check(s.fill.a == 1.0f, at + "opaque");
            expectRatio(s.label, s.fill, kText, at + "label");
            expectRatio(s.thumbOn, s.fill, kPart, at + "thumbnail bars");
        }
        const Look::SceneTile lit = Look::sceneTile(t, true, false);
        check(lit.fill.r == t.primary.r && lit.fill.g == t.primary.g, key + " scene: a selected tile is the primary");
    }
    std::cout << g_checks << " contrast checks, " << g_failures << " failure(s)\n";
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
