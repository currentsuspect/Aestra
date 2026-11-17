// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-27 point 2, "a grouped rack": units sit under Drums, Synth and Audio headers with their counts, in unit
// order inside a group, and the positions drive layout, scrolling and drag-and-drop from one answer.

#include "ArsenalGridScroll.h"
#include "ArsenalRackLayout.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

using namespace Aestra;
using Audio::UnitGroup;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

} // namespace

int main() {
    std::cout << "[1] rows group under headers, in unit order inside a group\n";
    // Units made in this order: Kick(D) Bass(S) Snare(D) Vox(A) Pluck(S) Hat(D)
    const std::vector<UnitGroup> groups{UnitGroup::Drums, UnitGroup::Synth, UnitGroup::Drums,
                                        UnitGroup::Audio, UnitGroup::Synth, UnitGroup::Drums};
    const auto layout = ArsenalRackLayout::compute(groups);
    {
        const std::vector<size_t> want{0, 2, 5, 1, 4, 3};
        check(layout.displayOrder == want, "Drums (Kick, Snare, Hat), Synth (Bass, Pluck), Audio (Vox)");
        int headers = 0;
        for (const auto& it : layout.items)
            headers += it.kind == ArsenalRackItem::Kind::Header;
        check(headers == 3, "three headers");
        check(layout.items.front().kind == ArsenalRackItem::Kind::Header && layout.items.front().group == UnitGroup::Drums &&
                  layout.items.front().count == 3,
              "the first is Drums, counting 3");
    }

    std::cout << "[2] positions stack with no overlap\n";
    {
        bool ordered = true;
        float lastBottom = 0.0f;
        for (const auto& it : layout.items) {
            ordered = ordered && it.y >= lastBottom - 0.01f;
            lastBottom = it.y + it.h;
        }
        check(ordered, "every item starts at or below the one above");
        check(layout.contentHeight >= 3 * ArsenalRackLayout::kHeaderH + 6 * ArsenalRackLayout::kRowH,
              "the content height holds 3 headers and 6 rows");
        check(layout.rowY(0) < layout.rowY(1), "Kick (Drums) sits above Bass (Synth)");
    }

    std::cout << "[3] a group with no units has no header\n";
    {
        const auto only = ArsenalRackLayout::compute({UnitGroup::Synth, UnitGroup::Synth});
        check(only.items.size() == 3 && only.items.front().group == UnitGroup::Synth, "just Synth: one header and two rows");
        check(ArsenalRackLayout::compute({}).items.empty() && ArsenalRackLayout::compute({}).contentHeight == 0.0f, "an empty rack is empty");
    }

    std::cout << "[4] unknown groups land under Other, last\n";
    {
        const auto mixed = ArsenalRackLayout::compute({UnitGroup::Unknown, UnitGroup::Drums});
        check(mixed.displayOrder.size() == 2 && mixed.displayOrder[0] == 1 && mixed.displayOrder[1] == 0, "Drums first, then the unknown unit");
        check(std::string(ArsenalRackLayout::groupLabel(UnitGroup::Unknown)) == "Other", "labelled Other");
    }

    std::cout << "[5] drag-and-drop maps a position to a unit index\n";
    {
        check(layout.dropSlotForY(0.0f) == 0, "above everything is slot 0");
        check(layout.dropSlotForY(layout.contentHeight + 50.0f) == 6, "below everything is the end");
        // Just above Snare (display slot 1) = before the 2nd row in display order.
        const size_t slot = layout.dropSlotForY(layout.rowY(2) + 1.0f);
        check(slot == 1, "a drop on Snare's upper half goes before it (slot " + std::to_string(slot) + ")");
        check(layout.insertIndexForSlot(1, 6) == 2, "and that inserts before unit index 2 (Snare)");
        check(layout.insertIndexForSlot(6, 6) == 6, "the end inserts at the end");
    }

    std::cout << "[6] an open row pushes the rows under it down by its lanes\n";
    {
        const auto open = ArsenalRackLayout::compute(groups, {0, 0, 100.0f, 0, 0, 0}); // Snare (index 2) is open
        check(open.contentHeight == layout.contentHeight + 100.0f, "content grows by the lanes' height");
        check(open.rowY(5) == layout.rowY(5) + 100.0f, "a later row in the same group moves down");
        check(open.rowY(0) == layout.rowY(0), "a row above does not move");
    }

    std::cout << "[7] moving a unit lands where the drop says (no off-by-one)\n";
    {
        const std::vector<Audio::UnitID> order{10, 20, 30, 40};
        // Drop unit 10 "before unit 30" (slot index 2): after it leaves the front it must sit between 20 and 30, at index 1.
        check(ArsenalRackLayout::finalIndexAfterMove(order, 10, 2) == 1, "dragged down: one earlier than the slot index (it left its old place first)");
        check(ArsenalRackLayout::finalIndexAfterMove(order, 40, 1) == 1, "dragged up: the slot index as it is");
        check(ArsenalRackLayout::finalIndexAfterMove(order, 20, 4) == 3, "dragged to the very end: the last place");
        check(ArsenalRackLayout::finalIndexAfterMove(order, 30, 2) == 2 && ArsenalRackLayout::finalIndexAfterMove(order, 30, 3) == 2, "dropped on or just after itself: it stays");
    }

    std::cout << "[8] a wheel notch lands on a bar line, and reversing never skips one\n";
    {
        // A 4-bar loop in a 2.75-bar viewport: the clamp stops at 1.25 bars, not on a bar line.
        const float bar = 100.0f;
        const float maxScroll = 1.25f * bar;
        check(Audio::ArsenalGridScroll::nextBarOffset(0.0f, bar, maxScroll, +1) == bar, "forward from the start: the next bar");
        const float clampedToEnd = std::min(2.0f * bar, maxScroll); // bar 2 is past the end
        check(Audio::ArsenalGridScroll::nextBarOffset(bar, bar, maxScroll, +1) == clampedToEnd,
              "forward from bar 1: the clamp, since bar 2 is past the end");
        check(Audio::ArsenalGridScroll::nextBarOffset(maxScroll, bar, maxScroll, -1) == bar,
              "reverse from the clamped 1.25 bars: back to bar 1, not past it to 0");
        check(Audio::ArsenalGridScroll::nextBarOffset(bar, bar, maxScroll, -1) == 0.0f, "reverse from a bar line: the bar before");
        check(Audio::ArsenalGridScroll::nextBarOffset(1.5f * bar, bar, maxScroll, -1) == bar,
              "reverse from half a bar past a line: back to that line");
        check(Audio::ArsenalGridScroll::nextBarOffset(0.0f, bar, maxScroll, -1) == 0.0f, "reverse at the start: nowhere to go");
    }

    std::cout << "[9] equal wheel distance moves equal bars, however it is split\n";
    {
        const float barPx = 40.0f; // one wheel notch
        float combined = 0.0f;
        const int once = Audio::ArsenalGridScroll::wheelBars(combined, 40.0f, barPx);
        float split = 0.0f;
        const int firstHalf = Audio::ArsenalGridScroll::wheelBars(split, 20.0f, barPx);
        const int secondHalf = Audio::ArsenalGridScroll::wheelBars(split, 20.0f, barPx);
        check(once == 1 && firstHalf == 0 && secondHalf == 1,
              "40 px once and 20 + 20 px both move one bar");
        check(std::abs(combined - split) < 1e-6f, "and leave the same remainder behind");
        float leftover = 0.0f;
        check(Audio::ArsenalGridScroll::wheelBars(leftover, 25.0f, barPx) == 0, "under a bar: nothing moves");
        check(Audio::ArsenalGridScroll::wheelBars(leftover, 25.0f, barPx) == 1, "the remainder completes the bar");
        check(std::abs(leftover - 10.0f) < 1e-6f, "past the bar stays in the accumulator");
        float flick = 0.0f;
        check(Audio::ArsenalGridScroll::wheelBars(flick, 120.0f, barPx) == 3, "120 px in one event is three bars");
        check(std::abs(flick) < 1e-6f, "a whole-bar flick leaves nothing over");
        float back = 0.0f;
        check(Audio::ArsenalGridScroll::wheelBars(back, -40.0f, barPx) == -1, "reversing moves back a bar");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
