// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Mixer slice 7: the inspector collapse handle reacts only over its tab (plus slop), not along the whole rail.

#include "InspectorHandle.h"

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
} // namespace

int main() {
    // Expanded: a 10px rail down the inspector's leading edge (x 983, 188..760). Collapsed: a 16px strip.
    struct Rail { const char* what; float x, y, w, h; };
    const Rail rails[] = {{"expanded rail", 983.0f, 188.0f, 10.0f, 572.0f}, {"collapsed rail", 1196.0f, 150.0f, 16.0f, 610.0f}};
    for (const auto& r : rails) {
        std::cout << "[" << r.what << "]\n";
        const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
        const auto tab = InspectorHandle::tab(r.x, r.y, r.w, r.h);
        const auto hit = InspectorHandle::hit(r.x, r.y, r.w, r.h);
        check(tab.w == 14.0f && tab.h == 40.0f, "the tab is 14x40");
        check(tab.x + tab.w * 0.5f == cx && tab.y + tab.h * 0.5f == cy, "centred on the rail");
        check(hit.contains(cx, cy), "the centre of the tab hits");
        check(hit.contains(cx, cy - 20.0f) && hit.contains(cx, cy + 19.0f), "the tab's top and bottom rows hit");
        check(hit.contains(cx, cy - 22.0f) && hit.contains(cx, cy + 22.0f), "a few px of slop above and below hits");
        check(!hit.contains(cx, cy - 30.0f) && !hit.contains(cx, cy + 30.0f), "beyond the slop misses");
        check(!hit.contains(cx, r.y + 5.0f) && !hit.contains(cx, r.y + r.h - 5.0f), "the rail's top and bottom ends miss (old full-height rail would hit)");
        check(!hit.contains(cx, cy + 120.0f), "a point on the old rail well below the tab misses");
        check(!hit.contains(cx + 14.0f, cy), "beside the tab misses");
        check(hit.x < tab.x && hit.y < tab.y && hit.w > tab.w, "the hit rect is the tab plus slop");
    }
    std::cout << "[short rail]\n";
    {
        const auto t = InspectorHandle::tab(0.0f, 0.0f, 10.0f, 25.0f);
        check(t.h == 25.0f && t.y == 0.0f, "a rail shorter than the tab shrinks it to fit");
        check(InspectorHandle::tab(0.0f, 0.0f, 10.0f, -5.0f).h == 0.0f, "no negative height");
    }
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
