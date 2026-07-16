// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file NUIThemeJSONTest.cpp
 * @brief Tests for NUITheme::loadFromFile JSON theme loading (Issue #259)
 *
 * Tests:
 * - Valid theme file: colors (#RRGGBB and #RRGGBBAA), dimensions, effects, fontSizes applied
 * - Missing fields fall back to default theme values
 * - Nonexistent file returns the default theme (never null)
 * - Malformed JSON returns the default theme
 * - Invalid entries (bad hex, wrong types, non-finite/non-positive numbers) keep defaults
 */

#include "NUITheme.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

using AestraUI::NUIColor;
using AestraUI::NUITheme;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << "  " << (condition ? "✓ " : "✗ ") << label << std::endl;
    if (!condition) {
        ++g_failures;
    }
}

bool nearlyEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

bool colorsEqual(const NUIColor& a, const NUIColor& b) {
    return nearlyEqual(a.r, b.r) && nearlyEqual(a.g, b.g) && nearlyEqual(a.b, b.b) && nearlyEqual(a.a, b.a);
}

std::string writeTempFile(const std::string& name, const std::string& content) {
    const std::string path = name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    return path;
}

void testValidThemeApplies() {
    std::cout << "[Test] Valid theme file applies all sections" << std::endl;
    const std::string path = writeTempFile("theme_valid_test.json", R"({
        "colors": {
            "primary": "#ff0000",
            "background": "#00ff00",
            "surface": "#0000ff80",
            "focusRing": "#112233",
            "dragTarget": "#44556680"
        },
        "dimensions": { "borderRadius": 3.5, "standardControlHeight": 30.0, "shadowNudge": -2.0 },
        "effects": { "glowIntensity": 0.9 },
        "fontSizes": { "normal": 17.0 }
    })");

    auto theme = NUITheme::loadFromFile(path);
    check(theme != nullptr, "theme is not null");
    check(colorsEqual(theme->getColor("primary"), NUIColor::fromHex(0xff0000)), "primary = #ff0000");
    check(colorsEqual(theme->getColor("background"), NUIColor::fromHex(0x00ff00)), "background = #00ff00");
    check(colorsEqual(theme->getColor("surface"), NUIColor::fromHex(0x0000ff, 128.0f / 255.0f)),
          "surface #RRGGBBAA alpha applied");
    check(colorsEqual(theme->getColor("focusRing"), NUIColor::fromHex(0x112233)), "semantic focus ring applied");
    check(colorsEqual(theme->getColor("dragTarget"), NUIColor::fromHex(0x445566, 128.0f / 255.0f)),
          "semantic drag target applied");
    check(nearlyEqual(theme->getDimension("borderRadius"), 3.5f), "borderRadius = 3.5");
    check(nearlyEqual(theme->getDimension("standardControlHeight"), 30.0f),
          "semantic control dimension applied");
    check(nearlyEqual(theme->getDimension("shadowNudge"), -2.0f), "negative dimension accepted");
    check(nearlyEqual(theme->getEffect("glowIntensity"), 0.9f), "glowIntensity = 0.9");
    check(nearlyEqual(theme->getFontSize("normal"), 17.0f), "fontSize normal = 17");
    std::remove(path.c_str());
}

void testMissingFieldsKeepDefaults() {
    std::cout << "[Test] Missing fields fall back to defaults" << std::endl;
    const std::string path = writeTempFile("theme_partial_test.json", R"({
        "colors": { "primary": "#123456" }
    })");

    auto theme = NUITheme::loadFromFile(path);
    auto defaults = NUITheme::createDefault();
    check(colorsEqual(theme->getColor("primary"), NUIColor::fromHex(0x123456)), "overridden color applied");
    check(colorsEqual(theme->getColor("background"), defaults->getColor("background")),
          "untouched color keeps default");
    check(nearlyEqual(theme->getDimension("borderRadius"), defaults->getDimension("borderRadius")),
          "untouched dimension keeps default");
    check(nearlyEqual(theme->getFontSize("normal"), defaults->getFontSize("normal")),
          "untouched font size keeps default");
    check(colorsEqual(theme->getColor("armed"), defaults->getColor("armed")),
          "missing semantic color keeps default");
    check(nearlyEqual(theme->getDimension("minimumHitArea"), defaults->getDimension("minimumHitArea")),
          "missing semantic dimension keeps default");
    std::remove(path.c_str());
}

