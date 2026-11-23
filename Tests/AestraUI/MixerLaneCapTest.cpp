// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Mixer rework slice 3: the strip lane's geometry and the dished cap's gradient (p-mixer2 variant F, locked).
// Pure headers, no libraries.

#include "CapLook.h"
#include "MixerLane.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace AestraUI;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}
bool near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }
bool nearRgb(const CapLook::Rgb& a, const CapLook::Rgb& b) { return near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b); }
} // namespace

int main() {
    std::cout << "[1] the lane: 16 label, 5, 38 track, 5, 18 meter\n";
    check(MixerLane::kFaderW == 59.0f, "label + gap + track = 59");
    check(MixerLane::kGroupW == 82.0f, "the whole lane is 82");
    check(MixerLane::groupX(4.0f, 88.0f) == 7.0f, "centred in an 88px column starting at 4");
    check(MixerLane::trackX(7.0f) == 28.0f && MixerLane::meterX(7.0f) == 71.0f, "track at +21, meter at +64");
    check(MixerLane::slotCentre() == 40.0f, "the slot is centred in the 38px track");
    check(MixerLane::leftTickX() == 22.0f, "left tick starts 1px in from the track");
    check(MixerLane::rightTickX() == 50.0f, "right tick starts 8px + 1px in from the track's right edge");
    check(MixerLane::rightTickX() + MixerLane::kTickLen == MixerLane::kLabelW + MixerLane::kGap + MixerLane::kTrackW - 1.0f,
          "right tick's end is 1px inside the track's right edge");
    check(MixerLane::groupX(0.0f, 70.0f) <= 0.0f, "a column narrower than the lane never shifts it right");

    std::cout << "[2] the cap gradient stops\n";
    {
        const CapLook::Rgb base{0.4f, 0.2f, 0.6f};
        check(nearRgb(CapLook::faceColour(base, 0.0f), {0.4f * 0.8f, 0.2f * 0.8f, 0.6f * 0.8f}), "top: 20% black");
        check(nearRgb(CapLook::faceColour(base, 0.28f), {0.4f + 0.6f * 0.14f, 0.2f + 0.8f * 0.14f, 0.6f + 0.4f * 0.14f}),
              "28%: 14% white");
        check(nearRgb(CapLook::faceColour(base, 0.5f), base), "50%: the base itself");
        check(nearRgb(CapLook::faceColour(base, 0.72f), {0.4f + 0.6f * 0.10f, 0.2f + 0.8f * 0.10f, 0.6f + 0.4f * 0.10f}),
              "72%: 10% white");
        check(nearRgb(CapLook::faceColour(base, 1.0f), {0.4f * 0.74f, 0.2f * 0.74f, 0.6f * 0.74f}), "bottom: 26% black");
        check(nearRgb(CapLook::faceColour(base, 0.39f), {(CapLook::faceColour(base, 0.28f).r + base.r) / 2.0f,
                                                          (CapLook::faceColour(base, 0.28f).g + base.g) / 2.0f,
                                                          (CapLook::faceColour(base, 0.28f).b + base.b) / 2.0f}),
              "between two stops it blends linearly");
        check(nearRgb(CapLook::faceColour(base, -3.0f), CapLook::faceColour(base, 0.0f)) &&
                  nearRgb(CapLook::faceColour(base, 9.0f), CapLook::faceColour(base, 1.0f)),
              "outside 0..1 clamps");
        check(CapLook::faceColour(base, 0.28f).g > CapLook::faceColour(base, 0.5f).g &&
                  CapLook::faceColour(base, 0.0f).g < CapLook::faceColour(base, 0.5f).g,
              "lit lip above the base, shaded top and bottom edges");
    }

    std::cout << "[3] the cap is 24 by 34, radius 3; rows keep the rounded corners\n";
    check(CapLook::kW == 24.0f && CapLook::kH == 34.0f && CapLook::kRadius == 3.0f && CapLook::kIndexH == 2.0f, "size");
    check(CapLook::cornerInset(0, 34.0f, 3.0f) >= 1.0f, "the top row is pulled in");
    check(CapLook::cornerInset(33, 34.0f, 3.0f) == CapLook::cornerInset(0, 34.0f, 3.0f), "the bottom row mirrors it");
    check(CapLook::cornerInset(3, 34.0f, 3.0f) == 0.0f && CapLook::cornerInset(17, 34.0f, 3.0f) == 0.0f, "rows past the radius are full width");
    check(CapLook::cornerInset(0, 34.0f, 3.0f) >= CapLook::cornerInset(1, 34.0f, 3.0f), "insets shrink toward the middle");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
