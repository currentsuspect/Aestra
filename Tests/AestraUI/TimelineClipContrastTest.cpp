// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Guards the timeline clip contrast contract: a clip is a surface with audio
// drawn on it, so the waveform ink must stay meaningfully brighter than the clip
// body it sits on, for every lane identity and in both selection states.
//
// Deliberately does NOT assert the brightness constants themselves. Those are
// taste values the owner is expected to retune; a test that restates 0.68f only
// duplicates the literal and turns every future tweak into a test edit. What
// must not regress is the *ordering and margin* between the two tones — that is
// what makes a waveform readable, and it is what silently breaks if someone
// brightens the body or dims the ink without looking at the other half.
//
// The pairing is only checkable because clipBodyTone() and waveformInkTone() are
// derived side by side in TrackColorPalette.h. If a later change moves one of
// them somewhere else, this test stops being able to see the contract.

#include "Widgets/TrackColorPalette.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using AestraUI::clipBodyTone;
using AestraUI::NUIColor;
using AestraUI::paletteIndexToARGB;
using AestraUI::PALETTE_SIZE;
using AestraUI::waveformInkTone;

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        ++failures;
    }
}

// Perceptual (WCAG) relative luminance, so the margin means something to an eye
// rather than to a raw channel sum.
float relativeLuminance(const NUIColor& c) {
    const auto channel = [](float v) {
        return (v <= 0.03928f) ? (v / 12.92f) : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * channel(c.r) + 0.7152f * channel(c.g) + 0.0722f * channel(c.b);
}

float contrastRatio(const NUIColor& a, const NUIColor& b) {
    const float la = relativeLuminance(a);
    const float lb = relativeLuminance(b);
    const float hi = (la > lb) ? la : lb;
    const float lo = (la > lb) ? lb : la;
    return (hi + 0.05f) / (lo + 0.05f);
}

// Below this the waveform stops reading as a distinct shape over its fill. Set
// under the current measured value so ordinary retuning does not trip it, but
// far enough above 1.0 that a collapse is caught.
constexpr float kMinInkOverBody = 1.35f;

std::string describe(const NUIColor& c) {
    return "(" + std::to_string(static_cast<int>(c.r * 255.0f)) + "," +
           std::to_string(static_cast<int>(c.g * 255.0f)) + "," +
           std::to_string(static_cast<int>(c.b * 255.0f)) + ")";
}

// The core contract, over every palette identity plus the unset-colour grey
// fallback, in both selection states.
void testInkOutranksBodyForEveryIdentity() {
    for (int i = 0; i <= PALETTE_SIZE; ++i) {
        // One past the palette exercises paletteIndexToARGB's out-of-range grey,
        // which is what an uncoloured clip actually renders with.
        const NUIColor identity = NUIColor::fromARGB(paletteIndexToARGB(i));

        for (const bool selected : {false, true}) {
            const NUIColor body = clipBodyTone(identity, selected);
            const NUIColor ink = waveformInkTone(identity, selected);
            const std::string where =
                "lane " + std::to_string(i) + (selected ? " (selected)" : " (unselected)");

            check(relativeLuminance(ink) > relativeLuminance(body),
                  where + ": waveform ink " + describe(ink) + " must be brighter than body " +
                      describe(body));

            const float ratio = contrastRatio(ink, body);
            check(ratio >= kMinInkOverBody,
                  where + ": ink-over-body contrast " + std::to_string(ratio) + ":1 fell below the " +
                      std::to_string(kMinInkOverBody) + ":1 floor");
        }
    }
}

// Selecting a clip must not darken it — the selected body carries state, and an
// inverted delta would make selection read as "disabled".
void testSelectionLiftsRatherThanDarkens() {
    for (int i = 0; i < PALETTE_SIZE; ++i) {
        const NUIColor identity = NUIColor::fromARGB(paletteIndexToARGB(i));
        const float plain = relativeLuminance(clipBodyTone(identity, false));
        const float selected = relativeLuminance(clipBodyTone(identity, true));
        check(selected > plain, "lane " + std::to_string(i) +
                                   ": selected body must be brighter than unselected");
    }
}

// The body must stay clearly visible against the timeline's black canvas, or the
// quieting has gone too far and clips vanish instead of receding.
void testBodyStaysVisibleAgainstCanvas() {
    const NUIColor canvas(0.0f, 0.0f, 0.0f, 1.0f);
    for (int i = 0; i <= PALETTE_SIZE; ++i) {
        const NUIColor identity = NUIColor::fromARGB(paletteIndexToARGB(i));
        const NUIColor body = clipBodyTone(identity, false);
        const float ratio = contrastRatio(body, canvas);
        check(ratio >= 2.0f, "lane " + std::to_string(i) + ": body-vs-canvas contrast " +
                                 std::to_string(ratio) + ":1 is too low to see the clip");
    }
}

// Restraint has to actually restrain: a no-op would silently reintroduce the raw
// palette everywhere the helper is used.
void testLaneIdentityRestraintIsNotAnIdentityFunction() {
    for (int i = 0; i < PALETTE_SIZE; ++i) {
        const NUIColor raw = NUIColor::fromARGB(paletteIndexToARGB(i));
        const NUIColor restrained = AestraUI::restrainLaneIdentityColor(raw, 1.0f);
        check(relativeLuminance(restrained) < relativeLuminance(raw),
              "lane " + std::to_string(i) + ": restrained identity must be darker than the raw palette hue");

        const auto saturation = [](const NUIColor& c) {
            const float mx = std::max(c.r, std::max(c.g, c.b));
            const float mn = std::min(c.r, std::min(c.g, c.b));
            return (mx <= 0.0f) ? 0.0f : (mx - mn) / mx;
        };
        check(saturation(restrained) < saturation(raw),
              "lane " + std::to_string(i) + ": restrained identity must be less saturated than raw");
    }
}

} // namespace

int main() {
    testInkOutranksBodyForEveryIdentity();
    testSelectionLiftsRatherThanDarkens();
    testBodyStaysVisibleAgainstCanvas();
    testLaneIdentityRestraintIsNotAnIdentityFunction();

    if (failures > 0) {
        std::cerr << "[FAIL] TimelineClipContrastTest: " << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "[PASS] TimelineClipContrastTest\n";
    return 0;
}
