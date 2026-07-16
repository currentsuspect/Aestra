// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

using namespace AestraUI;

namespace {

int gFailures = 0;

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

} // namespace

int main() {
    testSemanticDefaults();
    testStatePriorityAndGeometry();
    testDefaultContrast();
    testThemeChangeResolution();
    testCompatibilityAliases();
    testLightPresetCompleteness();

    if (gFailures == 0) {
        std::cout << "All UI theme consistency checks passed\n";
        return 0;
    }
    std::cout << gFailures << " UI theme consistency check(s) failed\n";
    return 1;
}
