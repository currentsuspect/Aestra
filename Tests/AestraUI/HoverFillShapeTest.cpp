// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Hover fills follow the shape of what is hovered (Depth::hoverFill, Depth::rowHoverFill, NUISegmentedControl::segmentThumb).
// The calls are recorded and rasterised at the pixel centres of the hovered rect's corners: the outermost pixels of a hover over a
// capsule or a rounded row are NOT painted, which is what made the old rectangles poke out of rounded shapes.

#include "../Support/NullRenderer.h"
#include "NUIDepth.h"
#include "NUISegmentedControl.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace AestraUI;

namespace {
int g_failures = 0, g_checks = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::cout << "  FAIL " << what << "\n";
        ++g_failures;
    }
}

struct Fill {
    NUIRect rect;
    float radius; // 0: a plain rectangle
};

class Recorder final : public Aestra::Testing::NullRenderer {
public:
    std::vector<Fill> fills;
    void fillRect(const NUIRect& r, const NUIColor&) override { fills.push_back({r, 0.0f}); }
    void fillRoundedRect(const NUIRect& r, float radius, const NUIColor&) override { fills.push_back({r, radius}); }
};

bool inside(const Fill& f, float x, float y) {
    if (x < f.rect.x || x > f.rect.right() || y < f.rect.y || y > f.rect.bottom()) return false;
    if (f.radius <= 0.0f) return true;
    const float cx = std::clamp(x, f.rect.x + f.radius, f.rect.right() - f.radius);
    const float cy = std::clamp(y, f.rect.y + f.radius, f.rect.bottom() - f.radius);
    return std::hypot(x - cx, y - cy) <= f.radius;
}
bool painted(const std::vector<Fill>& fills, float x, float y) {
    for (const auto& f : fills)
        if (inside(f, x, y)) return true;
    return false;
}
} // namespace

int main() {
    const NUIColor c(1, 1, 1, 0.1f);

    // A hover over a capsule (a pill chip, a tab) is a capsule: the corner pixels are not painted, the middle is.
    for (const NUIRect pill : {NUIRect(10, 10, 80, 22), NUIRect(10.5f, 40.5f, 60, 20), NUIRect(0, 0, 30, 30)}) {
        Recorder r;
        Depth::hoverFill(r, pill, 100.0f, c); // a radius larger than the shape allows is held to a capsule
        check(r.fills.size() == 1 && r.fills[0].radius > 0.0f && std::fabs(r.fills[0].radius - std::min(pill.width, pill.height) * 0.5f) < 1e-4f,
              "a hover over a capsule is one rounded fill at half the height");
        check(!painted(r.fills, pill.x + 0.5f, pill.y + 0.5f) && !painted(r.fills, pill.right() - 0.5f, pill.y + 0.5f) &&
                  !painted(r.fills, pill.x + 0.5f, pill.bottom() - 0.5f) && !painted(r.fills, pill.right() - 0.5f, pill.bottom() - 0.5f),
              "the outermost corner pixels of the hovered capsule are not painted");
        check(painted(r.fills, pill.x + pill.width * 0.5f, pill.y + pill.height * 0.5f), "the middle is painted");
    }
    // A rounded button with a 5 px radius: corners cut, edges full.
    {
        Recorder r;
        const NUIRect b(20, 20, 60, 28);
        Depth::hoverFill(r, b, 5.0f, c);
        check(!painted(r.fills, b.x + 0.5f, b.y + 0.5f) && painted(r.fills, b.x + 6.0f, b.y + 0.5f) && painted(r.fills, b.x + 0.5f, b.y + 14.0f),
              "a 5 px hover is cut at its corners and full along its edges");
    }
    // A sharp component (radius 0) stays a rectangle: its own shape.
    {
        Recorder r;
        const NUIRect b(20, 20, 60, 28);
        Depth::hoverFill(r, b, 0.0f, c);
        check(r.fills.size() == 1 && r.fills[0].radius == 0.0f && painted(r.fills, b.x + 0.5f, b.y + 0.5f), "a sharp component's hover is a rectangle");
    }
    // A row at the bottom of a rounded panel takes the panel's corner at the bottom and stays square against the row above.
    {
        const NUIRect panel(0, 0, 200, 300);
        Recorder r;
        const NUIRect row(0, 272, 200, 28);
        Depth::rowHoverFill(r, row, panel, 10.0f, c);
        check(!painted(r.fills, row.x + 0.5f, row.bottom() - 0.5f) && !painted(r.fills, row.right() - 0.5f, row.bottom() - 0.5f),
              "a bottom row does not poke out of the panel's rounded bottom corners");
        check(painted(r.fills, row.x + 0.5f, row.y + 0.5f) && painted(r.fills, row.right() - 0.5f, row.y + 0.5f),
              "and stays square at its top, against the row above");
        Recorder top;
        const NUIRect first(0, 0, 200, 28);
        Depth::rowHoverFill(top, first, panel, 10.0f, c);
        check(!painted(top.fills, first.x + 0.5f, first.y + 0.5f) && painted(top.fills, first.x + 0.5f, first.bottom() - 0.5f),
              "a top row takes the top corners and is square below");
        Recorder mid;
        Depth::rowHoverFill(mid, NUIRect(0, 100, 200, 28), panel, 10.0f, c);
        check(painted(mid.fills, 0.5f, 100.5f) && painted(mid.fills, 0.5f, 127.5f), "a row in the middle is a plain rectangle");
        Recorder flat;
        Depth::rowHoverFill(flat, row, panel, 0.0f, c);
        check(painted(flat.fills, row.x + 0.5f, row.bottom() - 0.5f), "a square panel gives square rows");
    }
    // The browser's tabs: the hover shape of each segment sits inside the pill's own capsule, so it can never poke out of it.
    {
        const NUIRect well(12, 8, 240, 28);
        for (int i = 0; i < 3; ++i) {
            const NUIRect t = NUISegmentedControl::segmentThumb(well, 3, static_cast<float>(i));
            Recorder r;
            Depth::hoverFill(r, t, t.height * 0.5f, c);
            const Fill bed{well, well.height * 0.5f};
            check(inside(bed, t.x + 0.5f, t.y + t.height * 0.5f) && inside(bed, t.right() - 0.5f, t.y + t.height * 0.5f) &&
                      inside(bed, t.x + t.height * 0.5f, t.y + 0.5f) && inside(bed, t.right() - t.height * 0.5f, t.bottom() - 0.5f),
                  "tab " + std::to_string(i) + "'s hover capsule lies inside the pill");
            check(!painted(r.fills, t.x + 0.5f, t.y + 0.5f) && !painted(r.fills, t.right() - 0.5f, t.bottom() - 0.5f),
                  "and its own corners are cut");
        }
        // (negative control) the old hover: a 3 px rectangle over the whole 24 px tab rect paints corner pixels outside a capsule
        const NUIRect tab(well.x + 2, well.y + 2, 76, 24);
        Recorder old;
        old.fillRoundedRect(tab, 3.0f, c);
        check(painted(old.fills, tab.x + 1.2f, tab.y + 1.2f), "(negative control) the old 3 px tab hover painted its corner pixels");
        check(!inside(Fill{NUISegmentedControl::segmentThumb(well, 3, 0.0f), 11.0f}, tab.x + 1.2f, tab.y + 1.2f), "which the capsule leaves unpainted");
    }

    std::cout << g_checks << " checks, " << g_failures << " failure(s)\n";
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
