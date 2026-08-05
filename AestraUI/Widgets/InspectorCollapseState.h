// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

namespace AestraUI {

/**
 * @brief Collapse state for the mixer inspector: explicit intent vs. derived fit.
 *
 * Two states, not one. A single bool cannot express "the user wants this open,
 * but there is no room for it right now" — and collapsing that distinction is
 * how a responsive layout ends up destroying a preference the user set
 * deliberately.
 *
 *   effectiveExpanded = expandedPreference && !forcedCollapsed
 *
 * - `expandedPreference` is the user's last *explicit* choice. It is the only
 *   part that persists, and width changes must never write to it.
 * - `forcedCollapsed` is derived from available width and is recomputed on
 *   every layout. It is never persisted.
 *
 * Kept as a plain struct with no UI or renderer dependency so the transition
 * sequences can be tested directly — those orderings are the easy thing to get
 * wrong, and they are invisible in a screenshot.
 */
struct InspectorCollapseState {
    /// Persisted explicit user choice. First run defaults to expanded.
    bool expandedPreference{true};

    /// Derived from available width; recomputed per layout, never persisted.
    bool forcedCollapsed{false};

    /// What the panel should actually draw.
    bool effectiveExpanded() const { return expandedPreference && !forcedCollapsed; }

    /// Recompute the width-driven constraint. Deliberately does not touch the
    /// preference — that is the whole point of keeping the two apart.
    void setForcedCollapsed(bool forced) { forcedCollapsed = forced; }

    /**
     * @brief Handle a click on the collapse rail.
     *
     * While width-constrained the panel is collapsed no matter what the
     * preference says, so the only intent a click can express there is "I want
     * this open". Recording that (rather than toggling, or ignoring the click)
     * means the inspector reappears by itself once the width returns, instead
     * of silently discarding what the user asked for.
     */
    void onRailClicked()
    {
        if (forcedCollapsed) {
            expandedPreference = true;
            return;
        }
        expandedPreference = !expandedPreference;
    }

    /// Restore the first-run default (reset layout / reset preferences).
    void reset() { expandedPreference = true; forcedCollapsed = false; }
};

} // namespace AestraUI
