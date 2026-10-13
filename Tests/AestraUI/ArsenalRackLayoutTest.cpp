// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-27 point 2, "a grouped rack": units sit under Drums, Synth and Audio headers with their counts, in unit
// order inside a group, and the positions drive layout, scrolling and drag-and-drop from one answer.

#include "ArsenalRackLayout.h"

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

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
