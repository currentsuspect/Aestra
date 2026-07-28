// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Sliding-indicator first-sync semantics (segmented control, #653 follow-up).
//
// The reported symptom: on launch the view toggle's indicator "comes from the
// far left and bounces in". Aestra's toggle defaults to Timeline (index 1),
// while the indicator position was default-constructed to 0 — so the first
// frames animated it from segment 0 to segment 1. It was animating from a
// position it had never been in.
//
// Four other segmented controls in the tree avoided this only because they
// select index 0, which is where the default position already sat. That is
// luck, not correctness, which is why the fix is caller-proof rather than an
// `animate=false` argument every call site must remember.
//
// The invariant under test:
//
//   The first synchronisation snaps the indicator directly to the authoritative
//   selected segment. Only subsequent genuine selection transitions animate.

#include "NUISlidingIndicator.h"

#include <cmath>
#include <iostream>
#include <string>

using AestraUI::NUISlidingIndicator;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}

void nearly(float got, float want, const std::string& what) {
    if (std::abs(got - want) > 1e-4f) {
        std::cerr << "[FAIL] " << what << ": got " << got << ", wanted " << want << '\n';
        ++g_failures;
    }
}

constexpr double kFrame = 1.0 / 60.0;
constexpr float kSpeed = 12.0f;

// Run frames until settled or the budget runs out; returns frames consumed.
int settle(NUISlidingIndicator& ind, float target, int maxFrames = 600) {
    int frames = 0;
    while (frames < maxFrames && ind.sync(target, kFrame, kSpeed)) {
        ++frames;
    }
    return frames;
}

} // namespace

int main() {
    // --- 1. constructed with a NON-first segment selected -> appears there at once
    // This is the reported bug: Timeline is index 1.
    {
        NUISlidingIndicator ind;
        check(!ind.isInitialized(), "starts uninitialised");

        const bool changed = ind.sync(1.0f, kFrame, kSpeed);
        check(changed, "first sync reports a change");
        nearly(ind.position(), 1.0f, "first sync SNAPS to segment 1, no slide from 0");
        check(ind.isInitialized(), "first sync marks initialised");

        // And it must be settled — not mid-animation.
        check(!ind.sync(1.0f, kFrame, kSpeed), "already on target: no further movement");
    }

    // --- 2. first selection is segment ZERO -> still initialises correctly
    // Zero is a valid position, so initialisation cannot be inferred from it.
    {
        NUISlidingIndicator ind;
        check(!ind.isInitialized(), "uninitialised before first sync");
        ind.sync(0.0f, kFrame, kSpeed);
        check(ind.isInitialized(), "syncing to zero initialises (zero is a valid position)");
        nearly(ind.position(), 0.0f, "position is zero");

        // The proof that it really initialised rather than staying unset: a
        // later change must ANIMATE, not snap.
        const bool moved = ind.sync(2.0f, kFrame, kSpeed);
        check(moved, "later change moves");
        check(ind.position() > 0.0f && ind.position() < 2.0f,
              "later change ANIMATES from zero rather than snapping");
    }

    // --- 3. change selection after initialisation -> animates
    {
        NUISlidingIndicator ind;
        ind.sync(0.0f, kFrame, kSpeed); // initialise

        ind.sync(3.0f, kFrame, kSpeed);
        check(ind.position() > 0.0f && ind.position() < 3.0f, "animates toward the new target");

        const int frames = settle(ind, 3.0f);
        check(frames > 1, "took multiple frames (it animated rather than snapping)");
        nearly(ind.position(), 3.0f, "settles exactly on the target");
    }

    // --- 4. setting the SAME selection again -> no animation restart
    {
        NUISlidingIndicator ind;
        ind.snapTo(2.0f);
        check(!ind.sync(2.0f, kFrame, kSpeed), "unchanged target: no change reported");
        nearly(ind.position(), 2.0f, "unchanged target: position untouched");

        // Repeated syncs must stay quiet — a control that keeps reporting
        // changes would repaint forever.
        for (int i = 0; i < 10; ++i) {
            check(!ind.sync(2.0f, kFrame, kSpeed), "repeated identical sync stays quiet");
        }
    }

    // --- 5. programmatic restoration -> first synchronisation snaps
    // Same path as user selection; the only difference is that nothing has
    // synchronised yet.
    {
        NUISlidingIndicator restored;
        restored.sync(4.0f, kFrame, kSpeed);
        nearly(restored.position(), 4.0f, "restored selection snaps on first sync");

        // Explicit non-animated selection also snaps, at any time.
        NUISlidingIndicator ind;
        ind.sync(0.0f, kFrame, kSpeed);
        ind.snapTo(5.0f);
        nearly(ind.position(), 5.0f, "snapTo places directly");
        check(!ind.sync(5.0f, kFrame, kSpeed), "after snapTo it is settled, not animating");
    }

    // --- 6. resize after initialisation -> stays aligned with the segment
    // Position is in SEGMENT-INDEX space, so a geometry change recomputes pixel
    // coordinates from the same index. Nothing about a resize may move it or
    // replay a selection animation.
    {
        NUISlidingIndicator ind;
        ind.sync(2.0f, kFrame, kSpeed);

        // A resize is not a selection change: the target is unchanged, so
        // syncing across it must be a no-op.
        for (int i = 0; i < 5; ++i) {
            check(!ind.sync(2.0f, kFrame, kSpeed), "resize frames do not move the indicator");
        }
        nearly(ind.position(), 2.0f, "still on its segment after resize frames");

        // Geometry rebuild that wants an explicit re-place must not animate.
        ind.snapTo(2.0f);
        nearly(ind.position(), 2.0f, "explicit geometry snap keeps the logical segment");
        check(!ind.sync(2.0f, kFrame, kSpeed), "no animation replayed after geometry snap");
    }

    // --- 7. rapid successive changes -> ends at the LATEST authoritative target
    {
        NUISlidingIndicator ind;
        ind.sync(0.0f, kFrame, kSpeed);

        ind.sync(1.0f, kFrame, kSpeed);
        ind.sync(4.0f, kFrame, kSpeed);
        ind.sync(2.0f, kFrame, kSpeed); // user lands here

        const int frames = settle(ind, 2.0f);
        check(frames > 0, "still animating toward the final target");
        nearly(ind.position(), 2.0f, "settles on the LATEST target, not an intermediate one");

        // Never overshoots past the final target during the chase.
        NUISlidingIndicator ind2;
        ind2.sync(0.0f, kFrame, kSpeed);
        for (int i = 0; i < 200; ++i) {
            ind2.sync(3.0f, kFrame, kSpeed);
            check(ind2.position() <= 3.0f + 1e-4f, "never overshoots the target");
        }
    }

    // --- backwards motion works too (higher index -> lower)
    {
        NUISlidingIndicator ind;
        ind.snapTo(5.0f);
        ind.sync(1.0f, kFrame, kSpeed);
        check(ind.position() < 5.0f && ind.position() > 1.0f, "animates backwards");
        settle(ind, 1.0f);
        nearly(ind.position(), 1.0f, "settles going backwards");
    }

    if (g_failures != 0) {
        std::cerr << "[FAIL] SlidingIndicatorTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] SlidingIndicatorTest\n";
    return 0;
}
