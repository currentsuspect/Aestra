// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Sliding-indicator animation state for segmented controls (#653 follow-up).
//
// The defect this exists to prevent: an indicator whose position is simply
// default-initialized to 0 animates from segment 0 to its real segment on the
// first frames, so a control that opens on any segment other than the first
// visibly slides in from the left — animating from a position it was never in.
//
// Aestra's view toggle defaults to "Timeline" (index 1), which made it visible
// on every launch. Four other segmented controls avoided it only because they
// happen to select index 0, which is where the default position already sits.
// That is luck, not correctness, and it is why this is caller-proof rather than
// an `animate=false` argument every call site has to remember.
//
// Deliberately free of widget, renderer and theme dependencies so the behaviour
// can be tested without linking AestraUI (which AESTRA_CI=ON disables).

#include <cmath>

namespace AestraUI {

/**
 * @brief Position of a sliding selection indicator, in SEGMENT-INDEX space.
 *
 * Index space rather than pixels is what makes resizing safe: the logical
 * position is resolution-independent, so a geometry change recomputes pixel
 * coordinates from the same index without replaying a selection animation.
 */
class NUISlidingIndicator {
public:
    /// Distance below which the indicator is considered settled on its target.
    static constexpr float kSettleEpsilon = 0.01f;

    /**
     * @brief Has this indicator ever been synchronised to an authoritative target?
     *
     * Tracked explicitly. It must NOT be inferred from the position value —
     * 0.0f is a perfectly valid position (segment zero), so "position == 0"
     * cannot distinguish "never initialised" from "correctly sitting on the
     * first segment". Conflating them is the original defect.
     */
    bool isInitialized() const { return initialized_; }

    /// Current position, in segment-index space.
    float position() const { return position_; }

    /**
     * @brief Synchronise toward the authoritative selected segment.
     *
     * The FIRST synchronisation snaps: an indicator that has never had a real
     * position has nothing to animate from. Every later synchronisation
     * animates, and only when the target actually differs from where the
     * indicator already is.
     *
     * @return true when the position changed and the caller should repaint.
     */
    bool sync(float target, double deltaTime, float speed) {
        if (!initialized_) {
            position_ = target;
            initialized_ = true;
            return true;
        }

        const float diff = target - position_;
        if (std::abs(diff) <= kSettleEpsilon) {
            if (position_ != target) {
                position_ = target; // settle exactly; no further frames needed
                return true;
            }
            return false; // already there — unchanged target does nothing
        }

        position_ += diff * speed * static_cast<float>(deltaTime);
        if (std::abs(target - position_) < kSettleEpsilon) {
            position_ = target;
        }
        return true;
    }

    /**
     * @brief Place the indicator directly, without animating.
     *
     * For an explicitly non-animated selection, and for any geometry change
     * that must preserve the logical segment rather than replay a selection
     * animation. Counts as initialisation.
     */
    void snapTo(float target) {
        position_ = target;
        initialized_ = true;
    }

private:
    float position_ = 0.0f;
    bool initialized_ = false;
};

} // namespace AestraUI
