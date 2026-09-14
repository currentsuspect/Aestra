// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-C14 step 2 (FD-23): the layout half of the resolution contract.
//
// The load-bearing check is the second one. AestraContent::onResize today writes
// every clamped panel rect back into stored state, so shrinking the window shrinks
// the saved rect for good. The contract replaces that with resolve-only layout
// passes over an untouched preference. That test runs both pipelines side by side
// over the same region churn and asserts the contract's comes back exactly while
// the write-back one does not — so it cannot pass just because nothing changed.
//
// Deliberately links nothing: NUIAnchoredPlacement.h is header-only.

#include "../../AestraUI/Layout/NUIAnchoredPlacement.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

using namespace AestraUI::Layout;
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

constexpr float kEdgeEps = 1e-3f;

bool insideRegion(const NUIWindowRect& r, const NUIWindowRect& region) {
    return r.x >= region.x - kEdgeEps && r.y >= region.y - kEdgeEps && r.right() <= region.right() + kEdgeEps &&
           r.bottom() <= region.bottom() + kEdgeEps;
}

// A compile-time proof, not a runtime one: if anyone gives the resolver a mutable
// reference or an out-parameter, this stops compiling.
using ResolverSignature = NUIPlacementResult (*)(const NUIAnchoredRect&, NUIPlacementMode, const NUIWindowRect&,
                                                 const NUISizeLimits&) noexcept;
static_assert(std::is_same_v<decltype(&resolveAnchoredPlacement), ResolverSignature>,
              "resolveAnchoredPlacement must take the preference by const reference and return by value");

void testResolverCannotRewriteThePreference() {
    check(true, "resolver signature takes the preference by const& with no out-params (static_assert)");
}

void testPreferenceSurvivesRegionChurn() {
    const NUIAnchoredRect preference{0.25, 0.75, 640.0, 360.0};
    const NUISizeLimits limits{100.0, 100.0};
    const NUIWindowRect big(0.0f, 40.0f, 1600.0f, 900.0f);

    const NUIPlacementResult first = resolveAnchoredPlacement(preference, NUIPlacementMode::Anchored, big, limits);

    // Today's defect, replayed: every layout pass writes its result back.
    NUIAnchoredRect writtenBack = preference;
    for (int i = 0; i < 50; ++i) {
        const NUIWindowRect region(static_cast<float>((i * 13) % 200), 40.0f, static_cast<float>(320 + (i * 37) % 600),
                                   static_cast<float>(200 + (i * 53) % 400));
        // The contract's pass: resolve only.
        (void)resolveAnchoredPlacement(preference, NUIPlacementMode::Anchored, region, limits);
        const NUIPlacementResult shown = resolveAnchoredPlacement(writtenBack, NUIPlacementMode::Anchored, region, limits);
        writtenBack = captureAnchoredPlacement(shown.resolved, region, writtenBack);
    }

    const NUIPlacementResult after = resolveAnchoredPlacement(preference, NUIPlacementMode::Anchored, big, limits);
    check(sameRect(after.resolved, first.resolved),
          "resolve-only passes: 50 smaller/shifted regions, then the original region -> the original rect exactly");

    const NUIPlacementResult defective = resolveAnchoredPlacement(writtenBack, NUIPlacementMode::Anchored, big, limits);
    check(!sameRect(defective.resolved, first.resolved),
          "negative control: writing each resolved rect back through capture drifts (the onResize defect), "
          "so the check above is not vacuous");
}

void testRoundTripIsBitExact() {
    const std::vector<NUIWindowRect> regions = {
        NUIWindowRect(0.0f, 40.0f, 1600.0f, 900.0f),
        NUIWindowRect(123.5f, 40.25f, 1333.333f, 777.777f),
        NUIWindowRect(251.0f, 36.0f, 100.0f / 3.0f * 30.0f, 612.4f),
    };
    int mismatches = 0;
    int flagged = 0;
    int cases = 0;
    for (const NUIWindowRect& region : regions) {
        for (float fw : {1.0f / 3.0f, 0.5f, 0.123456f, 1.0f}) {
            for (float fx : {0.0f, 0.333333f, 0.5f, 0.9f, 1.0f}) {
                const float w = region.width * fw;
                const float h = region.height * (1.0f - fw * 0.5f);
                const float x = region.x + (region.width - w) * fx;
                const float y = region.y + (region.height - h) * (1.0f - fx);
                const NUIWindowRect gesture(x, y, w, h);
                if (!insideRegion(gesture, region)) {
                    continue; // The identity is promised only for rects that fit.
                }
                ++cases;
                const NUIAnchoredRect captured = captureAnchoredPlacement(gesture, region, NUIAnchoredRect{});
                const NUIPlacementResult back =
                    resolveAnchoredPlacement(captured, NUIPlacementMode::Anchored, region, NUISizeLimits{});
                if (!sameRect(back.resolved, gesture)) {
                    ++mismatches;
                    std::cout << "  mismatch: gesture(" << x << "," << y << "," << w << "," << h << ") -> ("
                              << back.resolved.x << "," << back.resolved.y << "," << back.resolved.width << ","
                              << back.resolved.height << ")\n";
                }
                if (back.trace.adjustments != Adj::None) {
                    ++flagged;
                }
            }
        }
    }
    check(cases > 30, "round-trip sweep covers more than 30 fitting rects (" + std::to_string(cases) + ")");
    check(mismatches == 0, "resolve(capture(r, R), R) == r bit-exact for every fitting rect, incl. 33.333... values");
    check(flagged == 0, "a fitting rect resolves with no adjustments recorded");
}