void testNonexistentFileReturnsDefault() {
    std::cout << "[Test] Nonexistent file returns default theme" << std::endl;
    auto theme = NUITheme::loadFromFile("theme_does_not_exist_test.json");
    auto defaults = NUITheme::createDefault();
    check(theme != nullptr, "theme is not null");
    check(colorsEqual(theme->getColor("primary"), defaults->getColor("primary")), "matches default theme");
}

void testMalformedJSONReturnsDefault() {
    std::cout << "[Test] Malformed JSON returns default theme" << std::endl;
    const std::string path = writeTempFile("theme_malformed_test.json", "{ \"colors\": { \"primary\": ");
    auto theme = NUITheme::loadFromFile(path);
    auto defaults = NUITheme::createDefault();
    check(theme != nullptr, "theme is not null");
    check(colorsEqual(theme->getColor("primary"), defaults->getColor("primary")), "matches default theme");
    std::remove(path.c_str());

    const std::string path2 = writeTempFile("theme_nonobject_test.json", "[1, 2, 3]");
    auto theme2 = NUITheme::loadFromFile(path2);
    check(colorsEqual(theme2->getColor("primary"), defaults->getColor("primary")),
          "non-object root falls back to default");
    std::remove(path2.c_str());
}

void testInvalidEntriesKeepDefaults() {
    std::cout << "[Test] Invalid entries keep defaults, valid siblings still apply" << std::endl;
    const std::string path = writeTempFile("theme_invalid_entries_test.json", R"({
        "colors": {
            "primary": "#12345",
            "secondary": "not-a-color",
            "accent": 42,
            "text": "aabbcc",
            "textSecondary": "#abc",
            "error": "#ff00ff"
        },
        "dimensions": { "borderRadius": "twelve", "compactControlHeight": -1.0 },
        "fontSizes": { "normal": -5, "small": 11.0 }
    })");

    auto theme = NUITheme::loadFromFile(path);
    auto defaults = NUITheme::createDefault();
    check(colorsEqual(theme->getColor("primary"), defaults->getColor("primary")), "5-digit hex rejected");
    check(colorsEqual(theme->getColor("secondary"), defaults->getColor("secondary")), "non-hex string rejected");
    check(colorsEqual(theme->getColor("accent"), defaults->getColor("accent")), "number-as-color rejected");
    check(colorsEqual(theme->getColor("text"), defaults->getColor("text")), "hex without '#' rejected");
    check(colorsEqual(theme->getColor("textSecondary"), defaults->getColor("textSecondary")),
          "#RGB shorthand rejected");
    check(colorsEqual(theme->getColor("error"), NUIColor::fromHex(0xff00ff)), "valid sibling color applied");
    check(nearlyEqual(theme->getDimension("borderRadius"), defaults->getDimension("borderRadius")),
          "string-as-dimension rejected");
    check(nearlyEqual(theme->getDimension("compactControlHeight"), defaults->getDimension("compactControlHeight")),
          "non-positive semantic dimension rejected");
    check(nearlyEqual(theme->getFontSize("normal"), defaults->getFontSize("normal")),
          "non-positive font size rejected");
    check(nearlyEqual(theme->getFontSize("small"), 11.0f), "valid sibling font size applied");
    std::remove(path.c_str());
}

} // namespace

int main() {
    std::cout << "=== NUITheme JSON Loading Tests (Issue #259) ===" << std::endl;

    testValidThemeApplies();
    testMissingFieldsKeepDefaults();
    testNonexistentFileReturnsDefault();
    testMalformedJSONReturnsDefault();
    testInvalidEntriesKeepDefaults();

    if (g_failures == 0) {
        std::cout << "All NUITheme JSON tests passed ✓" << std::endl;
        return 0;
    }
    std::cout << g_failures << " check(s) failed ✗" << std::endl;
    return 1;
}
