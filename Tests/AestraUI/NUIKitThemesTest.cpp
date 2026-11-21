// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-79: the Aestra Kit themes load with every token, the old theme names resolve to Ink (the default since the
// Ink theme, 21 Nov 2026; Ember stays in the menu), and the depth helper draws the Design Spec's three depths with
// the theme's own light and shade.

#include "../Support/NullRenderer.h"
#include "NUIDepth.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
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

void testAllKitThemes() {
    std::cout << "fifteen kit themes\n";
    const auto& kit = NUIThemePresets::kitThemes();
    check(kit.size() == 15, "the kit has fifteen variants (Ink, Ember, Obsidian Moss, Solace, Monolith)");
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
            check(same(t.timelineBed, key == "ink-dark" ? NUIColor::fromHex(0x050709) : NUIColor::black()), key + " keeps its outer bed (pure black, or Ink dark's slate near-black)");
        if (k.mode == 2)
            check(t.neuEdge.a > 0.0f, key + " keeps an inner edge on controls");
        else
            check(t.neuEdge.a == 0.0f, key + " has no control outline");
        check(same(t.buttonBgActive, t.primary.withAlpha(0.30f)), key + " restates legacy fields from its tokens");
    }
}

void testInk() {
    std::cout << "Ink\n";
    const auto dark = NUIThemePresets::createKitTheme("ink-dark");
    const auto light = NUIThemePresets::createKitTheme("ink-light");
    const auto hc = NUIThemePresets::createKitTheme("ink-hc");
    const auto hex = [](unsigned v) { return NUIColor::fromHex(v); };
    int inkRows = 0;
    for (const auto& k : NUIThemePresets::kitThemes())
        if (std::string(k.key).rfind("ink-", 0) == 0) {
            ++inkRows;
            check(std::string(k.name) == "Ink", std::string(k.key) + " is named simply Ink");
        }
    check(inkRows == 3, "Ink has dark, light and high contrast");
    check(same(dark.timelineBed, hex(0x050709)) && same(dark.editorWell, hex(0x050709)) &&
              same(dark.meterBackground, hex(0x050709)),
          "Ink dark: the outer bed, editor well and meter background are the slate near-black (owner pick S)");
    check(same(dark.backgroundPrimary, hex(0x151a1f)) && same(dark.backgroundSecondary, hex(0x1b2025)) &&
              same(dark.surfaceTertiary, hex(0x272c32)) && same(dark.surfaceRaised, hex(0x32373d)),
          "Ink dark surfaces are the slate charcoal ladder: panel, card, control, raised");
    check(same(dark.mixerStripBg, hex(0x1b2025)) && same(dark.editorCard, hex(0x1b2025)) &&
              same(dark.editorControl, hex(0x151a1f)),
          "Ink dark mixer strip, editor card and control");
    check(same(dark.borderSubtle, hex(0x32373d)) && same(dark.border, hex(0x4c535a)) &&
              same(dark.borderStrong, hex(0x727a81)) && same(dark.controlEdge, hex(0x727a81)),
          "Ink dark borders are tinted slate");
    {
        // The whole family is tinted: every surface, border, wash and shade has the same cool hue and a little chroma, so no
        // neutral grey is left in it (the old #1e1e1e / #303030 read as dirty). A neutral grey fails the same check.
        const auto tinted = [](const NUIColor& c) {
            const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
            return (mx - mn) * 255.0f >= 3.0f && c.b >= c.r && c.b >= c.g - 1e-4f && c.b > c.r; // blue-leaning, not grey
        };
        const std::pair<const char*, NUIColor> family[] = {
            {"timelineBed", dark.timelineBed}, {"backgroundPrimary", dark.backgroundPrimary}, {"backgroundSecondary", dark.backgroundSecondary},
            {"surfaceTertiary", dark.surfaceTertiary}, {"surfaceRaised", dark.surfaceRaised}, {"editorCard", dark.editorCard},
            {"editorWell", dark.editorWell}, {"editorControl", dark.editorControl}, {"meterBackground", dark.meterBackground},
            {"mixerStripBg", dark.mixerStripBg}, {"border", dark.border}, {"borderSubtle", dark.borderSubtle},
            {"borderStrong", dark.borderStrong}, {"divider", dark.divider}, {"controlEdge", dark.controlEdge},
            {"hover", dark.hover}, {"pressed", dark.pressed}, {"gridMajor", dark.gridMajor}, {"gridMinor", dark.gridMinor},
            {"overlay", dark.overlay}, {"shadow", dark.shadow}, {"neuHi", dark.neuHi}, {"neuLo", dark.neuLo},
            {"glassHi", dark.glassHi}, {"glassShade", dark.glassShade}};
        for (const auto& [name, col] : family) check(tinted(col), std::string("Ink dark ") + name + " is slate-tinted, not a neutral grey");
        check(!tinted(NUIColor::fromHex(0x1e1e1e)) && !tinted(NUIColor::fromHex(0x303030)), "(negative control) the old neutral greys fail the check");
        // The steps: outer < panel < card < control < raised, each at least 1.07, panel to card the compromise that keeps
        // the selected violet 3:1 on a card; outer to panel and control steps are 1.15 or more.
        check(contrast(dark.backgroundPrimary, dark.timelineBed) >= 1.15f, "outer to panel is at least 1.15:1");
        check(contrast(dark.backgroundSecondary, dark.backgroundPrimary) >= 1.065f, "panel to card is 1.07:1 (the 3:1 selected-on-card compromise)");
        check(contrast(dark.surfaceTertiary, dark.backgroundSecondary) >= 1.15f && contrast(dark.surfaceRaised, dark.surfaceTertiary) >= 1.15f,
              "card to control and control to raised are at least 1.15:1");
        check(contrast(dark.selected, dark.backgroundSecondary) >= 3.0f && contrast(dark.selected, dark.backgroundPrimary) >= 3.0f &&
                  contrast(dark.selected, dark.timelineBed) >= 3.0f, "the selected violet is 3:1 on the card, the panel and the outer bed");
        for (const NUIColor* s : {&dark.timelineBed, &dark.backgroundPrimary, &dark.backgroundSecondary, &dark.surfaceTertiary, &dark.surfaceRaised})
            check(contrast(dark.textPrimary, *s) >= 4.5f && contrast(dark.textSecondary, *s) >= 4.5f, "primary and secondary text read on every level");
        check(contrast(dark.textMuted, dark.timelineBed) >= 4.5f && contrast(dark.textMuted, dark.backgroundPrimary) >= 4.5f &&
                  contrast(dark.textMuted, dark.backgroundSecondary) >= 4.5f, "muted text reads on the outer bed, the panel and the card");
        check(contrast(dark.accentText, dark.timelineBed) >= 4.5f && contrast(dark.accentText, dark.backgroundSecondary) >= 4.5f, "accent text reads on the outer bed and the card");
    }
    check(same(dark.textPrimary, hex(0xf5f5f5)) && same(dark.textSecondary, hex(0xc7c7c7)) &&
              same(dark.textMuted, hex(0x9e9e9e)) && same(dark.textDisabled, hex(0x717171)),
          "Ink dark text greys");
    check(same(dark.primary, hex(0x764bdf)) && same(dark.primaryHover, hex(0x8a66ea)) &&
              same(dark.primaryPressed, hex(0x6538c5)) && same(dark.textOnPrimary, hex(0xffffff)),
          "Ink dark accent (option C: deep violet, white labels)");
    check(contrast(dark.primaryHover, dark.primary) > 1.0f && contrast(dark.primaryPressed, dark.backgroundPrimary) > 1.0f &&
              (dark.primaryHover.r + dark.primaryHover.g + dark.primaryHover.b) >
                  (dark.primary.r + dark.primary.g + dark.primary.b) &&
              (dark.primaryPressed.r + dark.primaryPressed.g + dark.primaryPressed.b) <
                  (dark.primary.r + dark.primary.g + dark.primary.b),
          "Ink dark hover is lighter and pressed is darker than the primary");
    check(contrast(dark.primary, dark.backgroundPrimary) >= 3.0f && contrast(dark.primary, dark.backgroundSecondary) >= 3.0f,
          "the selected fill is a 3:1 mark on the bed and on a card");
    check(contrast(dark.accentText, dark.backgroundPrimary) >= 4.5f && contrast(dark.accentText, dark.backgroundSecondary) >= 4.5f,
          "violet used as text (accentText) reads 4.5:1 on the bed and on a card");
    check(same(dark.focusRing, dark.primary), "the focus ring follows the primary");
    check(same(dark.selected, dark.primary) && dark.selected.a == 1.0f,
          "selected is the solid deep-violet primary, no alpha");
    check(same(dark.focusRing, dark.primary) && dark.focusRing.a == 1.0f, "the focus ring is opaque");
    check(contrast(dark.textOnPrimary, dark.selected) >= 4.5f, "textOnPrimary reads on the selected fill");
    check(same(dark.success, hex(0x3fc168)) && same(dark.warning, hex(0xfcab00)) && same(dark.error, hex(0xfd7468)) &&
              same(dark.meterSafe, hex(0x00deaa)) && same(dark.textOnStatus, hex(0x0a0a0a)),
          "Ink dark status colours");
    check(std::fabs(dark.hover.a - 15.0f / 255.0f) < 1e-3f,
          "the hover overlay stays an alpha for now");
    check(same(light.backgroundPrimary, hex(0xede9e1)) && same(light.backgroundSecondary, hex(0xfffbf4)) &&
              same(light.timelineBed, hex(0xf2f0eb)) && same(light.primary, hex(0x764bdf)) &&
              same(light.textOnPrimary, hex(0xffffff)),
          "Ink light is Paper's surfaces under Ink's accent");
    check(same(light.selected, light.primary) && contrast(light.textOnPrimary, light.selected) >= 4.5f,
          "Ink light selection is solid and its label reads");
    check(contrast(light.borderStrong, light.backgroundSecondary) >= 3.0f &&
              contrast(light.borderStrong, light.backgroundPrimary) >= 3.0f,
          "Ink light strong border reads at 3:1");
    check(light.primaryHover.r < light.primary.r && light.primaryPressed.r < light.primaryHover.r,
          "Ink light hover and pressed step darker");
    check(contrast(light.primary, light.backgroundPrimary) >= 3.0f && contrast(light.accentText, light.backgroundPrimary) >= 4.5f &&
              contrast(light.accentText, light.backgroundSecondary) >= 4.5f,
          "Ink light: the selection is a mark on the bed, violet text reads 4.5:1");
    check(hc.mode == 2 && same(hc.timelineBed, NUIColor::black()), "Ink high contrast is a high-contrast theme");
    check(same(hc.primary, NUIThemePresets::createKitTheme("ember-hc").primary), "Ink high contrast copies Ember's");

    // Ember stays exactly as it was: the warm black and the thin selection wash.
    const auto ember = NUIThemePresets::createKitTheme("ember-dark");
    check(same(ember.primary, hex(0x8a3ff0)) && same(ember.backgroundPrimary, hex(0x0a0908)),
          "Ember dark is unchanged");
    check(ember.selected.a < 0.5f, "Ember dark keeps its alpha selection");
}

