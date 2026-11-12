// © 2026 Aestra Studios — All Rights Reserved.
//
// MarkerModelTest — markers (song sections and cues): the ordered model behind the ruler.
//
// What must hold: markers stay sorted by beat through add and move, identity is stable (undo can restore the same id),
// navigation never returns the marker it started from, and a hostile file cannot grow the list without bound.

#include "Models/MarkerModel.h"

#include <cmath>
#include <cstdio>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++g_failures;
}

bool closeTo(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

void testAddKeepsBeatOrder() {
    MarkerModel model;
    model.add(32.0, "Hook");
    model.add(0.0, "Intro");
    model.add(16.0, "Verse");
    const auto& m = model.markers();
    check(m.size() == 3 && m[0].name == "Intro" && m[1].name == "Verse" && m[2].name == "Hook", "AddKeepsBeatOrder");
}

void testIdsAreStableAndUnique() {
    MarkerModel model;
    const uint64_t a = model.add(4.0, "A");
    const uint64_t b = model.add(8.0, "B");
    check(a != 0 && b != 0 && a != b, "IdsAreUnique");
    check(model.remove(a) && model.find(a) == nullptr, "RemoveClearsTheId");
    const uint64_t restored = model.add(4.0, "A", a);
    check(restored == a && model.find(a) != nullptr && model.find(a)->name == "A", "RestoreKeepsTheSameId");
    check(model.add(9.0, "Dup", a) == 0, "RestoringALiveIdIsRefused");
}

void testNextAndPreviousSkipTheCurrentMarker() {
    MarkerModel model;
    model.add(0.0, "Intro");
    model.add(16.0, "Verse");
    model.add(32.0, "Hook");
    const Marker* next = model.nextAfter(16.0, false);
    check(next != nullptr && next->name == "Hook", "NextAfterSkipsTheMarkerYouAreOn");
    const Marker* prev = model.previousBefore(16.0, false);
    check(prev != nullptr && prev->name == "Intro", "PreviousBeforeSkipsTheMarkerYouAreOn");
    check(model.nextAfter(32.0, false) == nullptr, "NextPastTheLastIsNoneWithoutWrap");
    check(model.nextAfter(32.0, true) != nullptr && model.nextAfter(32.0, true)->name == "Intro", "NextWrapsToTheFirst");
    check(model.previousBefore(0.0, true) != nullptr && model.previousBefore(0.0, true)->name == "Hook",
          "PreviousWrapsToTheLast");
}

void testNearestUsesTolerance() {
    MarkerModel model;
    model.add(10.0, "Here");
    check(model.nearest(10.4, 0.5) != nullptr, "NearestInsideTolerance");
    check(model.nearest(10.6, 0.5) == nullptr, "NearestOutsideToleranceIsNone");
}

void testMoveReSorts() {
    MarkerModel model;
    const uint64_t a = model.add(0.0, "A");
    const uint64_t b = model.add(8.0, "B");
    check(model.moveTo(a, 20.0), "MoveSucceeds");
    const auto& m = model.markers();
    check(m.size() == 2 && m[0].id == b && m[1].id == a, "MoveReSortsTheList");
    check(!model.moveTo(999, 4.0), "MoveUnknownMarkerIsRefused");
}

void testRenameIsBoundedAndNonEmpty() {
    MarkerModel model;
    const uint64_t id = model.add(0.0, "Intro");
    check(!model.rename(id, ""), "EmptyNameIsRefused");
    const std::string longName(MarkerModel::kMaxNameBytes + 40, 'x');
    check(model.rename(id, longName), "LongNameAccepted");
    check(model.find(id)->name.size() == MarkerModel::kMaxNameBytes, "LongNameIsTruncated");
}

void testBadPositionsAreRefused() {
    MarkerModel model;
    check(model.add(-1.0, "Negative") == 0, "NegativeBeatRefused");
    check(model.add(std::nan(""), "NaN") == 0, "NaNsRefused");
}

void testCapacityIsBounded() {
    MarkerModel model;
    for (size_t i = 0; i < MarkerModel::kMaxMarkers; ++i) {
        model.add(static_cast<double>(i), "M");
    }
    check(model.count() == MarkerModel::kMaxMarkers, "FillsToTheCap");
    check(model.add(1.0e6, "Over") == 0, "BeyondTheCapIsRefused");
}

void testDefaultNamesAreUsable() {
    MarkerModel model;
    model.add(4.0);
    model.add(8.0);
    check(!model.markers()[0].name.empty() && model.markers()[0].name != model.markers()[1].name,
          "DefaultNamesAreUnique");
}

void testClearResetsIdentity() {
    MarkerModel model;
    model.add(4.0, "A");
    model.clear();
    check(model.empty() && model.count() == 0, "ClearEmptiesTheList");
    check(model.add(4.0, "A") == 1, "ClearRestartsIdMinting");
}

} // namespace

int main() {
    testAddKeepsBeatOrder();
    testIdsAreStableAndUnique();
    testNextAndPreviousSkipTheCurrentMarker();
    testNearestUsesTolerance();
    testMoveReSorts();
    testRenameIsBoundedAndNonEmpty();
    testBadPositionsAreRefused();
    testCapacityIsBounded();
    testDefaultNamesAreUsable();
    testClearResetsIdentity();

    if (g_failures) {
        std::printf("%d MarkerModel test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All MarkerModel tests passed\n");
    return 0;
}
