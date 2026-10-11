// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-C14 step 2 (FD-23): the store half of the resolution contract, driven through
// UISurfaceGeometry exactly as a panel will be once step 3 wires it.
//
// The first check is FD-23's own proving case, maximize: the store records
// maximized = true, layout fills whatever region exists, and restoring returns the
// pre-maximize rect — with the saved anchor and size byte-equal the whole time.
//
// Links nothing: every header involved is header-only.

#include "../../Source/Core/UISurfaceResolution.h"

#include <iostream>
#include <string>

using namespace Aestra;
using AestraUI::Layout::NUIWindowPoint;
using AestraUI::Layout::NUIWindowRect;
using AestraUI::Layout::NUISizeLimits;
namespace Adj = AestraUI::Layout::NUIPlacementAdjust;

namespace {

int failures = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "PASS: " << what << "\n";
    } else {
        std::cout << "FAIL: " << what << "\n";
        ++failures;
    }
}

bool sameRect(const NUIWindowRect& a, const NUIWindowRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool sameGeometry(const UISurfaceGeometry& a, const UISurfaceGeometry& b) {
    return a.anchorX == b.anchorX && a.anchorY == b.anchorY && a.width == b.width && a.height == b.height;
}

void testMaximizeRestoresThePreMaximizeRect() {
    const NUIWindowRect region(0.0f, 40.0f, 1600.0f, 900.0f);
    const NUISizeLimits limits{100.0, 100.0};

    UISurfaceGeometry preference = defaultSurfacePreference(480.0, 320.0, 0.5, 0.5);
    preference = captureSurfaceGesture(preference, NUIWindowRect(300.0f, 200.0f, 640.0f, 400.0f), region, 1000);
    const NUIWindowRect beforeMaximize = resolveSurfacePlacement(preference, region, limits).resolved;
    const UISurfaceGeometry saved = preference;

    preference = captureMaximizeToggle(preference, true, 1001);
    check(preference.maximized, "maximize is recorded in the preference");
    check(sameGeometry(preference, saved), "maximizing leaves the saved anchor and size byte-equal");

    int notFilled = 0;
    for (int i = 0; i < 20; ++i) {
        const NUIWindowRect changed(static_cast<float>(i * 17), 40.0f, static_cast<float>(700 + i * 41),
                                    static_cast<float>(400 + i * 23));
        const auto shown = resolveSurfacePlacement(preference, changed, limits);
        if (!sameRect(shown.resolved, changed) || (shown.trace.adjustments & Adj::FilledRegion) == 0) {
            ++notFilled;
        }
    }
    check(notFilled == 0, "while maximized, every one of 20 different regions is filled exactly");
    check(sameGeometry(preference, saved), "20 layout passes while maximized never touched the saved geometry");

    preference = captureMaximizeToggle(preference, false, 1002);
    check(!preference.maximized, "restore is recorded");
    check(sameRect(resolveSurfacePlacement(preference, region, limits).resolved, beforeMaximize),
          "restoring returns exactly the pre-maximize rect");
}

void testMaximizedWithUnmetMinimum() {
    UISurfaceGeometry preference = defaultSurfacePreference(640.0, 400.0, 0.5, 0.5);
    preference = captureMaximizeToggle(preference, true, 10);
    const NUIWindowRect tiny(0.0f, 0.0f, 50.0f, 50.0f);

    const auto shown = resolveSurfacePlacement(preference, tiny, NUISizeLimits{100.0, 100.0});
    check((shown.trace.adjustments & Adj::FilledRegion) != 0 && (shown.trace.adjustments & Adj::MinExceedsRegion) != 0,
          "maximized into a region below the minimum reports FilledRegion and MinExceedsRegion");
    check(sameRect(shown.resolved, tiny), "and fills the region rather than overflowing it");
    check(preference.maximized, "the maximize preference is unaffected by a region it cannot fit");
}

void testDraggingAMaximizedSurfaceRestoresIt() {
    const NUIWindowRect region(0.0f, 40.0f, 1600.0f, 900.0f);
    const NUISizeLimits limits{100.0, 100.0};

    UISurfaceGeometry preference = defaultSurfacePreference(640.0, 400.0, 0.2, 0.3);
    preference = captureMaximizeToggle(preference, true, 50);
    const UISurfaceGeometry beforeDrag = preference;

    const NUIWindowRect maximizedRect = resolveSurfacePlacement(preference, region, limits).resolved;
    const NUIWindowPoint grab(maximizedRect.x + maximizedRect.width * 0.6f, maximizedRect.y + 10.0f);
    const NUIWindowRect restored =
        AestraUI::Layout::restoredRectUnderPointer(maximizedRect, preference.width, preference.height, grab, region);

    const UISurfaceGeometry next = captureSurfaceGesture(preference, restored, region, 60);
    const double expectedAnchorX = (static_cast<double>(restored.x) - region.x) / (region.width - 640.0);

    check(!next.maximized, "dragging a maximized surface's title bar restores it");
    check(next.width == 640.0 && next.height == 400.0, "the restored surface keeps its saved size");
    check(next.anchorX == expectedAnchorX, "and its new anchor is where it was dropped");
    check(beforeDrag.maximized && sameGeometry(beforeDrag, preference),
          "the preference passed in is untouched; capture returned a new value");
}

void testAbsentEntryResolvesFromDefaultWithoutWriting() {
    UISurfaceStore store;
    const NUIWindowRect region(0.0f, 40.0f, 1600.0f, 900.0f);

    const auto found = store.surfaces.find("panel.mixer");
    const UISurfaceGeometry preference =
        found != store.surfaces.end() ? found->second : defaultSurfacePreference(800.0, 400.0, 0.5, 1.0);
    const auto shown = resolveSurfacePlacement(preference, region, NUISizeLimits{100.0, 100.0});

    check(shown.applicable && shown.resolved.width == 800.0f && shown.resolved.bottom() == region.bottom(),
          "an absent entry resolves from its default (bottom-docked here)");
    check(store.surfaces.empty(), "resolving a default never writes an entry into the store");
}

} // namespace

int main() {
    testMaximizeRestoresThePreMaximizeRect();
    testMaximizedWithUnmetMinimum();
    testDraggingAMaximizedSurfaceRestoresIt();
    testAbsentEntryResolvesFromDefaultWithoutWriting();

    if (failures != 0) {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All UISurfaceResolution checks passed\n";
    return 0;
}