// The selection family: every theme states it, all opaque, and each pairing meets its floor.
void testSelectionFamily() {
    std::cout << "selection family\n";
    for (const auto& k : NUIThemePresets::kitThemes()) {
        const std::string key = k.key;
        const auto t = NUIThemePresets::createKitTheme(key);
        const bool solid = t.selected.a >= 0.9f;
        check(t.selectionFill.a == 1.0f && t.selectionTint.a == 1.0f && t.selectionEdge.a == 1.0f && t.hoverSelected.a == 1.0f &&
                  t.accentText.a == 1.0f && t.playhead.a == 1.0f && t.selectedText.a == 1.0f,
              key + ": the selection family is opaque");
        check(contrast(t.selectedText, solid ? t.selected : t.selectionFill) >= 4.5f,
              key + ": selectedText reads on what it labels");
        check(contrast(t.textPrimary, t.selectionFill) >= 4.5f, key + ": textPrimary reads on selectionFill");
        check(contrast(t.accentText, t.backgroundPrimary) >= 4.5f && contrast(t.accentText, t.backgroundSecondary) >= 4.5f,
              key + ": accentText reads on the bed and on a card");
        check(contrast(t.textPrimary, t.selectionTint) >= 4.5f, key + ": textPrimary reads on selectionTint");
        check(contrast(t.selectionEdge, t.selectionTint) >= 3.0f, key + ": selectionEdge is a mark on selectionTint");
        check(contrast(t.selectionEdge, t.backgroundSecondary) >= 3.0f, key + ": selectionEdge is a mark on a card");
        check(contrast(t.playhead, t.backgroundPrimary) >= 3.0f && contrast(t.playhead, t.surfaceTertiary) >= 3.0f &&
                  contrast(t.playhead, t.surfaceRaised) >= 3.0f,
              key + ": the playhead reads 3:1 on the bed, a control and a raised surface");
    }
}

