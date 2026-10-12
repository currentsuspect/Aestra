// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-27 point 1, "patterns as one-click scenes": every MIDI pattern is a chip in id order, the active one is
// kept in view when they overflow, a click names the scene, and "+" gets the next free "Pattern N".

#include "ArsenalSceneStrip.h"

#include <iostream>
#include <string>

using namespace Aestra;
using Audio::MidiPayload;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
} // namespace

int main() {
    std::cout << "[1] chips fit: no scrolling, in order\n";
    float off = -1.0f;
    auto p = ArsenalSceneStrip::place({60, 80, 60}, 0, 600, 0, true, off);
    check(p.size() == 3 && off == 0.0f, "three chips, offset 0");
    check(p[1].x == 60 + ArsenalSceneStrip::kGap, "second chip sits one gap after the first");

    std::cout << "[2] overflow keeps the active chip in view and clamps the scroll\n";
    const std::vector<float> w(10, 80.0f);
    p = ArsenalSceneStrip::place(w, 9, 300, 0, true, off);
    check(off > 0.0f, "scrolled toward the last chip");
    check(p[9].x + p[9].w <= 300 - ArsenalSceneStrip::kAddW + 0.01f, "last chip ends before the + chip");
    p = ArsenalSceneStrip::place(w, 0, 300, 5000, false, off);
    float total = 10 * 80.0f + 9 * ArsenalSceneStrip::kGap;
    check(off <= total, "manual scroll is clamped");
    p = ArsenalSceneStrip::place(w, 0, 300, -50, false, off);
    check(off == 0.0f, "never scrolls below zero");

    std::cout << "[3] widths and names\n";
    check(ArsenalSceneStrip::chipWidth("A") == 56.0f, "short name hits the minimum width");
    check(ArsenalSceneStrip::chipWidth(std::string(60, 'x')) == 140.0f, "long name is capped");
    check(ArsenalSceneStrip::nextName({{1, "Pattern 1"}, {2, "Pattern 2"}}) == "Pattern 3", "next name counts on");
    check(ArsenalSceneStrip::nextName({{1, "Pattern 2"}}) == "Pattern 3", "next name skips one that is taken");

    std::cout << "[4] the strip reads patterns, ignores non-MIDI, and answers clicks\n";
    Audio::PatternManager pm;
    const auto a = pm.createMidiPattern("Verse", 8.0, MidiPayload{});
    const auto b = pm.createMidiPattern("Chorus", 8.0, MidiPayload{});
    ArsenalSceneStrip strip;
    const AestraUI::NUIRect rect{10, 20, 400, ArsenalSceneStrip::kHeight};
    strip.refresh(pm, b.value, rect);
    check(strip.scenes().size() == 2, "two scenes");
    check(strip.scenes()[0].name == "Verse" && strip.scenes()[1].name == "Chorus", "in creation order");
    const auto r0 = strip.chipRect(strip.placed()[0]);
    auto hit = strip.hitTest({r0.x + 4, r0.y + 4});
    check(hit.kind == ArsenalSceneStrip::Hit::Kind::Scene && hit.id == a.value, "clicking the first chip names that pattern");
    hit = strip.hitTest({strip.addRect().x + 2, strip.addRect().y + 2});
    check(hit.kind == ArsenalSceneStrip::Hit::Kind::Add, "clicking + asks for a new scene");
    hit = strip.hitTest({0, 0});
    check(hit.kind == ArsenalSceneStrip::Hit::Kind::None, "outside the strip is nothing");

    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
