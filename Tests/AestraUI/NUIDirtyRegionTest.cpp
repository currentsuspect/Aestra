// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// SPEC-028 R1: the invalidation region. The properties the renderer will rely on: it stays small, near rectangles
// join, a moved thing invalidates where it was as well as where it is, and past a fraction of the window it is
// simply "the whole window".

#include "Core/NUIDirtyRegion.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace AestraUI;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  PASS " : "  FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
bool same(const NUIRect& a, const NUIRect& b) {
    return std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.y - b.y) < 1e-3f && std::fabs(a.width - b.width) < 1e-3f &&
           std::fabs(a.height - b.height) < 1e-3f;
}

} // namespace

int main() {
    const NUIRect window(0, 0, 1366, 768);

    std::cout << "[1] empty, degenerate and non-finite input\n";
    {
        NUIDirtyRegion r;
        check(r.empty() && r.resolve(window).empty(), "nothing invalidated is an empty region (so: no present)");
        r.add(NUIRect(10, 10, 0, 20));
        r.add(NUIRect(10, 10, 20, -5));
        r.add(NUIRect(std::numeric_limits<float>::quiet_NaN(), 0, 10, 10));
        r.add(NUIRect(0, 0, std::numeric_limits<float>::infinity(), 10));
        check(r.empty(), "zero-size, negative and non-finite rectangles are ignored");
    }

    std::cout << "[2] separate rectangles stay separate; overlapping and near ones join\n";
    {
        NUIDirtyRegion r;
        r.add(NUIRect(0, 0, 100, 40));
        r.add(NUIRect(600, 400, 50, 50));
        check(r.rects().size() == 2, "two far-apart rectangles stay two (a number and a meter are not one big box)");
        r.add(NUIRect(90, 30, 40, 40)); // overlaps the first
        check(r.rects().size() == 2 && same(r.rects()[0].x < 100 ? r.rects()[0] : r.rects()[1], NUIRect(0, 0, 130, 70)),
              "an overlapping one joins it into their union");
        NUIDirtyRegion near;
        near.add(NUIRect(0, 0, 100, 40));
        near.add(NUIRect(104, 0, 100, 40)); // 4 px apart: inside the merge gap
        check(near.rects().size() == 1 && same(near.rects()[0], NUIRect(0, 0, 204, 40)), "rectangles 4 px apart join");
    }

    std::cout << "[3] joining is transitive\n";
    {
        NUIDirtyRegion r;
        r.add(NUIRect(0, 0, 50, 50));
        r.add(NUIRect(200, 0, 50, 50));
        r.add(NUIRect(25, 0, 200, 50)); // bridges the two
        check(r.rects().size() == 1 && same(r.rects()[0], NUIRect(0, 0, 250, 50)), "a rectangle that bridges two joins all three");
    }

    std::cout << "[4] the set stays small\n";
    {
        NUIDirtyRegion r;
        for (int i = 0; i < 15; ++i) r.add(NUIRect(static_cast<float>(i) * 90.0f, static_cast<float>(i % 2) * 300.0f, 20, 20));
        check(r.rects().size() <= 8, "fifteen scattered invalidations are capped at 8 rectangles (not fifteen tree walks)");
        // Whatever was joined, nothing that was invalidated may be left out.
        bool covered = true;
        for (int i = 0; i < 15; ++i) {
            const NUIRect inv(static_cast<float>(i) * 90.0f, static_cast<float>(i % 2) * 300.0f, 20, 20);
            bool inside = false;
            for (const auto& c : r.rects())
                inside = inside || (c.x <= inv.x + 1e-3f && c.y <= inv.y + 1e-3f && c.right() >= inv.right() - 1e-3f &&
                                    c.bottom() >= inv.bottom() - 1e-3f);
            covered = covered && inside;
        }
        check(covered, "and every invalidated rectangle is still inside the set");
    }

    std::cout << "[5] a moved thing invalidates where it was and where it is\n";
    {
        NUIDirtyRegion r;
        r.addMoved(NUIRect(500, 100, 2, 400), NUIRect(503, 100, 2, 400)); // a playhead, 3 px right
        check(r.rects().size() == 1 && r.rects()[0].x <= 500.0f && r.rects()[0].right() >= 505.0f,
              "both the old and the new playhead position are inside the region");
        NUIDirtyRegion jump;
        jump.addMoved(NUIRect(100, 100, 2, 400), NUIRect(900, 100, 2, 400)); // a seek across the screen
        bool old = false, now = false;
        for (const auto& c : jump.rects()) {
            old = old || (c.x <= 100.0f && c.right() >= 102.0f);
            now = now || (c.x <= 900.0f && c.right() >= 902.0f);
        }
        check(old && now, "a seek invalidates both ends, wherever they are");
    }

    std::cout << "[6] resolve: clip to the window, and the whole window past the fraction\n";
    {
        NUIDirtyRegion r;
        r.add(NUIRect(-50, -50, 100, 100)); // half off the top-left corner
        const auto clipped = r.resolve(window);
        check(clipped.size() == 1 && same(clipped[0], NUIRect(0, 0, 50, 50)), "a rectangle is clipped to the window");
        NUIDirtyRegion off;
        off.add(NUIRect(5000, 5000, 10, 10));
        check(off.resolve(window).empty(), "a rectangle outside the window resolves to nothing");

        NUIDirtyRegion big;
        big.add(NUIRect(0, 0, 1366, 400)); // 52% of the window
        const auto full = big.resolve(window);
        check(full.size() == 1 && same(full[0], window), "past half the window the answer is the whole window");
        NUIDirtyRegion small;
        small.add(NUIRect(0, 0, 100, 40));
        check(std::fabs(small.fractionOf(window) - (100.0f * 40.0f) / (1366.0f * 768.0f)) < 1e-4f,
              "fractionOf reports the share of the window (0.4% for a transport number)");
    }

    std::cout << "[7] clear\n";
    {
        NUIDirtyRegion r;
        r.add(NUIRect(0, 0, 10, 10));
        r.clear();
        check(r.empty() && r.fractionOf(window) == 0.0f, "clear empties the region");
    }

    if (g_failures == 0) {
        std::cout << "NUIDirtyRegionTest: all checks passed\n";
        return 0;
    }
    std::cout << "NUIDirtyRegionTest: " << g_failures << " failure(s)\n";
    return 1;
}