// A key that is an alias of a property (accent for primary, selection for selected) loses to the property's own key when a
// file supplies both (owner's note in applyJSONThemeOverrides, 21 Nov 2026). accent/accentHover/accentPressed used to win.
void testCanonicalKeyBeatsAlias() {
    std::cout << "canonical keys beat their aliases\n";
    const std::string path = "theme_alias_test.json";
    {
        std::ofstream out(path);
        out << R"({"colors": {
            "accent": "#aa0000", "primary": "#00aa00", "accentHover": "#bb0000", "primaryHover": "#00bb00",
            "accentPressed": "#cc0000", "primaryPressed": "#00cc00", "selection": "#dd0000", "selected": "#00dd00",
            "recessedPanel": "#110000", "backgroundSecondary": "#001100"}})";
    }
    const auto hex = [](unsigned v) { return NUIColor::fromHex(v); };
    auto& manager = NUIThemeManager::getInstance();
    check(manager.loadThemeFromFile("alias-test", path, "ink-dark"), "the theme file loads over Ink dark");
    manager.setActiveTheme("alias-test");
    const auto& t = manager.getCurrentTheme();
    check(same(t.primary, hex(0x00aa00)), "primary beats accent");
    check(same(t.primaryHover, hex(0x00bb00)), "primaryHover beats accentHover");
    check(same(t.primaryPressed, hex(0x00cc00)), "primaryPressed beats accentPressed");
    check(same(t.selected, hex(0x00dd00)), "selected beats selection");
    check(same(t.backgroundSecondary, hex(0x001100)), "backgroundSecondary beats recessedPanel");
    manager.setActiveTheme("ink-dark");
    std::remove(path.c_str());
}

