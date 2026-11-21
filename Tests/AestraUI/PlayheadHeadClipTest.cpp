// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The playhead head is a whole marker at every position, including the ruler's left and right edges (owner, 21 Nov 2026: at
// beat 1 the Arsenal ruler's playhead looked tucked in, folded to half a marker, because the ruler clipped to its own bounds and
// the head is centred on the line, so half of it sat outside). Pure geometry from PlayheadMarker.h.

#include "PlayheadMarker.h"

#include <iostream>
#include <string>

using namespace AestraUI;
namespace PH = AestraUI::Playhead;

namespace {
int g_failures = 0, g_checks = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::cout << "  FAIL " << what << "\n";
        ++g_failures;
    }
}
bool contains(const NUIRect& outer, const NUIRect& in) {
    return in.x >= outer.x - 1e-4f && in.right() <= outer.right() + 1e-4f && in.y >= outer.y - 1e-4f && in.bottom() <= outer.bottom() + 1e-4f;
}
} // namespace

int main() {
    // The Arsenal ruler (bounds from ArsenalPanel: grid left, 41 px tall) and the piano roll ruler (28 px tall), both at fractional x.
    for (const NUIRect ruler : {NUIRect(575.0f, 233.0f, 760.0f, 41.0f), NUIRect(60.5f, 100.0f, 900.0f, 28.0f), NUIRect(0.0f, 0.0f, 300.0f, 20.0f)}) {
        for (const float scale : {1.0f, 1.25f}) {
            for (const float t : {0.0f, 0.001f, 0.25f, 0.5f, 0.999f, 1.0f}) {
                const float x = ruler.x + ruler.width * t;
                const NUIRect head = PH::headRect(x, ruler.bottom(), scale);
                const std::string at = "ruler " + std::to_string(static_cast<int>(ruler.width)) + " px, position " + std::to_string(t) + ", scale " + std::to_string(scale) + ": ";
                check(contains(PH::headClip(ruler, scale), head), at + "the whole head is inside the clip");
                check(head.y >= ruler.y - 1e-4f && head.bottom() <= ruler.bottom() + 1e-4f, at + "and inside the ruler vertically, its point on the ruler's bottom edge");
                check(std::fabs((head.x + head.width * 0.5f) - (std::floor(x) + 0.5f)) < 1e-3f, at + "centred on the line");
            }
        }
    }
    // (negative control) the old clip, the ruler's own bounds, cut the head at both ends.
    const NUIRect ruler(575.0f, 233.0f, 760.0f, 41.0f);
    check(!contains(ruler, PH::headRect(ruler.x, ruler.bottom())), "(negative control) at beat 1 the head sticks out of the ruler's own bounds: clipping to them folded it");
    check(!contains(ruler, PH::headRect(ruler.right(), ruler.bottom())), "(negative control) so does a head at the right edge");
    std::cout << g_checks << " checks, " << g_failures << " failure(s)\n";
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
