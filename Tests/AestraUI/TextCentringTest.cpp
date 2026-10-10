// © 2026 Aestra Studios — All Rights Reserved.
//
// SPEC 3 §3.2: the renderer's text-centring contract. drawText() takes the TOP of the line
// box (it adds the ascent itself); calculateOpticalTextY() must put the band between the
// baseline and the cap line on the requested centre, using the font's real cap height.
// A stub renderer supplies metrics whose cap height is deliberately far from the line box's
// centre, so a helper that centred the line box instead would fail here.

#include "../Support/NullRenderer.h"

#include <cmath>
#include <iostream>

namespace {
int g_failures = 0;

void check(bool ok, const char* what) {
    std::cout << (ok ? "PASS: " : "FAIL: ") << what << "\n";
    if (!ok) ++g_failures;
}

// ascent 12, descent 2, cap height 6: line-box centring lands (2 - 12 + 6) / 2 = 2 px away from cap centring.
class StubMetricsRenderer : public Aestra::Testing::NullRenderer {
public:
    float capHeight = 6.0f;
    FontMetrics getFontMetrics(float) const override {
        FontMetrics m;
        m.ascent = 12.0f;
        m.descent = 2.0f;
        m.lineHeight = 14.0f;
        m.capHeight = capHeight;
        return m;
    }
};
} // namespace

int main() {
    StubMetricsRenderer renderer;
    const float centre = 100.0f;

    // drawText() adds the ascent, so the baseline is y + ascent and the caps span
    // [baseline - capHeight, baseline]. Their midpoint must be the requested centre.
    const float y = renderer.calculateOpticalTextY(centre, 12.0f);
    const float baseline = y + 12.0f;
    check(std::abs((baseline - 6.0f * 0.5f) - centre) < 1e-4f, "the cap band is centred on the requested centre");

    const AestraUI::NUIRect rect(0.0f, 90.0f, 50.0f, 20.0f); // centre 100
    check(std::abs(renderer.calculateOpticalTextY(rect, 12.0f) - y) < 1e-4f, "the rect overload centres on the rect's middle");

    // Not the line box: with these metrics line-box centring sits 2 px away. Guards against
    // the helper quietly degrading into calculateTextY().
    check(std::abs(renderer.calculateTextY(rect, 12.0f) - y) > 1.5f, "it is not line-box centring (non-vacuous)");

    // A font with no cap-height metric falls back to 0.7 x size, never 0.
    renderer.capHeight = 0.0f;
    const float fallbackY = renderer.calculateOpticalTextY(centre, 10.0f);
    check(std::abs((fallbackY + 12.0f - 7.0f * 0.5f) - centre) < 1e-4f, "no cap height: falls back to 0.7 x size");

    if (g_failures == 0) {
        std::cout << "Text centring tests passed\n";
        return 0;
    }
    std::cout << g_failures << " text centring test(s) failed\n";
    return 1;
}
