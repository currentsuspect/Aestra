// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Guards the timeline clip contrast contract: a clip is a flat surface in its
// identity hue with content drawn on it, so the waveform/note ink and the label
// must stay meaningfully LIGHTER than the clip body they sit on, for every lane
// identity and in both selection states.
//
// The direction of that gap is the part that changed (spec 2 §1, 2026-09-19).
// It used to point the other way — ink deepened toward black, label colour
// chosen per hue by luminance — which meant the same clip's foreground flipped
// between near-black and near-white depending on which channel it routed to,
// and on the dark hues the near-black ink sat on a near-black body. The tests
// below are deliberately asymmetric about that direction now: an accidental
// return to "deepen the ink" fails them rather than merely re-tuning them.
//
// Deliberately does NOT assert the brightness constants themselves. Those are
// taste values the owner is expected to retune; a test that restates 0.185f only
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
using AestraUI::clipForegroundInk;
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
// rather than to a raw channel sum. Computed here rather than reusing the
// header's helper: the test must be able to catch a wrong luminance formula in
// the production code, which it cannot do if both sides call the same function.
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
// under the current measured minimum so ordinary retuning does not trip it, but
// far enough above 1.0 that a collapse is caught.
constexpr float kMinInkBodyContrast = 3.0f;

// WCAG AA for normal text. Clip labels are small, so this is the floor, not the
// target; the header's black wash pushes the real figure well past it.
constexpr float kMinLabelContrast = 4.5f;

float saturation(const NUIColor& c) {
    const float mx = std::max(c.r, std::max(c.g, c.b));
    const float mn = std::min(c.r, std::min(c.g, c.b));
    return (mx <= 0.0f) ? 0.0f : (mx - mn) / mx;
}

std::string describe(const NUIColor& c) {
    return "(" + std::to_string(static_cast<int>(c.r * 255.0f)) + "," +
           std::to_string(static_cast<int>(c.g * 255.0f)) + "," +
           std::to_string(static_cast<int>(c.b * 255.0f)) + ")";
}

// Every identity the timeline can hand the contract: the palette plus the
// out-of-range grey that paletteIndexToARGB() falls back to.
int identityCount() { return PALETTE_SIZE + 1; }

NUIColor identityAt(int i) { return NUIColor::fromARGB(paletteIndexToARGB(i)); }

// The core contract, over every identity, in both selection states.
void testInkStaysLighterThanBodyForEveryIdentity() {
    for (int i = 0; i < identityCount(); ++i) {
        const NUIColor identity = identityAt(i);

        for (const bool selected : {false, true}) {
            const NUIColor body = clipBodyTone(identity, selected);
            const NUIColor ink = waveformInkTone(identity, selected);
            const std::string where =
                "lane " + std::to_string(i) + (selected ? " (selected)" : " (unselected)");

            check(relativeLuminance(ink) > relativeLuminance(body),
                  where + ": waveform ink " + describe(ink) + " must be lighter than body " +
                      describe(body));

            const float ratio = contrastRatio(ink, body);
            check(ratio >= kMinInkBodyContrast,
                  where + ": ink-vs-body contrast " + std::to_string(ratio) + ":1 fell below the " +
                      std::to_string(kMinInkBodyContrast) + ":1 floor");
        }
    }
}

// The whole point of spec 2 §1: routing a clip to a different channel changes
// its hue and nothing else about how readable its foreground is. A per-channel
// special case, or a rule that flips polarity on light hues, shows up here as a
// spread between the best and worst identity.
void testForegroundLegibilityDoesNotDependOnRouting() {
    float worstInk = 1e9f;
    float bestInk = 0.0f;
    for (int i = 0; i < identityCount(); ++i) {
        const NUIColor body = clipBodyTone(identityAt(i), false);
        const float ratio = contrastRatio(waveformInkTone(identityAt(i), false), body);
        worstInk = std::min(worstInk, ratio);
        bestInk = std::max(bestInk, ratio);
    }
    // A modest spread is fine — hues differ. An order-of-magnitude spread means
    // some channel is getting a different treatment from the others.
    check(bestInk <= worstInk * 1.8f,
          "ink-vs-body contrast varies too much across routings: worst " + std::to_string(worstInk) +
              ":1, best " + std::to_string(bestInk) + ":1");
}

// Clip bodies carry the same hue the mixer strips paint. Desaturating them is
// what made clips read dull and out of place, so guard against it returning —
// the luminance cap is allowed to darken, never to drain colour.
void testBodyKeepsPaletteSaturation() {
    for (int i = 0; i < PALETTE_SIZE; ++i) {
        const NUIColor raw = identityAt(i);
        const float bodySat = saturation(clipBodyTone(raw, false));
        check(bodySat >= saturation(raw) * 0.95f,
              "lane " + std::to_string(i) + ": clip body saturation " + std::to_string(bodySat) +
                  " drifted below the palette hue's " + std::to_string(saturation(raw)));
    }
}

