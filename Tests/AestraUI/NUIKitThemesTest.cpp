// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-79: the twelve Aestra Kit themes load with every token, the old theme names still resolve to Ember, and the
// depth helper draws the Design Spec's three depths with the theme's own light and shade.

#include "../Support/NullRenderer.h"
#include "NUIDepth.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace AestraUI;

namespace {

int gFailures = 0;

void check(bool condition, const std::string& label) {
    std::cout << "  " << (condition ? "PASS " : "FAIL ") << label << '\n';
    if (!condition)
        ++gFailures;
}

bool same(const NUIColor& a, const NUIColor& b) {
    return std::fabs(a.r - b.r) < 1e-4f && std::fabs(a.g - b.g) < 1e-4f && std::fabs(a.b - b.b) < 1e-4f &&
           std::fabs(a.a - b.a) < 1e-4f;
}

float luminance(const NUIColor& c) {
    const auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}

float contrast(const NUIColor& a, const NUIColor& b) {
    const float la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

struct Call {
    std::string what;
    NUIRect rect;
    float a = 0.0f, b = 0.0f;
    NUIColor color;
};

class Recorder final : public Aestra::Testing::NullRenderer {
public:
    std::vector<Call> calls;
    void fillRoundedRect(const NUIRect& r, float radius, const NUIColor& c) override {
        calls.push_back({"fill", r, radius, 0.0f, c});
    }
    void strokeRoundedRect(const NUIRect& r, float radius, float thickness, const NUIColor& c) override {
        calls.push_back({"stroke", r, radius, thickness, c});
    }
    void fillRoundedRectVertical(const NUIRect& r, float radius, const NUIColor& top, const NUIColor&) override {
        calls.push_back({"gradient", r, radius, 0.0f, top});
    }
    void drawInnerShadow(const NUIRect& r, float, float offset, float blur, const NUIColor& c, bool) override {
        calls.push_back({"inner", r, offset, blur, c});
    }
    void drawShadow(const NUIRect& r, float, float offsetY, float blur, const NUIColor& c) override {
        calls.push_back({"shadow", r, offsetY, blur, c});
    }
    int count(const std::string& what) const {
        return static_cast<int>(std::count_if(calls.begin(), calls.end(), [&](const Call& c) { return c.what == what; }));
    }
};

void testTwelveThemes() {
    std::cout << "twelve kit themes\n";
    const auto& kit = NUIThemePresets::kitThemes();
    check(kit.size() == 12, "the kit has twelve variants");
    auto& manager = NUIThemeManager::getInstance();
    for (const auto& k : kit) {
        const std::string key = k.key;
        check(manager.hasTheme(key), key + " is registered");
        const auto t = NUIThemePresets::createKitTheme(key);
        check(t.mode == k.mode, key + " knows its mode");
        check(contrast(t.textPrimary, t.backgroundPrimary) >= 7.0f, key + " primary text reads at 7:1");
        check(contrast(t.textMuted, t.backgroundSecondary) >= 4.5f, key + " muted text meets the floor on panels");
        check(t.neuLo.a > 0.0f && t.neuHi.a > 0.0f, key + " has depth light and shade");
        check(t.glassEdge.a > 0.0f, key + " has a glass edge");
        if (k.mode != 1)
            check(same(t.timelineBed, NUIColor::black()), key + " keeps the pure-black timeline bed");
        if (k.mode == 2)
            check(t.neuEdge.a > 0.0f, key + " keeps an inner edge on controls");
        else
            check(t.neuEdge.a == 0.0f, key + " has no control outline");
        check(same(t.buttonBgActive, t.primary.withAlpha(0.30f)), key + " restates legacy fields from its tokens");
    }
}

void testAliases() {
    std::cout << "old names resolve to Ember\n";
    const auto ember = NUIThemePresets::createKitTheme("ember-dark");
    check(same(NUIThemePresets::createAestraDark().primary, ember.primary), "createAestraDark is Ember dark");
    check(same(ember.primary, NUIColor::fromHex(0x8a3ff0)), "Ember dark carries the 12 Nov violet");
    auto& manager = NUIThemeManager::getInstance();
    check(manager.setActiveTheme("Aestra-dark"), "Aestra-dark still loads");
    check(same(manager.getCurrentTheme().backgroundPrimary, ember.backgroundPrimary), "Aestra-dark is Ember dark");
    check(manager.setActiveTheme("high-contrast-dark"), "high-contrast-dark still loads");
    check(manager.getCurrentTheme().mode == 2, "high-contrast-dark is Ember high contrast");
    check(same(manager.getColor("editorWell"), NUIThemePresets::createKitTheme("ember-hc").editorWell),
          "getColor reads stated editor tokens");
    check(same(NUIThemePresets::createKitTheme("no-such-theme").primary, ember.primary),
          "an unknown key falls back to Ember dark");
    manager.setActiveTheme("Aestra-dark");
}

void testDepth() {
    std::cout << "depth helper\n";
    auto& manager = NUIThemeManager::getInstance();
    manager.setActiveTheme("ember-dark");
    const auto& t = manager.getCurrentTheme();
    const NUIRect r{10.0f, 10.0f, 100.0f, 28.0f};

    Recorder bed;
    Depth::bed(bed, r, 14.0f);
    check(bed.count("fill") == 1 && same(bed.calls[0].color, t.backgroundPrimary), "a bed is backgroundPrimary");
    check(bed.count("inner") == 2, "a bed is shaded and lit from inside");
    check(bed.calls[1].a > 0.0f && same(bed.calls[1].color, t.neuLo), "shade on the top-left inner edge");
    check(bed.calls[2].a < 0.0f && same(bed.calls[2].color, t.neuHi), "highlight on the bottom-right");
    check(bed.count("stroke") == 0, "no outline on a bed in dark mode");

    Recorder obj;
    Depth::object(obj, r, 4.0f);
    check(obj.count("gradient") == 1, "an object is lit from above");
    check(obj.count("shadow") == 2, "an object casts a shadow");
    check(obj.count("stroke") == 0, "an object has no outline");
    const float restDrop = obj.calls[0].a;
    Recorder held;
    Depth::object(held, r, 4.0f, true);
    check(held.calls[0].a > restDrop, "a held object lifts further");
    const auto heldFace = std::find_if(held.calls.begin(), held.calls.end(), [](const Call& c) { return c.what == "gradient"; });
    check(heldFace != held.calls.end() && heldFace->rect.width > r.width, "a held object grows a little");

    Recorder ring;
    Depth::focusRing(ring, r, 14.0f);
    check(ring.count("stroke") == 1 && ring.calls[0].b == 2.0f, "the focus ring is 2px");
    check(std::fabs(ring.calls[0].rect.x - (r.x - 3.0f)) < 1e-4f, "the ring's stroke sits 2px off the control");
    check(same(ring.calls[0].color, t.focusRing), "the ring is focusRing");

    manager.setActiveTheme("ember-hc");
    Recorder hc;
    Depth::bed(hc, r, 14.0f);
    check(hc.count("stroke") == 1, "high contrast keeps an inner edge on a bed");
    manager.setActiveTheme("Aestra-dark");
}

} // namespace

int main() {
    testTwelveThemes();
    testAliases();
    testDepth();
    std::cout << (gFailures == 0 ? "ALL PASS\n" : "FAILURES: " + std::to_string(gFailures) + "\n");
    return gFailures == 0 ? 0 : 1;
}
