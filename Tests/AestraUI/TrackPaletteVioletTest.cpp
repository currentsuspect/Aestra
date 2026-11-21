// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Ink theme: violet means "selected" and "the playhead", so no colour a unit, track or route can be given may sit in
// the violet hue family (OKLCH hue 255-300, the window around the theme's primary). Purple and Indigo were in it; they
// stay as legacy slots so a saved project (which names colours by palette index and by ARGB) loads exactly as it was.

#include "NUIThemeSystem.h"
#include "TrackColorPalette.h"

#include <cmath>
#include <iostream>
#include <set>
#include <string>

using namespace AestraUI;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}

double oklchHue(uint32_t argb) {
    const auto lin = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    const double r = lin(((argb >> 16) & 0xFF) / 255.0), g = lin(((argb >> 8) & 0xFF) / 255.0), b = lin((argb & 0xFF) / 255.0);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    const double A = 1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s;
    const double B = 0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s;
    double h = std::atan2(B, A) * 180.0 / 3.14159265358979323846;
    return h < 0 ? h + 360.0 : h;
}
double hueGap(double a, double b) { const double d = std::fabs(a - b); return d > 180.0 ? 360.0 - d : d; }
bool inVioletWindow(double h) { return h >= 255.0 && h <= 300.0; }
} // namespace

int main() {
    std::cout << "assignable colours avoid the violet family\n";
    check(ASSIGNABLE_COUNT == 8, "the palette still offers eight colours");
    std::set<int> seen;
    for (int k = 0; k < ASSIGNABLE_COUNT; ++k) {
        const int idx = ASSIGNABLE_ORDER[k];
        check(idx >= 0 && idx < PALETTE_SIZE && idx != 1 && idx != 7, std::string(PALETTE_NAMES[idx]) + " is a valid, non-legacy slot");
        check(seen.insert(idx).second, std::string(PALETTE_NAMES[idx]) + " appears once");
        const double h = oklchHue(TRACK_PALETTE[idx]);
        check(!inVioletWindow(h), std::string(PALETTE_NAMES[idx]) + " (hue " + std::to_string(static_cast<int>(h)) + ") is outside 255-300");
        // ...and a clear distance from every theme's actual primary, whatever hue a theme gives it.
        for (const auto& kit : NUIThemePresets::kitThemes()) {
            const NUIThemeProperties t = NUIThemePresets::createKitTheme(kit.key);
            const uint32_t prim = (0xFFu << 24) | (static_cast<uint32_t>(std::lround(t.primary.r * 255)) << 16) |
                                  (static_cast<uint32_t>(std::lround(t.primary.g * 255)) << 8) |
                                  static_cast<uint32_t>(std::lround(t.primary.b * 255));
            const bool violetPrimary = std::string(kit.key).rfind("ink-", 0) == 0 || std::string(kit.key).rfind("ember-", 0) == 0;
            if (violetPrimary)
                check(hueGap(h, oklchHue(prim)) >= 25.0, std::string(PALETTE_NAMES[idx]) + " is 25+ degrees from " + kit.key + "'s primary");
        }
    }
    // Distinct: no two assignable colours within 20 degrees of each other.
    for (int a = 0; a < ASSIGNABLE_COUNT; ++a)
        for (int b = a + 1; b < ASSIGNABLE_COUNT; ++b)
            check(hueGap(oklchHue(TRACK_PALETTE[ASSIGNABLE_ORDER[a]]), oklchHue(TRACK_PALETTE[ASSIGNABLE_ORDER[b]])) >= 20.0,
                  std::string(PALETTE_NAMES[ASSIGNABLE_ORDER[a]]) + " and " + PALETTE_NAMES[ASSIGNABLE_ORDER[b]] + " are 20+ degrees apart");

    std::cout << "new assignments never hand out violet\n";
    bool never = true;
    for (size_t n = 0; n < 200; ++n) {
        const int idx = assignablePaletteIndex(n);
        never = never && idx != 1 && idx != 7 && !inVioletWindow(oklchHue(paletteIndexToARGB(idx)));
    }
    check(never, "200 consecutive new units, tracks and channels: none violet");
    check(assignablePaletteIndex(0) != assignablePaletteIndex(1) && assignablePaletteIndex(0) == assignablePaletteIndex(8), "rotation wraps at eight");

    std::cout << "saved projects load unchanged\n";
    check(paletteIndexToARGB(1) == 0xFFB88EFC && paletteIndexToARGB(7) == 0xFF8AA1FC, "a saved index 1 or 7 still decodes to its old violet");
    check(paletteIndexToARGB(0) == 0xFF06C49D && paletteIndexToARGB(2) == 0xFFFB8304 && paletteIndexToARGB(6) == 0xFFFB70AD,
          "the surviving entries keep their index and colour");
    check(nearestPaletteIndex(0xFFB88EFC) == 1 && nearestPaletteIndex(0xFF8AA1FC) == 7, "a saved violet ARGB still resolves to its legacy slot");
    check(resolveTrackColorIndex(1, 0, 0) == 1 && resolveTrackColorIndex(7, 0, 0) == 7, "a saved legacy index is kept, not remapped");
    check(resolveTrackColorIndex(-1, 0, 1) == assignablePaletteIndex(1), "an unset channel takes the next assignable colour");
    check(resolveTrackColorIndex(PALETTE_SIZE, 0, 4) == assignablePaletteIndex(4), "a stale out-of-range index is not used");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