void testEverySizeChangeIsExplained() {
    const std::vector<NUIWindowRect> regions = {NUIWindowRect(0.0f, 0.0f, 800.0f, 600.0f),
                                                NUIWindowRect(50.0f, 30.0f, 90.0f, 70.0f),
                                                NUIWindowRect(-20.0f, 10.0f, 2400.0f, 120.0f)};
    const std::vector<NUIAnchoredRect> preferences = {{0.0, 0.0, 300.0, 200.0},  {1.0, 1.0, 1000.0, 900.0},
                                                      {0.5, 0.5, 20.0, 20.0},    {0.2, 0.8, 90.0, 70.0},
                                                      {0.7, 0.1, 5000.0, 50.0}};
    const std::vector<NUISizeLimits> limitSets = {{0.0, 0.0}, {100.0, 100.0}, {1000.0, 50.0}};
    constexpr double kSizeEps = 1e-3;
    int unexplained = 0;
    int outside = 0;
    int noneButChanged = 0;
    for (const auto& region : regions) {
        for (const auto& preference : preferences) {
            for (const auto& limits : limitSets) {
                const NUIPlacementResult r =
                    resolveAnchoredPlacement(preference, NUIPlacementMode::Anchored, region, limits);
                const bool sizeChanged = std::fabs(r.trace.dw()) > kSizeEps || std::fabs(r.trace.dh()) > kSizeEps;
                const std::uint32_t sizeReasons = Adj::GrownToMin | Adj::ShrunkToRegion | Adj::FilledRegion;
                if (sizeChanged && (r.trace.adjustments & sizeReasons) == 0) {
                    ++unexplained;
                }
                if (r.trace.adjustments == Adj::None && sizeChanged) {
                    ++noneButChanged;
                }
                if (!insideRegion(r.resolved, region)) {
                    ++outside;
                }
            }
        }
    }
    check(unexplained == 0, "every size difference between requested and resolved carries a reason flag");
    check(noneButChanged == 0, "no adjustments recorded implies the requested size was given exactly");
    check(outside == 0, "every resolved rect lies inside its region, including when even the minimum does not fit");
}

void testDockingSurvivesRegionChange() {
    const NUIAnchoredRect rightDocked{1.0, 0.0, 300.0, 200.0};
    const NUIAnchoredRect centred{0.5, 0.5, 300.0, 200.0};
    const NUIWindowRect wide(0.0f, 40.0f, 1200.0f, 800.0f);
    const NUIWindowRect narrowShifted(250.0f, 40.0f, 700.0f, 600.0f);

    for (const auto& region : {wide, narrowShifted}) {
        const NUIPlacementResult r = resolveAnchoredPlacement(rightDocked, NUIPlacementMode::Anchored, region, {});
        check(r.resolved.right() == region.right(), "anchor 1.0 stays flush with the region's right edge");
        check(r.resolved.y == region.y, "anchor 0.0 stays flush with the region's top edge");

        const NUIPlacementResult c = resolveAnchoredPlacement(centred, NUIPlacementMode::Anchored, region, {});
        const float regionCentreX = region.x + region.width * 0.5f;
        const float rectCentreX = c.resolved.x + c.resolved.width * 0.5f;
        check(std::fabs(regionCentreX - rectCentreX) <= kEdgeEps, "anchor 0.5 stays centred in the region");
    }
}

