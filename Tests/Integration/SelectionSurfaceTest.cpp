// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Selection System Spec, section 2.3 and 5: selecting repaints what changed and nothing else. A fake surface of ten
// rows drives the real SelectionModel; repaintRects() must name exactly the rows whose selected state flipped (and a
// primary that moved), only for the surface asked, and nothing for an id that is not on screen.

#include "SelectionSurface.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace Aestra;
using S = SelectionSurface;
using M = SelectMode;
using R = Reason;

namespace {
int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::cout << "  FAIL " << what << "\n";
        ++g_failures;
    }
}

// Ten rows 20 px tall, ids 100..109, row i at y = 20 * i. Rows listed in `hidden` are scrolled out of view.
class RowSurface : public ISelectionSurface {
public:
    RowSurface(S which, uint64_t firstId) : which_(which), first_(firstId) {}
    S surface() const override { return which_; }
    std::optional<AestraUI::NUIRect> boundsOf(uint64_t id) const override {
        if (id < first_ || id >= first_ + 10 || hidden.count(id)) return std::nullopt;
        return AestraUI::NUIRect(0.0f, 20.0f * static_cast<float>(id - first_), 100.0f, 20.0f);
    }
    void onSelectionChanged(const SelectionChange&) override { ++notified; }
    std::map<uint64_t, bool> hidden;
    int notified = 0;

private:
    S which_;
    uint64_t first_;
};

std::vector<int> rows(const std::vector<AestraUI::NUIRect>& rects) {
    std::vector<int> out;
    for (const auto& r : rects) out.push_back(static_cast<int>(r.y / 20.0f));
    return out;
}
} // namespace

int main() {
    // Selecting row 3 repaints row 3 only; moving to row 5 repaints rows 3 and 5 only.
    {
        SelectionModel m;
        RowSurface arsenal(S::ArsenalUnits, 100);
        SelectionChange last;
        Subscription sub = m.subscribe([&](const SelectionChange& c) { last = c; });
        m.select(S::ArsenalUnits, {103}, M::Replace, R::Click);
        check(rows(repaintRects(arsenal, last)) == std::vector<int>{3}, "selecting row 3 repaints row 3 only");
        m.select(S::ArsenalUnits, {105}, M::Replace, R::Click);
        check(rows(repaintRects(arsenal, last)) == (std::vector<int>{3, 5}), "moving to row 5 repaints rows 3 and 5 only");
        m.select(S::ArsenalUnits, {105}, M::Replace, R::Click);
        check(rows(repaintRects(arsenal, last)) == (std::vector<int>{3, 5}), "re-selecting the same row publishes nothing new");
    }
    // Extending a multi-selection repaints only the added rows, plus a primary that moved.
    {
        SelectionModel m;
        RowSurface s(S::ArsenalUnits, 100);
        SelectionChange last;
        Subscription sub = m.subscribe([&](const SelectionChange& c) { last = c; });
        m.select(S::ArsenalUnits, {101, 102}, M::Replace, R::Click);
        m.select(S::ArsenalUnits, {107}, M::Add, R::Marquee);
        check(rows(repaintRects(s, last)) == std::vector<int>{7}, "adding row 7 repaints row 7 only (the primary does not move)");
        m.select(S::ArsenalUnits, {102}, M::Toggle, R::Click);
        // Row 2 was the primary: it repaints, and so does row 7, which becomes the primary (its look changes too).
        check(rows(repaintRects(s, last)) == (std::vector<int>{2, 7}), "toggling the primary off repaints it and the row that becomes primary");
        m.select(S::ArsenalUnits, {101}, M::Toggle, R::Click);
        check(rows(repaintRects(s, last)) == std::vector<int>{1}, "toggling a non-primary row off repaints that row only");
    }
    // A press in another surface clears: the old surface repaints its rows, the new one is untouched.
    {
        SelectionModel m;
        RowSurface arsenal(S::ArsenalUnits, 100);
        RowSurface timeline(S::TimelineClips, 200);
        SelectionChange last;
        Subscription sub = m.subscribe([&](const SelectionChange& c) { last = c; });
        m.select(S::ArsenalUnits, {101, 104}, M::Replace, R::Click);
        m.pressedIn(S::TimelineClips, R::Click);
        check(rows(repaintRects(arsenal, last)) == (std::vector<int>{1, 4}), "a press elsewhere repaints the cleared rows");
        check(repaintRects(timeline, last).empty(), "the surface that was pressed in has nothing stale");
    }
    // Switching surface in one select(): the old surface repaints what it lost, the new one what it gained.
    {
        SelectionModel m;
        RowSurface arsenal(S::ArsenalUnits, 100);
        RowSurface timeline(S::TimelineClips, 200);
        SelectionChange last;
        Subscription sub = m.subscribe([&](const SelectionChange& c) { last = c; });
        m.select(S::ArsenalUnits, {102}, M::Replace, R::Click);
        m.select(S::TimelineClips, {206}, M::Replace, R::Click);
        check(rows(repaintRects(arsenal, last)) == std::vector<int>{2}, "the old surface repaints the row it lost");
        check(rows(repaintRects(timeline, last)) == std::vector<int>{6}, "the new surface repaints the row it gained");
    }
    // An id that is not on screen needs no repaint.
    {
        SelectionModel m;
        RowSurface s(S::ArsenalUnits, 100);
        s.hidden[104] = true;
        SelectionChange last;
        Subscription sub = m.subscribe([&](const SelectionChange& c) { last = c; });
        m.select(S::ArsenalUnits, {104, 105}, M::Replace, R::Click);
        check(rows(repaintRects(s, last)) == std::vector<int>{5}, "a scrolled-out row is not repainted");
    }
    // The interface is usable through a base pointer and the side-effect hook is the surface's own.
    {
        RowSurface s(S::BrowserRows, 300);
        ISelectionSurface* base = &s;
        base->onSelectionChanged(SelectionChange{});
        check(s.notified == 1 && base->surface() == S::BrowserRows, "a surface is driven through ISelectionSurface");
    }
    std::cout << "SelectionSurfaceTest: " << g_checks << " checks, " << g_failures << " failure(s)\n";
    if (g_failures == 0) std::cout << "ALL PASS\n";
    return g_failures == 0 ? 0 : 1;
}
