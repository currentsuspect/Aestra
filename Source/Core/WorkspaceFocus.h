// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file WorkspaceFocus.h
 * @brief Pure, headless-constructible workspace-focus model.
 *
 * Phase-3 "workspace panel ownership": the segmented control, ViewFocus
 * transitions, and overlay-panel visibility all key off this single model so
 * the segment index<->focus mapping, transition classification, and
 * remembered-open derivation are table-testable without constructing
 * AestraContent (which cannot be built headlessly; see
 * Tests/AestraAudio/ProjectDirtyStateTest.cpp).
 *
 * The model owns no state; all functions are pure. ViewFocus stays an
 * app-level, global-scope enum so existing protocol/file users keep compiling
 * unchanged (the protocol strings live here too, next to the enum).
 */

#pragma once

#include <array>
#include <cstddef>
#include <string>

/**
 * @brief View focus - which workspace is emphasized.
 * Arsenal, Timeline, and Audition are the three main modes; RoutingMap is a
 * full-panel overlay; PianoRoll is a first-class workspace reachable from the
 * segmented control and from contextual pattern navigation.
 */
enum class ViewFocus {
    Arsenal,    // Pattern construction/sound design
    Timeline,   // Arrangement/composition
    Audition,   // Album listening/reference/DSP preview
    RoutingMap, // Full-panel routing visualization
    PianoRoll   // Pattern editing workspace
};

namespace WorkspaceFocusModel {

/**
 * @brief Canonical segmented-control segments, in displayed order.
 * Segment 3 (PianoRoll) is the phase-3 addition. The array is capture-by-value
 * at NUISegmentedControl construction, so a fourth segment must be present
 * here up front.
 */
inline constexpr size_t kSegmentCount = 4;
inline constexpr std::array<const char*, kSegmentCount> kSegmentLabels = {"Arsenal", "Timeline", "Audition",
                                                                          "PianoRoll"};

/** @brief True when the focus owns a segmented-control segment. */
inline constexpr bool hasSegment(ViewFocus focus) {
    return focus != ViewFocus::RoutingMap;
}

/**
 * @brief Segment index -> focus.
 * Out-of-range indices fall back to Timeline, matching the pre-existing toggle
 * default behavior.
 */
inline ViewFocus focusForSegmentIndex(size_t index) {
    switch (index) {
    case 0:
        return ViewFocus::Arsenal;
    case 1:
        return ViewFocus::Timeline;
    case 2:
        return ViewFocus::Audition;
    case 3:
        return ViewFocus::PianoRoll;
    default:
        return ViewFocus::Timeline;
    }
}

/**
 * @brief Focus -> segment index.
 * @return True when the focus owns a segment (outIndex is written). False for
 * foci without one (RoutingMap) — callers leave the prior selection untouched.
 */
inline bool segmentIndexForFocus(ViewFocus focus, size_t& outIndex) {
    switch (focus) {
    case ViewFocus::Arsenal:
        outIndex = 0;
        return true;
    case ViewFocus::Timeline:
        outIndex = 1;
        return true;
    case ViewFocus::Audition:
        outIndex = 2;
        return true;
    case ViewFocus::PianoRoll:
        outIndex = 3;
        return true;
    case ViewFocus::RoutingMap:
        return false;
    }
    return false;
}

/**
 * @brief What a focus switch is allowed to mutate.
 * This is the single source of truth used by the transport paths in
 * AestraContent. Ordinary transitions (including every PianoRoll pair) are
 * pure visibility changes: no playback, pause, position, scheduled-instance,
 * or engine-mode mutation.
 */
enum class WorkspaceTransitionKind {
    PlaybackHotSwap, // Arsenal<->Timeline: re-arm pattern/arrangement transport (stop + play)
    Audition,        // enters or exits Audition: full teardown + audition engine
    RoutingMap,      // enters or exits RoutingMap: overlay only
    Ordinary         // everything else: pure visibility, zero transport/engine mutation
};

/** @brief Classify a current<->previous focus pair. */
inline WorkspaceTransitionKind classifyTransition(ViewFocus current, ViewFocus previous) {
    if (current == ViewFocus::Audition || previous == ViewFocus::Audition) {
        return WorkspaceTransitionKind::Audition;
    }
    if (current == ViewFocus::RoutingMap || previous == ViewFocus::RoutingMap) {
        return WorkspaceTransitionKind::RoutingMap;
    }
    const bool currentIsDaw = (current == ViewFocus::Arsenal || current == ViewFocus::Timeline);
    const bool previousIsDaw = (previous == ViewFocus::Arsenal || previous == ViewFocus::Timeline);
    if (currentIsDaw && previousIsDaw) {
        return WorkspaceTransitionKind::PlaybackHotSwap;
    }
    return WorkspaceTransitionKind::Ordinary;
}

/** @brief True when either side of the transition is Audition. */
inline bool isAuditionTransition(ViewFocus current, ViewFocus previous) {
    return classifyTransition(current, previous) == WorkspaceTransitionKind::Audition;
}

/** @brief True when either side of the transition is RoutingMap. */
inline bool isRoutingMapTransition(ViewFocus current, ViewFocus previous) {
    return classifyTransition(current, previous) == WorkspaceTransitionKind::RoutingMap;
}

/** @brief True for ordinary (pure-visibility) transitions, including PianoRoll pairs. */
inline bool isOrdinaryWorkspaceTransition(ViewFocus current, ViewFocus previous) {
    return classifyTransition(current, previous) == WorkspaceTransitionKind::Ordinary;
}

/**
 * @brief Effective overlay-panel visibility for a focus, derived from the
 *        remembered-open state. Ordinary workspaces (Timeline/Arsenal/
 *        PianoRoll) restore overlays from remembered-open; Audition hides
 *        everything; RoutingMap shows only the mixer over its own map panel.
 */
struct WorkspacePanelVisibility {
    bool mixer = false;
    bool pianoRoll = false;
    bool sequencer = false;
};

inline WorkspacePanelVisibility derivePanelVisibility(ViewFocus focus, bool mixerOpen, bool pianoRollOpen,
                                                      bool sequencerOpen) {
    if (focus == ViewFocus::Audition) {
        return {false, false, false};
    }
    if (focus == ViewFocus::RoutingMap) {
        return {mixerOpen, false, false};
    }
    return {mixerOpen, pianoRollOpen, sequencerOpen};
}

/**
 * @brief Stable protocol/persistence name for a focus. Agents and project files
 *        hardcode these strings; renaming one is a breaking change.
 */
inline const char* workspaceFocusName(ViewFocus focus) {
    switch (focus) {
    case ViewFocus::Arsenal:
        return "arsenal";
    case ViewFocus::Timeline:
        return "timeline";
    case ViewFocus::Audition:
        return "audition";
    case ViewFocus::RoutingMap:
        return "routingMap";
    case ViewFocus::PianoRoll:
        return "pianoRoll";
    }
    return "unknown";
}

/** @brief Parse a persisted/protocol focus name (case-sensitive, matching workspaceFocusName). */
inline bool parseWorkspaceFocus(const std::string& name, ViewFocus& out) {
    if (name == "arsenal") {
        out = ViewFocus::Arsenal;
        return true;
    }
    if (name == "timeline") {
        out = ViewFocus::Timeline;
        return true;
    }
    if (name == "audition") {
        out = ViewFocus::Audition;
        return true;
    }
    if (name == "routingMap") {
        out = ViewFocus::RoutingMap;
        return true;
    }
    if (name == "pianoRoll") {
        out = ViewFocus::PianoRoll;
        return true;
    }
    return false;
}

} // namespace WorkspaceFocusModel