void testOrderAndDeterminism() {
    const NUIWindowRect small(10.0f, 20.0f, 80.0f, 60.0f);
    const NUISizeLimits limits{200.0, 150.0};
    const NUIAnchoredRect preference{0.5, 0.5, 400.0, 300.0};

    const NUIPlacementResult r = resolveAnchoredPlacement(preference, NUIPlacementMode::Anchored, small, limits);
    check(r.resolved.width == small.width && r.resolved.height == small.height,
          "minimum larger than the region: the region's size wins, never the minimum");
    check(r.resolved.x == small.x && r.resolved.y == small.y, "and it sits at the region origin, not off-screen");
    check((r.trace.adjustments & Adj::MinExceedsRegion) != 0, "the unmet minimum is reported, not hidden");

    const NUIPlacementResult again = resolveAnchoredPlacement(preference, NUIPlacementMode::Anchored, small, limits);
    check(sameRect(r.resolved, again.resolved) && r.trace.adjustments == again.trace.adjustments,
          "the same inputs give the same rect and the same reasons");
}

void testDegenerateRegionsAreNotApplicable() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<NUIWindowRect> bad = {NUIWindowRect(0.0f, 0.0f, 0.0f, 0.0f),
                                            NUIWindowRect(0.0f, 0.0f, -10.0f, 100.0f),
                                            NUIWindowRect(static_cast<float>(nan), 0.0f, 100.0f, 100.0f),
                                            NUIWindowRect(0.0f, 0.0f, inf, 100.0f)};
    const NUIAnchoredRect prior{0.3, 0.6, 250.0, 180.0};
    int applicable = 0;
    int unflagged = 0;
    int changed = 0;
    for (const auto& region : bad) {
        const NUIPlacementResult r = resolveAnchoredPlacement(prior, NUIPlacementMode::Anchored, region, {});
        if (r.applicable) {
            ++applicable;
        }
        if ((r.trace.adjustments & Adj::DegenerateRegion) == 0) {
            ++unflagged;
        }
        const NUIAnchoredRect captured =
            captureAnchoredPlacement(NUIWindowRect(10.0f, 10.0f, 100.0f, 100.0f), region, prior);
        if (captured.anchorX != prior.anchorX || captured.anchorY != prior.anchorY ||
            captured.width != prior.width || captured.height != prior.height) {
            ++changed;
        }
    }
    check(applicable == 0, "zero-size, negative, NaN and infinite regions are never applicable");
    check(unflagged == 0, "and each is reported as DegenerateRegion");
    check(changed == 0, "capturing against an unusable region returns the prior preference unchanged");
}

void testRestoredRectUnderPointer() {
    const NUIWindowRect maximized(200.0f, 40.0f, 1400.0f, 900.0f);
    const NUIWindowPoint pointer(200.0f + 1400.0f * 0.25f, 52.0f);

    // Independent reference: a quarter of the way along the maximized title bar
    // must stay a quarter of the way along the restored one, 12px below its top.
    const float expectedX = pointer.x - 0.25f * 500.0f;
    const float expectedY = 40.0f;

    const NUIWindowRect restored = restoredRectUnderPointer(maximized, 500.0, 300.0, pointer, maximized);
    check(restored.x == expectedX && restored.y == expectedY,
          "the pointer keeps its proportional position along the title bar and its offset from the top");
    check(restored.width == 500.0f && restored.height == 300.0f, "the restored rect keeps the saved size");

    // Grabbed at the far right of a region narrower than the saved width.
    const NUIWindowRect narrow(200.0f, 40.0f, 400.0f, 900.0f);
    const NUIWindowPoint farRight(1590.0f, 50.0f);
    const NUIWindowRect edge = restoredRectUnderPointer(maximized, 500.0, 300.0, farRight, narrow);
    check(edge.width == 500.0f, "a saved width wider than the region is kept, not rewritten by the drag");
    check(edge.x == narrow.x, "its position is fitted so it starts inside the region");

    const NUIAnchoredRect captured = captureAnchoredPlacement(edge, narrow, NUIAnchoredRect{0.5, 0.5, 500.0, 300.0});
    const NUIPlacementResult shown = resolveAnchoredPlacement(captured, NUIPlacementMode::Anchored, narrow, {});
    check(insideRegion(shown.resolved, narrow), "and resolving the captured drag displays it fully inside the region");
}

} // namespace

int main() {
    testResolverCannotRewriteThePreference();
    testPreferenceSurvivesRegionChurn();
    testRoundTripIsBitExact();
    testEverySizeChangeIsExplained();
    testDockingSurvivesRegionChange();
    testOrderAndDeterminism();
    testDegenerateRegionsAreNotApplicable();
    testRestoredRectUnderPointer();

    if (failures != 0) {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All NUIAnchoredPlacement checks passed\n";
    return 0;
}
