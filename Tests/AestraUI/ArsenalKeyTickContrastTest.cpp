// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The key-owner tick (Ember, 3 x 10 px) is a non-text cue, so it needs 3:1 against the bar it sits on. Checks the helper
// against known ratios, then the tick colour (accentPrimary) against the real Arsenal title bar and the real piano roll
// header, as WindowPanel::onRender draws them.

#include "ContrastRatio.h"
#include "NUIThemeSystem.h"

#include <iostream>
#include <string>
#include <vector>

using namespace AestraUI;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}

float ratio(const NUIColor& a, const NUIColor& b) { return Aestra::ContrastRatio::ratio(a.r, a.g, a.b, b.r, b.g, b.b); }

// colour `top` at alpha over opaque `under`
NUIColor over(const NUIColor& top, float alpha, const NUIColor& under) {
    return {top.r * alpha + under.r * (1.0f - alpha), top.g * alpha + under.g * (1.0f - alpha), top.b * alpha + under.b * (1.0f - alpha), 1.0f};
}
} // namespace

int main() {
    std::cout << "[1] the helper\n";
    check(std::abs(Aestra::ContrastRatio::ratio(1, 1, 1, 0, 0, 0) - 21.0f) < 0.01f, "white on black is 21:1");
    check(std::abs(Aestra::ContrastRatio::ratio(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f) - 1.0f) < 0.001f, "a colour on itself is 1:1");
    check(std::abs(Aestra::ContrastRatio::ratio(0, 0, 0, 1, 1, 1) - 21.0f) < 0.01f, "the order does not matter");
    check(std::abs(Aestra::ContrastRatio::ratio(0.467f, 0.467f, 0.467f, 1, 1, 1) - 4.48f) < 0.05f, "#777 on white is about 4.48:1 (WCAG reference)");

    std::cout << "[2] the tick on the real bars\n";
    auto& themes = NUIThemeManager::getInstance();
    std::vector<std::string> names;
    for (const auto& kit : NUIThemePresets::kitThemes()) names.push_back(kit.key);
    for (const std::string& name : names) {
        themes.setActiveTheme(name);
        const NUIColor tick = themes.getColor("accentPrimary");
        // Arsenal: WindowPanel draws the title bar as the opaque card (backgroundSecondary); it was 98% over the body.
        const NUIColor arsenalBar = themes.getColor("backgroundSecondary");
        // Piano roll: full-window mode fills its header with backgroundSecondary.
        const NUIColor pianoBar = themes.getColor("backgroundSecondary");
        const float ra = ratio(tick, arsenalBar), rp = ratio(tick, pianoBar);
        std::cout << "  " << name << ": tick on Arsenal title bar " << ra << ":1, on piano roll header " << rp << ":1\n";
        check(ra >= Aestra::ContrastRatio::kNonTextMinimum, name + ": the tick reads on the Arsenal title bar (>= 3:1)");
        check(rp >= Aestra::ContrastRatio::kNonTextMinimum, name + ": the tick reads on the piano roll header (>= 3:1)");
    }
    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
