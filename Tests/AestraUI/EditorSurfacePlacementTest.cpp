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
#include <limits>
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

    {
        // The layer covers the window (y 0) and the app title bar owns the top 32 px: a 738 px panel opened
        // centred in the whole layer lands at y 15 and its close button sits under the title bar.
        const auto region = editorPlacementRegion(0.0f, 1366.0f, 768.0f, 32.0f);
        expect(region.y == 32.0f && region.height == 736.0f, "the editor region starts below the app title bar");
        const auto placed = resolveAnchoredPlacement(AestraUI::Layout::NUIAnchoredRect{0.5, 0.5, 866.0, 738.0},
                                                     NUIPlacementMode::Anchored, region, NUISizeLimits{0.0, 0.0});
        expect(placed.applicable && placed.resolved.y >= 32.0f, "an editor never opens over the title bar strip");
        // A layer that itself starts below the strip is untouched; one starting above it is trimmed to it.
        const auto low = editorPlacementRegion(40.0f, 800.0f, 600.0f, 32.0f);
        expect(low.y == 0.0f && low.height == 600.0f, "a layer already below the strip is not trimmed");
    }

    {
        // The founder's trace: window 1366x768, region y 32 h 736. Dexed's view is 866x674 under a 64 px two-row header;
        // resizable adds the 12 px grip band. Negative control: the plain resolver (hold = false) shrinks the panel.
        const auto region = editorPlacementRegion(0.0f, 1366.0f, 768.0f, 32.0f);
        struct Row { const char* name; double h; bool hold; double wantH; double wantY; };
        const Row rows[] = {
            {"fixed 738 fits", 738.0, true, 738.0, 32.0},
            {"resizable 750 not shrunk", 750.0, true, 750.0, 32.0},
            {"smaller view is centred", 400.0, true, 400.0, 32.0 + 0.5 * (736.0 - 400.0)},
            {"much taller than region", 1200.0, true, 1200.0, 32.0},
        };
        for (const auto& r : rows) {
            const auto placed = resolveEditorPlacement(NUIAnchoredRect{0.5, 0.5, 866.0, r.h}, region, r.hold);
            expect(placed.applicable && near(placed.resolved.height, r.wantH) && near(placed.resolved.y, r.wantY),
                   std::string("hold-size placement: ") + r.name);
            expect(placed.resolved.width == 866.0f && placed.resolved.x >= 0.0f, "hold-size keeps width and x in the layer");
        }
        // Without hold the panel is shrunk to the region (the defect): a held size must differ from it.
        const auto shrunk = resolveEditorPlacement(NUIAnchoredRect{0.5, 0.5, 866.0, 750.0}, region, false);
        expect(near(shrunk.resolved.height, 736.0), "control: the plain resolver shrinks a 750 px panel to 736");
        // A view wider than the region keeps its width and starts at the region's left edge.
        const auto wide = resolveEditorPlacement(NUIAnchoredRect{0.5, 0.5, 1500.0, 300.0}, region, true);
        expect(near(wide.resolved.width, 1500.0) && near(wide.resolved.x, 0.0), "a wider-than-region panel starts at x 0");
    }

    {
        // Outside-click decision: panel bounds 473.5,32 866x652 (the trace) while the plugin view reaches y 770.
        const float bx = 473.5f, by = 32.0f, bw = 866.0f, bh = 652.0f;
        const float vx = 473.5f, vy = 96.0f, vw = 866.0f, vh = 674.0f;
        expect(!pressInsidePanel(725.0f, 706.0f, bx, by, bw, bh, false, 0, 0, 0, 0),
               "control: bounds alone call a press on the plugin's piano keys outside");
        expect(pressInsidePanel(725.0f, 706.0f, bx, by, bw, bh, true, vx, vy, vw, vh),
               "a press on the embedded view is inside the panel");
        expect(pressInsidePanel(500.0f, 40.0f, bx, by, bw, bh, true, vx, vy, vw, vh), "a press on the header is inside");
        expect(!pressInsidePanel(100.0f, 400.0f, bx, by, bw, bh, true, vx, vy, vw, vh), "a press left of the panel is outside");
        expect(!pressInsidePanel(725.0f, 771.0f, bx, by, bw, bh, true, vx, vy, vw, vh), "a press below the view is outside");
        expect(!pressInsidePanel(1339.5f, 400.0f, bx, by, bw, bh, true, vx, vy, vw, vh), "the right edge is exclusive");
    }

    expect(editorSurfaceKey("3.1.com.Aestrastudios.eq") == "editor.3.1.com.Aestrastudios.eq",
           "surface key lives under the editor namespace");

    {
        const auto live = editorIntrinsicBounds(800.0f, 600.0f, 400, 400);
        expect(live.width == 800.0f && live.height == 600.0f, "live editor bounds win over the reported size");
        const auto reported = editorIntrinsicBounds(0.0f, 0.0f, 500, 300);
        expect(reported.width == 500.0f && reported.height == 300.0f, "reported editor size is the fallback");
        const auto fallback = editorIntrinsicBounds(0.0f, -10.0f, 0, 0);
        expect(fallback.width == 400.0f && fallback.height == 400.0f, "empty sizes fall back to 400");
        const auto infinite =
            editorIntrinsicBounds(std::numeric_limits<float>::infinity(), 600.0f, 500, 300);
        expect(infinite.width == 500.0f && infinite.height == 600.0f,
               "non-finite live dimensions fall back instead of poisoning placement");
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
        expect(near(backA.x, gestureLocal.x) && near(backA.y, gestureLocal.y),
               "the round trip preserves the original popup-local position");
    }

    if (g_failures == 0) {
        std::cout << "All EditorSurfacePlacement tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " EditorSurfacePlacement test(s) failed.\n";
    return 1;
}
