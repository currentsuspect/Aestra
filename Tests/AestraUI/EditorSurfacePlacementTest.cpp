// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-C14 step 5b (FD-23): plugin editors store their anchor only — size is the
// plugin's intrinsic size, never a user preference. These tests pin the pure
// assembly in AestraUI/Widgets/EditorSurfacePlacement.h: intrinsic-size
// selection, stored-anchor-or-centred-default preference assembly (a stale
// stored size must never win), the resolve round-trip that puts a reopened
// editor back where the user left it, and that the localToWindow conversion
// step 5b performs is placement-preserving.

#include "../../AestraUI/Widgets/EditorSurfacePlacement.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace AestraUI;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        ++g_failures;
    }
}

bool near(double a, double b, double eps = 0.01) {
    return std::abs(a - b) <= eps;
}

} // namespace

int main() {
    using namespace AestraUI::Layout;
    using namespace AestraUI::EditorPlacement;

    expect(editorSurfaceKey("3.1.com.Aestrastudios.eq") == "editor.3.1.com.Aestrastudios.eq",
           "surface key lives under the editor namespace");

    {
        const auto live = editorIntrinsicBounds(800.0f, 600.0f, 400, 400);
        expect(live.width == 800.0f && live.height == 600.0f, "live editor bounds win over the reported size");
        const auto reported = editorIntrinsicBounds(0.0f, 0.0f, 500, 300);
        expect(reported.width == 500.0f && reported.height == 300.0f, "reported editor size is the fallback");
        const auto fallback = editorIntrinsicBounds(0.0f, -10.0f, 0, 0);
        expect(fallback.width == 400.0f && fallback.height == 400.0f, "empty sizes fall back to 400");
    }

    {
        const auto fresh = editorOpenPreference(std::nullopt, 640.0f, 480.0f);
        expect(fresh.anchorX == 0.5 && fresh.anchorY == 0.5, "a never-placed editor opens centred");
        expect(fresh.width == 640.0 && fresh.height == 480.0, "a fresh preference carries the intrinsic size");
    }

    {
        const NUIAnchoredRect stored{0.25, 0.75, 100.0, 100.0};
        const auto reopened = editorOpenPreference(stored, 640.0f, 480.0f);
        expect(reopened.anchorX == 0.25 && reopened.anchorY == 0.75, "a reopened editor keeps its stored anchor");
        expect(reopened.width == 640.0 && reopened.height == 480.0,
               "a stale stored size never wins: the record is anchor-only in practice");
    }

    {
        // The reopened editor resolves back to where the user left it.
        const NUIAnchoredRect stored{0.25, 0.75, 640.0, 400.0};
        const NUIWindowRect region(0.0f, 0.0f, 1600.0f, 900.0f);
        const NUISizeLimits limits{0.0, 0.0};
        const auto placed = resolveAnchoredPlacement(editorOpenPreference(stored, 640.0f, 400.0f),
                                                     NUIPlacementMode::Anchored, region, limits);
        expect(placed.applicable, "a sane region resolves");
        expect(near(placed.resolved.x, 0.25 * (1600.0 - 640.0)) && near(placed.resolved.y, 0.75 * (900.0 - 400.0)),
               "resolve puts the editor at its stored anchor");
        expect(near(placed.resolved.width, 640.0) && near(placed.resolved.height, 400.0),
               "resolve keeps the intrinsic size");
    }

    {
        // The localToWindow conversion is placement-preserving: the same
        // popup-local gesture captured under two different measured popup
        // origins yields the same anchor, and resolving in either window
        // frame converts back to the same popup-local placement. Shifting
        // frames must never shift where the editor lands.
        const NUIWindowPoint originA(100.0f, 50.0f);
        const NUIWindowPoint originB(0.0f, 200.0f);
        const NUILocalRect gestureLocal(300.0f, 200.0f, 640.0f, 400.0f);
        const NUILocalRect regionLocal(0.0f, 0.0f, 1600.0f, 900.0f);
        const NUISizeLimits limits{0.0, 0.0};
        const NUIAnchoredRect prior{0.5, 0.5, 640.0, 400.0};
        const auto capturedA =
            captureAnchoredPlacement(localToWindow(gestureLocal, originA), localToWindow(regionLocal, originA), prior);
        const auto capturedB =
            captureAnchoredPlacement(localToWindow(gestureLocal, originB), localToWindow(regionLocal, originB), prior);
        expect(near(capturedA.anchorX, capturedB.anchorX) && near(capturedA.anchorY, capturedB.anchorY),
               "the captured anchor does not depend on the measured popup origin");
        const auto placedA = resolveAnchoredPlacement(capturedA, NUIPlacementMode::Anchored,
                                                      localToWindow(regionLocal, originA), limits);
        const auto placedB = resolveAnchoredPlacement(capturedB, NUIPlacementMode::Anchored,
                                                      localToWindow(regionLocal, originB), limits);
        const NUILocalRect backA = windowToLocal(placedA.resolved, originA);
        const NUILocalRect backB = windowToLocal(placedB.resolved, originB);
        expect(near(backA.x, backB.x) && near(backA.y, backB.y),
               "both window frames convert back to the same popup-local placement");
    }

    if (g_failures == 0) {
        std::cout << "All EditorSurfacePlacement tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " EditorSurfacePlacement test(s) failed.\n";
    return 1;
}
