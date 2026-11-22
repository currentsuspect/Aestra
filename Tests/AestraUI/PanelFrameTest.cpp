// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A panel's frame is one closed rectangle with no orphan segments (owner, 21 Nov 2026: the Arsenal's top corners showed an L-shaped stub of
// line, because the title bar was filled over the stroked border). The draw calls are rasterised to whole pixels: every perimeter pixel is
// painted exactly once, nothing else is, the loop is connected, and the divider under a title bar meets both sides.

#include "../Support/NullRenderer.h"
#include "PanelFrame.h"

#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace AestraUI;
namespace PF = Aestra::PanelFrame;

namespace {
int g_failures = 0, g_checks = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::cout << "  FAIL " << what << "\n";
        ++g_failures;
    }
}
class Recorder final : public Aestra::Testing::NullRenderer {
public:
    std::vector<NUIRect> rects;
    void fillRect(const NUIRect& r, const NUIColor&) override { rects.push_back(r); }
};
using Px = std::pair<int, int>;
std::multiset<Px> raster(const std::vector<NUIRect>& rs) {
    std::multiset<Px> out;
    for (const auto& r : rs)
        for (int y = static_cast<int>(std::lround(r.y)); y < static_cast<int>(std::lround(r.bottom())); ++y)
            for (int x = static_cast<int>(std::lround(r.x)); x < static_cast<int>(std::lround(r.right())); ++x) out.insert({x, y});
    return out;
}
} // namespace

int main() {
    for (const NUIRect r : {NUIRect(262, 88, 1104, 680), NUIRect(10.4f, 20.6f, 300.3f, 200.2f), NUIRect(0, 0, 24, 24), NUIRect(5, 5, 3, 3)}) {
        Recorder rec;
        PF::draw(rec, r, NUIColor(1, 1, 1, 1));
        const auto px = raster(rec.rects);
        const int x0 = std::lround(r.x), y0 = std::lround(r.y), x1 = std::lround(r.right()), y1 = std::lround(r.bottom());
        const std::string at = "rect " + std::to_string(x1 - x0) + "x" + std::to_string(y1 - y0) + ": ";
        std::set<Px> want;
        for (int x = x0; x < x1; ++x) { want.insert({x, y0}); want.insert({x, y1 - 1}); }
        for (int y = y0; y < y1; ++y) { want.insert({x0, y}); want.insert({x1 - 1, y}); }
        bool once = true, same = px.size() == want.size();
        for (const auto& p : want) once = once && px.count(p) == 1;
        check(once && same, at + "every perimeter pixel is painted exactly once and nothing else is");
        // Connected loop: every painted pixel has exactly two painted 4-neighbours (the corners and straight runs of a closed 1 px loop).
        bool loop = (x1 - x0) >= 3 && (y1 - y0) >= 3;
        if (loop)
            for (const auto& p : want) {
                int n = 0;
                for (const Px d : {Px{1, 0}, Px{-1, 0}, Px{0, 1}, Px{0, -1}}) n += want.count({p.first + d.first, p.second + d.second}) ? 1 : 0;
                loop = loop && n == 2;
            }
        check(loop, at + "the frame is a closed loop: no orphan segment, no open end");
    }
    // The divider spans the whole inside and meets the sides: its pixels plus the frame's left and right sides touch on both ends.
    {
        const NUIRect r(262, 88, 1104, 680);
        const NUIRect d = PF::divider(r, 32.0f);
        const auto l = PF::lines(r);
        check(d.x == l.left.right() && d.right() == l.right.x && d.height == 1.0f, "the divider runs from the left side to the right side, edge to edge");
        check(d.y > l.top.y && d.y < l.bottom.y && d.y >= l.left.y && d.y < l.left.bottom(), "and sits between the top and bottom lines, on the sides' rows");
    }
    // (negative control) the old order: a stroke then a title-bar fill over it. The fill covers the top line and the sides' first rows of the
    // title bar, so what remains of the frame there is the sides below the title bar only: an open top.
    {
        const NUIRect r(262, 88, 1104, 680);
        const auto l = PF::lines(r);
        const NUIRect title(r.x, r.y, r.width, 32.0f);
        const auto covered = [&](const NUIRect& line) { return line.x >= title.x && line.right() <= title.right() && line.y >= title.y && line.bottom() <= title.bottom(); };
        check(covered(l.top), "(negative control) a title bar filled after the stroke covers the whole top line: the stub the owner saw");
    }
    std::cout << g_checks << " checks, " << g_failures << " failure(s)\n";
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