// The label rule has no branch any more: one light ink, on every clip, over any
// header wash the clip grammar cares to apply. Sweeping the wash instead of
// restating the production constant keeps this a property, not a copy of a
// literal — and proves the ink never comes out as the near-black it used to be
// on the light hues.
void testLabelInkIsAlwaysTheLightOne() {
    const NUIColor nearBlack(0.04f, 0.04f, 0.05f, 1.0f);
    for (int i = 0; i < identityCount(); ++i) {
        for (const bool selected : {false, true}) {
            const NUIColor body = clipBodyTone(identityAt(i), selected);
            for (int washStep = 0; washStep <= 8; ++washStep) {
                const float wash = static_cast<float>(washStep) * 0.05f; // 0% .. 40% black
                const NUIColor surface =
                    NUIColor::lerp(body, NUIColor(0.0f, 0.0f, 0.0f, 1.0f), wash);
                const NUIColor ink = clipForegroundInk(surface, 1.0f);
                const std::string where = "lane " + std::to_string(i) +
                                          (selected ? " (selected)" : " (unselected)") + " wash " +
                                          std::to_string(wash);

                check(relativeLuminance(ink) > relativeLuminance(surface),
                      where + ": label ink " + describe(ink) + " must be lighter than its surface " +
                          describe(surface));

                const float ratio = contrastRatio(ink, surface);
                check(ratio >= kMinLabelContrast,
                      where + ": label contrast " + std::to_string(ratio) + ":1 fell below " +
                          std::to_string(kMinLabelContrast) + ":1");

                check(ratio >= contrastRatio(nearBlack, surface),
                      where + ": near-black text would read better than the light ink — the "
                              "luminance cap has stopped capping");
            }
        }
    }
}

// Selecting a clip must not darken it — the selected body carries state, and an
// inverted delta would make selection read as "disabled".
void testSelectionLiftsRatherThanDarkens() {
    for (int i = 0; i < PALETTE_SIZE; ++i) {
        const NUIColor identity = identityAt(i);
        const float plain = relativeLuminance(clipBodyTone(identity, false));
        const float selected = relativeLuminance(clipBodyTone(identity, true));
        check(selected > plain, "lane " + std::to_string(i) +
                                   ": selected body must be brighter than unselected");
    }
}

// The body must stay clearly visible against the timeline's black canvas, or the
// cap has gone too far and clips vanish instead of receding.
void testBodyStaysVisibleAgainstCanvas() {
    const NUIColor canvas(0.0f, 0.0f, 0.0f, 1.0f);
    for (int i = 0; i < identityCount(); ++i) {
        const NUIColor body = clipBodyTone(identityAt(i), false);
        const float ratio = contrastRatio(body, canvas);
        check(ratio >= 3.0f, "lane " + std::to_string(i) + ": body-vs-canvas contrast " +
                                 std::to_string(ratio) + ":1 is too low to see the clip");
    }
}

// The cap is the mechanism the light-foreground rule rests on. Prove it engages
// on a hue that needs it (a raw palette hue is brighter than the ceiling), and
// that engaging it is a pure brightness move.
void testLuminanceCapDarkensWithoutShiftingHue() {
    bool sawACappedHue = false;
    for (int i = 0; i < PALETTE_SIZE; ++i) {
        const NUIColor raw = identityAt(i);
        const NUIColor body = clipBodyTone(raw, false);
        if (relativeLuminance(raw) > relativeLuminance(body) + 1e-4f) {
            sawACappedHue = true;
        }
        check(relativeLuminance(body) <= AestraUI::kClipBodyLuminanceCeiling + 1e-3f,
              "lane " + std::to_string(i) + ": body luminance " +
                  std::to_string(relativeLuminance(body)) + " exceeded the ceiling");

        // Channel ratios are what carry hue. One scalar scaling all three leaves
        // them identical; anything else has moved the colour, not just its level.
        const float rawMax = std::max(raw.r, std::max(raw.g, raw.b));
        const float bodyMax = std::max(body.r, std::max(body.g, body.b));
        if (rawMax > 0.0f && bodyMax > 0.0f) {
            const float scale = bodyMax / rawMax;
            check(std::abs(body.r - raw.r * scale) < 0.01f && std::abs(body.g - raw.g * scale) < 0.01f &&
                      std::abs(body.b - raw.b * scale) < 0.01f,
                  "lane " + std::to_string(i) + ": capping shifted the hue — " + describe(raw) +
                      " became " + describe(body));
        }
    }
    check(sawACappedHue, "no palette hue was actually capped — the ceiling is inert and the test "
                         "above proves nothing");
}

// Restraint has to actually restrain: a no-op would silently reintroduce the raw
// palette everywhere the helper is used.
void testLaneIdentityRestraintIsNotAnIdentityFunction() {
    for (int i = 0; i < PALETTE_SIZE; ++i) {
        const NUIColor raw = identityAt(i);
        const NUIColor restrained = AestraUI::restrainLaneIdentityColor(raw, 1.0f);
        check(relativeLuminance(restrained) < relativeLuminance(raw),
              "lane " + std::to_string(i) + ": restrained identity must be darker than the raw palette hue");

        check(saturation(restrained) < saturation(raw),
              "lane " + std::to_string(i) + ": restrained identity must be less saturated than raw");
    }
}

} // namespace

int main() {
    testInkStaysLighterThanBodyForEveryIdentity();
    testForegroundLegibilityDoesNotDependOnRouting();
    testBodyKeepsPaletteSaturation();
    testLabelInkIsAlwaysTheLightOne();
    testSelectionLiftsRatherThanDarkens();
    testBodyStaysVisibleAgainstCanvas();
    testLuminanceCapDarkensWithoutShiftingHue();
    testLaneIdentityRestraintIsNotAnIdentityFunction();

    if (failures > 0) {
        std::cerr << "[FAIL] TimelineClipContrastTest: " << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "[PASS] TimelineClipContrastTest\n";
    return 0;
}
