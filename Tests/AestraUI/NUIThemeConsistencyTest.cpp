// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIThemeSystem.h"
#include "NUIComponent.h"
#include "NUIConfigLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

using namespace AestraUI;

namespace {

int gFailures = 0;

class ThemeProbe final : public NUIComponent {
public:
    void onThemeChanged(const NUIThemeProperties& theme) override {
        ++changeCount;
        observedPrimary = theme.primary;
        NUIComponent::onThemeChanged(theme);
    }

    int changeCount = 0;
    NUIColor observedPrimary;
};

void check(bool condition, const std::string& label) {
    std::cout << "  " << (condition ? "PASS " : "FAIL ") << label << '\n';
    if (!condition)
        ++gFailures;
}

bool nearlyEqual(float a, float b, float epsilon = 1.0e-4f) {
    return std::fabs(a - b) <= epsilon;
}

bool colorsEqual(const NUIColor& a, const NUIColor& b) {
    return nearlyEqual(a.r, b.r) && nearlyEqual(a.g, b.g) && nearlyEqual(a.b, b.b) && nearlyEqual(a.a, b.a);
}

NUIColor composite(const NUIColor& foreground, const NUIColor& background) {
    const float a = foreground.a + background.a * (1.0f - foreground.a);
    if (a <= 0.0f)
        return NUIColor::transparent();
    return {(foreground.r * foreground.a + background.r * background.a * (1.0f - foreground.a)) / a,
            (foreground.g * foreground.a + background.g * background.a * (1.0f - foreground.a)) / a,
            (foreground.b * foreground.a + background.b * background.a * (1.0f - foreground.a)) / a, a};
}

float luminance(const NUIColor& color) {
    const auto linear = [](float component) {
        component = std::clamp(component, 0.0f, 1.0f);
        return component <= 0.04045f ? component / 12.92f : std::pow((component + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * linear(color.r) + 0.7152f * linear(color.g) + 0.0722f * linear(color.b);
}

float contrastRatio(const NUIColor& foreground, const NUIColor& background) {
    const NUIColor opaqueBackground = composite(background, NUIColor::black());
    const NUIColor opaqueForeground = composite(foreground, opaqueBackground);
    const float lighter = std::max(luminance(opaqueForeground), luminance(opaqueBackground));
    const float darker = std::min(luminance(opaqueForeground), luminance(opaqueBackground));
    return (lighter + 0.05f) / (darker + 0.05f);
}

void testSemanticDefaults() {
    std::cout << "[Test] semantic defaults\n";
    const auto theme = NUIThemePresets::createAestraDark();

    check(theme.focusRing.a > theme.borderSubtle.a * 0.5f, "focus ring is visible");
    check(colorsEqual(theme.armed, theme.error), "armed resolves to recording/error role");
    check(colorsEqual(theme.muted, theme.warning), "muted resolves to warning role");
    check(colorsEqual(theme.soloed, theme.info), "soloed resolves to information role");
    check(theme.dragTarget.a > theme.hover.a, "drag target is stronger than hover");
    check(theme.gridMajor.a > theme.gridMinor.a, "major grid is stronger than minor grid");
    check(theme.overlay.a > 0.0f && theme.overlay.a < 0.8f, "modal overlay dims without hiding the application");
    check(theme.layout.compactControlHeight == theme.layout.minimumHitArea,
          "compact controls meet the minimum hit area");
    check(theme.layout.transportButtonSize == theme.layout.standardControlHeight,
          "transport and standard controls share the 28 px metric");
    check(theme.layout.titleBarHeight == 32.0f && theme.layout.viewToggleWidth == 310.0f &&
              theme.layout.viewToggleHeight == theme.layout.compactControlHeight,
          "application chrome defaults use the shared compact metric");
    check(theme.layout.standardMenuRowHeight == theme.layout.standardRowHeight,
          "ordinary menu and list rows share a metric");
}

void testStatePriorityAndGeometry() {
    std::cout << "[Test] deterministic state priority\n";
    const auto theme = NUIThemePresets::createAestraDark();
    const float controlHeight = theme.layout.standardControlHeight;

    const auto idle = resolveControlColors(theme, {});
    const auto hovered = resolveControlColors(theme, {true, true, false, false, false});
    const auto selected = resolveControlColors(theme, {true, true, false, true, false});
    const auto pressed = resolveControlColors(theme, {true, true, true, true, false});
    const auto disabled = resolveControlColors(theme, {false, true, true, true, true});
    const auto focused = resolveControlColors(theme, {true, false, false, false, true});

    check(colorsEqual(idle.background, theme.buttonBgDefault), "idle uses control background");
    check(colorsEqual(hovered.background, theme.buttonBgHover), "hover uses neutral hover");
    check(colorsEqual(selected.background, theme.selected), "selection wins over hover");
    check(colorsEqual(pressed.background, theme.buttonBgActive), "pressed wins over selection");
    check(colorsEqual(disabled.text, theme.textDisabled), "disabled wins over all interaction states");
    check(colorsEqual(focused.border, theme.focusRing) && focused.borderWidth == 1.5f,
          "focus changes border treatment");
    check(theme.layout.standardControlHeight == controlHeight, "state transitions do not alter control height");
}

void testDefaultContrast() {
    std::cout << "[Test] default shared-control contrast\n";
    const auto theme = NUIThemePresets::createAestraDark();
    check(contrastRatio(theme.textPrimary, theme.backgroundPrimary) >= 7.0f,
          "primary text has strong workspace contrast");
    check(contrastRatio(theme.buttonTextDefault, theme.buttonBgDefault) >= 4.5f,
          "default control text remains readable");
    check(contrastRatio(theme.buttonTextActive, theme.buttonBgActive) >= 4.5f, "active control text remains readable");
    check(contrastRatio(theme.textOnPrimary, theme.primary) >= 4.5f,
          "primary dialog action text remains readable");
}

void testThemeChangeResolution() {
    std::cout << "[Test] theme changes update semantic resolution\n";
    auto& manager = NUIThemeManager::getInstance();
    auto custom = NUIThemePresets::createAestraDark();
    custom.buttonBgHover = NUIColor::fromHex(0x123456);
    custom.focusRing = NUIColor::fromHex(0xabcdef);

    int callbackCount = 0;
    manager.setOnThemeChanged([&callbackCount](const NUIThemeProperties&) { ++callbackCount; });
    manager.setCustomTheme("consistency-test", custom);
    manager.setActiveTheme("consistency-test");

    const auto resolved = resolveControlColors(manager.getCurrentTheme(), {true, true, false, false, true});
    check(colorsEqual(resolved.background, custom.buttonBgHover), "hover reads the newly active theme");
    check(colorsEqual(resolved.border, custom.focusRing), "focus reads the newly active theme");
    check(callbackCount == 1, "theme activation emits one change callback");

    manager.setActiveTheme("Aestra-dark");
    manager.setOnThemeChanged({});
}

void testIndependentSubscriptionsAndAtomicSwitching() {
    std::cout << "[Test] independent subscribers and atomic switching\n";
    auto& manager = NUIThemeManager::getInstance();
    manager.setActiveTheme("Aestra-dark");

    auto target = NUIThemePresets::createAestraLight();
    target.primary = NUIColor::fromHex(0x13579b);
    target.layout.standardControlHeight = 31.0f;
    manager.setCustomTheme("subscription-test", target);

    int firstCount = 0;
    int secondCount = 0;
    bool observedCompleteTheme = false;
    const auto first = manager.subscribeToThemeChanges([&](const NUIThemeProperties&) { ++firstCount; });
    const auto second = manager.subscribeToThemeChanges([&](const NUIThemeProperties& theme) {
        ++secondCount;
        observedCompleteTheme = colorsEqual(theme.primary, target.primary) &&
                                nearlyEqual(theme.layout.standardControlHeight, 31.0f);
    });

    manager.switchTheme("subscription-test", 300.0f);
    check(manager.getActiveTheme() == "subscription-test", "animated API activates the requested theme atomically");
    check(firstCount == 1 && secondCount == 1, "independent subscribers each receive one notification");
    check(observedCompleteTheme, "subscriber receives a complete target theme, not a partial interpolation");

    manager.unsubscribeFromThemeChanges(first);
    manager.setActiveTheme("Aestra-dark");
    check(firstCount == 1 && secondCount == 2, "unsubscribing one listener does not disconnect another");
    manager.unsubscribeFromThemeChanges(second);
}

void testLiveJSONThemeRegistration() {
    std::cout << "[Test] JSON overrides register into the live manager\n";
    const std::string path = "live_theme_registration_test.json";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"({
            "colors": { "accent": "#2468ac", "focusRing": "#abcdef" },
            "dimensions": {
                "standardControlHeight": 30.0,
                "titleBarHeight": 34.0,
                "viewToggleWidth": 300.0,
                "viewToggleHeight": 26.0
            },
            "fontSizes": { "normal": 13.0 }
        })";
    }

    auto& manager = NUIThemeManager::getInstance();
    const auto base = NUIThemePresets::createAestraDark();
    check(manager.loadThemeFromFile("live-json-test", path), "valid JSON theme registers successfully");
    check(manager.setActiveTheme("live-json-test"), "registered JSON theme can become active");
    const auto& loaded = manager.getCurrentTheme();
    check(colorsEqual(loaded.primary, NUIColor::fromHex(0x2468ac)), "legacy accent key maps to live primary role");
    check(colorsEqual(loaded.focusRing, NUIColor::fromHex(0xabcdef)), "semantic focus ring maps into live state");
    check(colorsEqual(loaded.backgroundPrimary, base.backgroundPrimary), "missing live token inherits the base preset");
    check(nearlyEqual(loaded.layout.standardControlHeight, 30.0f), "live control dimension is overridden");
    check(nearlyEqual(manager.getLayoutDimension("titleBarHeight"), 34.0f) &&
              nearlyEqual(manager.getLayoutDimension("viewToggleWidth"), 300.0f) &&
              nearlyEqual(manager.getLayoutDimension("viewToggleHeight"), 26.0f),
          "application chrome dimensions are overridden and exposed through the manager");
    check(nearlyEqual(loaded.fontSizeM, 13.0f), "legacy normal font size maps to live body text");

    int reloadCount = 0;
    const auto reloadSubscription = manager.subscribeToThemeChanges(
        [&](const NUIThemeProperties&) { ++reloadCount; });
    check(manager.loadThemeFromFile("live-json-test", path), "active JSON theme can be reloaded");
    check(reloadCount == 1, "reloading the active theme emits one update");
    manager.unsubscribeFromThemeChanges(reloadSubscription);
    std::remove(path.c_str());

    const std::string invalidPath = "invalid_live_theme_registration_test.json";
    {
        std::ofstream out(invalidPath, std::ios::binary | std::ios::trunc);
        out << "{ invalid";
    }
    check(!manager.loadThemeFromFile("invalid-live-json-test", invalidPath),
          "malformed JSON is rejected by live registration");
    check(!manager.hasTheme("invalid-live-json-test"), "failed registration does not install a misleading fallback");
    std::remove(invalidPath.c_str());
    manager.setActiveTheme("Aestra-dark");
}

void testHierarchyInvalidation() {
    std::cout << "[Test] component hierarchy theme propagation\n";
    auto root = std::make_shared<ThemeProbe>();
    auto child = std::make_shared<ThemeProbe>();
    root->addChild(child);
    root->setDirty(false);
    child->setDirty(false);

    const auto theme = NUIThemePresets::createAestraLight();
    root->onThemeChanged(theme);
    check(root->changeCount == 1 && child->changeCount == 1, "theme change reaches each hierarchy node once");
    check(root->isDirty() && child->isDirty(), "theme change invalidates root and descendants");
    check(colorsEqual(child->observedPrimary, theme.primary), "descendant observes the complete active theme");
}

void testCompatibilityAliases() {
    std::cout << "[Test] live component token aliases\n";
    auto& manager = NUIThemeManager::getInstance();
    manager.setActiveTheme("Aestra-dark");
    const auto& theme = manager.getCurrentTheme();

    check(colorsEqual(manager.getColor("backgroundTertiary"), theme.surfaceTertiary),
          "legacy tertiary background does not fall back to accent");
    check(colorsEqual(manager.getColor("surfaceSecondary"), theme.surfaceRaised),
          "legacy secondary surface resolves semantically");
    check(colorsEqual(manager.getColor("borderPrimary"), theme.borderStrong) &&
              colorsEqual(manager.getColor("borderSecondary"), theme.borderSubtle),
          "routing borders resolve to structural roles");
    check(colorsEqual(manager.getColor("textTertiary"), theme.textMuted) &&
              colorsEqual(manager.getColor("textInfo"), theme.info),
          "tertiary and informational text avoid accent fallback");
    check(colorsEqual(manager.getColor("textOnAccent"), theme.textOnPrimary),
          "accent action text uses a readable foreground");
    check(colorsEqual(manager.getColor("inputBackground"), theme.inputBgDefault),
          "legacy input background resolves to the control input role");
    check(colorsEqual(manager.getColor("accentAmber"), theme.warning),
          "audio minimap amber resolves to warning rather than primary accent");
}

void testLightPresetCompleteness() {
    std::cout << "[Test] light preset semantic completeness\n";
    const auto theme = NUIThemePresets::createAestraLight();

    check(colorsEqual(theme.textLink, theme.primary), "light link text is initialized");
    check(colorsEqual(theme.textCritical, theme.error), "light critical text is initialized");
    check(theme.highlightGlow.a > 0.0f && theme.highlightGlow.a < 0.3f,
          "light highlight glow is initialized and restrained");
    check(theme.overlay.a > 0.0f && theme.overlay.a < 0.8f,
          "light modal overlay remains translucent");
    check(contrastRatio(theme.textOnPrimary, theme.primary) >= 4.5f,
          "light primary action text remains readable");
}

void testHighContrastPreset() {
    std::cout << "[Test] high-contrast preset is distinct and readable\n";
    const auto ordinary = NUIThemePresets::createAestraDark();
    const auto highContrast = NUIThemePresets::createHighContrastDark();

    check(!colorsEqual(highContrast.borderStrong, ordinary.borderStrong),
          "high contrast is not an alias of ordinary dark");
    check(contrastRatio(highContrast.textPrimary, highContrast.backgroundPrimary) >= 7.0f,
          "high-contrast primary text exceeds enhanced contrast");
    check(contrastRatio(highContrast.textSecondary, highContrast.backgroundSecondary) >= 4.5f,
          "high-contrast secondary text remains readable");
    check(contrastRatio(highContrast.textOnPrimary, highContrast.primary) >= 4.5f,
          "high-contrast accent action text remains readable");
    check(highContrast.gridMajor.a > ordinary.gridMajor.a && highContrast.gridMinor.a > ordinary.gridMinor.a,
          "high-contrast grid hierarchy is stronger without flattening major/minor distinction");
}

// #582 review: the chrome layout dimensions must flow through NUIConfigLoader's
// config parse+apply path, and malformed values must not clobber the theme
// defaults with zero (see the applyLayout validation guard).
void testChromeDimensionConfigLoad() {
    std::cout << "[Test] chrome dimensions load through NUIConfigLoader and reject invalid values\n";
    auto& manager = NUIThemeManager::getInstance();
    auto& configLoader = NUIConfigLoader::getInstance();

    {
        auto& theme = manager.getCurrentThemeMutable();
        theme.layout.titleBarHeight = 30.0f;
        theme.layout.viewToggleWidth = 300.0f;
        theme.layout.viewToggleHeight = 24.0f;
    }

    // Valid values apply. loadConfigFromString parses the config document with
    // the loader's own parser, exercising applyLayout for the new chrome keys.
    check(configLoader.loadConfigFromString(
              "layout:\n"
              "  titleBarHeight: 41.0\n"
              "  viewToggleWidth: 321.0\n"
              "  viewToggleHeight: 27.0\n"),
          "config with chrome dimensions parses");
    {
        const auto& t = manager.getCurrentTheme();
        check(nearlyEqual(t.layout.titleBarHeight, 41.0f), "titleBarHeight applied from config");
        check(nearlyEqual(t.layout.viewToggleWidth, 321.0f), "viewToggleWidth applied from config");
        check(nearlyEqual(t.layout.viewToggleHeight, 27.0f), "viewToggleHeight applied from config");
    }

    // Invalid (non-positive) values are rejected: the previously applied value
    // is retained instead of being zeroed by parseDimension()'s 0.0f fallback.
    check(configLoader.loadConfigFromString(
              "layout:\n"
              "  titleBarHeight: 0\n"
              "  viewToggleWidth: -5\n"),
          "config with invalid chrome dimensions still parses");
    {
        const auto& t = manager.getCurrentTheme();
        check(nearlyEqual(t.layout.titleBarHeight, 41.0f), "zero titleBarHeight is rejected, default retained");
        check(nearlyEqual(t.layout.viewToggleWidth, 321.0f), "negative viewToggleWidth is rejected, default retained");
    }

    // Non-numeric input hits parseDimension()'s 0.0f fallback; an extent
    // dimension must reject it and keep the current value (CR #582 follow-up).
    check(configLoader.loadConfigFromString(
              "layout:\n"
              "  viewToggleHeight: bad\n"),
          "config with a non-numeric chrome dimension still parses");
    check(nearlyEqual(manager.getCurrentTheme().layout.viewToggleHeight, 27.0f),
          "non-numeric viewToggleHeight is rejected, default retained");

    // Spacing tokens differ from extents: zero is a legitimate flush layout, so
    // panelMargin/componentPadding accept 0 rather than being rejected.
    {
        auto& theme = manager.getCurrentThemeMutable();
        theme.layout.panelMargin = 12.0f;
        theme.layout.componentPadding = 8.0f;
    }
    check(configLoader.loadConfigFromString(
              "layout:\n"
              "  panelMargin: 0\n"
              "  componentPadding: 0\n"),
          "config with zero spacing parses");
    {
        const auto& t = manager.getCurrentTheme();
        check(nearlyEqual(t.layout.panelMargin, 0.0f), "zero panelMargin is accepted (flush layout)");
        check(nearlyEqual(t.layout.componentPadding, 0.0f), "zero componentPadding is accepted (flush layout)");
    }
}

// #585 regression: saveConfig() serializes the theme as JSON, so loadConfig()
// must parse JSON. Before the format-detecting loader, loadConfig() ran the JSON
// document through the YAML line-parser, returned true, and applied nothing — a
// saved config silently failed to restore. This drives the real file round-trip
// (saveConfig -> clobber -> loadConfig), which must fail without the fix.
void testConfigSaveLoadRoundtrip() {
    std::cout << "[Test] saveConfig -> loadConfig round-trips through JSON (#585)\n";
    auto& manager = NUIThemeManager::getInstance();
    auto& configLoader = NUIConfigLoader::getInstance();

    // Known, distinct values to persist. The color uses fromHex so it survives
    // the hex serialization exactly.
    {
        auto& theme = manager.getCurrentThemeMutable();
        theme.layout.trackHeight = 63.0f;
        theme.layout.titleBarHeight = 37.0f;
        theme.layout.viewToggleWidth = 311.0f;
        theme.layout.viewToggleHeight = 29.0f;
        theme.spacingM = 17.0f;
        theme.spacingS = 9.0f;
        theme.primary = NUIColor::fromHex(0xBB86FC);
    }

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "aestra_nuiconfig_roundtrip_585.cfg";
    std::error_code ec;
    std::filesystem::remove(tmp, ec); // clear any stale leftover
    configLoader.saveConfig(tmp.string());

    // Clobber every persisted value in memory so a no-op load is detectable: if
    // loadConfig applies nothing, these sentinels survive and the checks fail.
    {
        auto& theme = manager.getCurrentThemeMutable();
        theme.layout.trackHeight = 1.0f;
        theme.layout.titleBarHeight = 1.0f;
        theme.layout.viewToggleWidth = 1.0f;
        theme.layout.viewToggleHeight = 1.0f;
        theme.spacingM = 1.0f;
        theme.spacingS = 1.0f;
        theme.primary = NUIColor::fromHex(0x010203);
    }

    check(configLoader.loadConfig(tmp.string()), "saved config file loads");
    {
        const auto& t = manager.getCurrentTheme();
        check(nearlyEqual(t.layout.trackHeight, 63.0f), "trackHeight restored from saved config");
        check(nearlyEqual(t.layout.titleBarHeight, 37.0f), "titleBarHeight restored from saved config");
        check(nearlyEqual(t.layout.viewToggleWidth, 311.0f), "viewToggleWidth restored from saved config");
        check(nearlyEqual(t.layout.viewToggleHeight, 29.0f), "viewToggleHeight restored from saved config");
        check(nearlyEqual(t.spacingM, 17.0f), "panelMargin (spacingM) restored from saved config");
        check(nearlyEqual(t.spacingS, 9.0f), "componentPadding (spacingS) restored from saved config");
        check(colorsEqual(t.primary, NUIColor::fromHex(0xBB86FC)), "primary color restored from saved config");
    }

    std::filesystem::remove(tmp, ec);
}

} // namespace

int main() {
    testSemanticDefaults();
    testStatePriorityAndGeometry();
    testDefaultContrast();
    testThemeChangeResolution();
    testIndependentSubscriptionsAndAtomicSwitching();
    testLiveJSONThemeRegistration();
    testChromeDimensionConfigLoad();
    testConfigSaveLoadRoundtrip();
    testHierarchyInvalidation();
    testCompatibilityAliases();
    testLightPresetCompleteness();
    testHighContrastPreset();

    if (gFailures == 0) {
        std::cout << "All UI theme consistency checks passed\n";
        return 0;
    }
    std::cout << gFailures << " UI theme consistency check(s) failed\n";
    return 1;
}
