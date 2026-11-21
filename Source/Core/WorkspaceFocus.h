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
 * Arsenal, Timeline, and Audition are the three main workspaces; RoutingMap is
 * a full-panel overlay. The piano roll is NOT a workspace: it is a contextual
 * editor for musical data inside an owning workspace, opened by pattern-level
 * navigation (see AestraContent::openPatternInPianoRoll), and it retains that
 * owning focus while open. Keeping it out of ViewFocus means later editors
 * (clip editor, automation lane) extend the model without pretending each one
 * is a new application mode.
 */
enum class ViewFocus {
    Arsenal,    // Pattern construction/sound design
    Timeline,   // Arrangement/composition
    Audition,   // Album listening/reference/DSP preview
    RoutingMap  // Full-panel routing visualization
};

namespace WorkspaceFocusModel {

/**
 * @brief Canonical segmented-control segments, in displayed order.
 * The array is captured by-value at NUISegmentedControl construction, so every
 * segment must be present here up front.
 */
inline constexpr size_t kSegmentCount = 3;
inline constexpr std::array<const char*, kSegmentCount> kSegmentLabels = {"Arsenal", "Timeline", "Audition"};

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
    case ViewFocus::RoutingMap:
        return false;
    }
    return false;
}

/**
 * @brief What a focus switch is allowed to mutate.
 * This is the single source of truth used by the transport paths in
 * AestraContent. Ordinary transitions are pure visibility changes: no playback,
 * pause, position, scheduled-instance, or engine-mode mutation. The piano roll
 * editor never participates here — it is a contextual editor, not a focus.
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
    if (current != previous && currentIsDaw && previousIsDaw) {
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

/** @brief True for ordinary (pure-visibility) transitions. */
inline bool isOrdinaryWorkspaceTransition(ViewFocus current, ViewFocus previous) {
    return classifyTransition(current, previous) == WorkspaceTransitionKind::Ordinary;
}

/**
 * @brief Effective overlay-panel visibility for a focus, derived from the
 *        remembered-open state. Ordinary workspaces (Timeline/Arsenal) restore
 *        overlays from remembered-open; Audition hides everything; RoutingMap
 *        shows only the mixer over its own map panel.
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
    // The piano roll editor is a remembered-open overlay inside the owning
    // workspace; its flag applies here without PianoRoll being a focus.
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
    // Legacy "pianoRoll" persisted by phase-3 builds is deliberately NOT
    // accepted: the piano roll is a contextual editor, not a workspace. Files
    // saved with it fall back to Timeline (parse failure -> caller default).
    return false;
}

/**
 * @brief Where the file browser's preview-dock button sends a sound: it follows the mode the user is listening in.
 *
 * In the Arsenal the sound becomes a sampler row; in the Timeline it is placed on the arrangement. Anywhere else
 * (the routing map; Audition hides the dock) the Arsenal is the sensible home for a new sound.
 */
enum class PreviewAddTarget { Arsenal, Timeline };

inline constexpr PreviewAddTarget previewAddTarget(ViewFocus focus) {
    return focus == ViewFocus::Timeline ? PreviewAddTarget::Timeline : PreviewAddTarget::Arsenal;
}

inline constexpr const char* previewAddLabel(PreviewAddTarget target) {
    return target == PreviewAddTarget::Timeline ? "Add to Timeline" : "Add to Arsenal";
}

/**
 * @brief The surface the user last pressed in. Focus decides where Delete, Backspace, Escape, F2 and Ctrl+A go.
 *
 * The piano roll is an overlay that never takes widget focus and leaves the owning ViewFocus untouched, so the
 * view focus alone cannot say which surface the user is working in. A press inside the piano roll makes it the
 * key surface; a press anywhere else hands the keys back to the workspace.
 */
enum class KeySurface { Workspace, PianoRoll };

/** @brief True when the Arsenal should interpret the selection-level keys (Delete, Backspace, Escape, F2, Ctrl+A). */
inline constexpr bool arsenalOwnsSelectionKeys(ViewFocus focus, bool pianoRollVisible, KeySurface lastPressed) {
    return focus == ViewFocus::Arsenal && !(pianoRollVisible && lastPressed == KeySurface::PianoRoll);
}

/**
 * @brief What can change which surface owns the keys. One reducer, so every path through the app obeys the same rules.
 *
 *  - the piano roll becoming visible (button, F7, a double-click on a pattern, a workspace switch that restores it) makes it
 *    the key surface at once; becoming hidden hands the keys back to the workspace (the Arsenal);
 *  - a left press inside a panel makes that panel the owner;
 *  - the pointer merely moving over a panel never does;
 *  - a unit added with "+ Add a sound" is an Arsenal action, so the keys go to the Arsenal (F2 renames the new row).
 */
enum class KeySurfaceEvent { PianoRollOpened, PianoRollClosed, PressInPianoRoll, PressElsewhere, PointerHover, UnitAdded };

inline constexpr KeySurface nextKeySurface(KeySurface current, KeySurfaceEvent event) {
    switch (event) {
    case KeySurfaceEvent::PianoRollOpened:
    case KeySurfaceEvent::PressInPianoRoll:
        return KeySurface::PianoRoll;
    case KeySurfaceEvent::PianoRollClosed:
    case KeySurfaceEvent::PressElsewhere:
    case KeySurfaceEvent::UnitAdded:
        return KeySurface::Workspace;
    case KeySurfaceEvent::PointerHover:
        return current;
    }
    return current;
}

/// The panel that owns the keys, for the key-owner tick. At most one: the tick is drawn on that panel's title only.
enum class KeyOwner { None, Arsenal, PianoRoll };

inline constexpr KeyOwner keyOwner(ViewFocus focus, bool pianoRollVisible, KeySurface lastPressed) {
    if (pianoRollVisible && lastPressed == KeySurface::PianoRoll) return KeyOwner::PianoRoll;
    return arsenalOwnsSelectionKeys(focus, pianoRollVisible, lastPressed) ? KeyOwner::Arsenal : KeyOwner::None;
}

/// The app publishes the owner here once per frame so a panel can ask "do I own the keys?" while drawing its title,
/// without knowing about AestraContent. UI thread only.
inline KeyOwner& publishedKeyOwner() {
    static KeyOwner owner = KeyOwner::None;
    return owner;
}
inline KeyOwner currentKeyOwner() { return publishedKeyOwner(); }
inline void setCurrentKeyOwner(KeyOwner owner) { publishedKeyOwner() = owner; }

} // namespace WorkspaceFocusModel