void testAliases() {
    std::cout << "old names resolve to Ink\n";
    const auto ink = NUIThemePresets::createKitTheme("ink-dark");
    check(same(NUIThemePresets::createAestraDark().primary, ink.primary), "createAestraDark is Ink dark");
    check(same(NUIThemePresets::createAestraDark().backgroundSecondary, ink.backgroundSecondary),
          "createAestraDark carries Ink's panels");
    check(same(NUIThemePresets::createAestraLight().backgroundPrimary,
               NUIThemePresets::createKitTheme("ink-light").backgroundPrimary),
          "createAestraLight is Ink light");
    auto& manager = NUIThemeManager::getInstance();
    check(manager.setActiveTheme("Aestra-dark"), "Aestra-dark still loads");
    check(same(manager.getCurrentTheme().backgroundPrimary, ink.backgroundPrimary), "Aestra-dark is Ink dark");
    check(manager.setActiveTheme("aestra-dark") && same(manager.getCurrentTheme().primary, ink.primary),
          "aestra-dark is Ink dark");
    check(manager.setActiveTheme("Aestra-light") &&
              same(manager.getCurrentTheme().backgroundPrimary,
                   NUIThemePresets::createKitTheme("ink-light").backgroundPrimary),
          "Aestra-light is Ink light");
    check(manager.setActiveTheme("high-contrast-dark"), "high-contrast-dark still loads");
    check(manager.getCurrentTheme().mode == 2, "high-contrast-dark is a high-contrast theme");
    check(same(manager.getColor("editorWell"), NUIThemePresets::createKitTheme("ink-hc").editorWell),
          "getColor reads stated editor tokens");
    check(same(NUIThemePresets::createKitTheme("no-such-theme").primary, ink.primary),
          "an unknown key falls back to Ink dark");
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
    testAllKitThemes();
    testInk();
    testSelectionFamily();
    testCanonicalKeyBeatsAlias();
    testAliases();
    testDepth();
    std::cout << (gFailures == 0 ? "ALL PASS\n" : "FAILURES: " + std::to_string(gFailures) + "\n");
    return gFailures == 0 ? 0 : 1;
}
