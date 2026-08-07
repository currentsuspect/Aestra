// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "WorkspaceFocus.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        std::exit(1);
    }
}

void testSegmentMapping() {
    // Every segment index maps to the canonical focus, and every focus with a
    // segment round-trips back to the same index. RoutingMap owns no segment;
    // the piano roll is a contextual editor, not a workspace, so it owns an
    // index here either.
    require(WorkspaceFocusModel::focusForSegmentIndex(0) == ViewFocus::Arsenal, "segment 0 must be Arsenal");
    require(WorkspaceFocusModel::focusForSegmentIndex(1) == ViewFocus::Timeline, "segment 1 must be Timeline");
    require(WorkspaceFocusModel::focusForSegmentIndex(2) == ViewFocus::Audition, "segment 2 must be Audition");
    require(WorkspaceFocusModel::focusForSegmentIndex(3) == ViewFocus::Timeline,
            "segment 3 must NOT exist (fall back to Timeline)");
    require(WorkspaceFocusModel::focusForSegmentIndex(99) == ViewFocus::Timeline,
            "out-of-range segment must fall back to Timeline");

    const ViewFocus kSegmentFoci[] = {ViewFocus::Arsenal, ViewFocus::Timeline, ViewFocus::Audition};
    for (size_t index = 0; index < WorkspaceFocusModel::kSegmentCount; ++index) {
        size_t back = 99;
        require(WorkspaceFocusModel::segmentIndexForFocus(kSegmentFoci[index], back),
                "segment focus must own an index");
        require(back == index, "focus -> segment index must round-trip");
    }

    size_t unused = 99;
    require(!WorkspaceFocusModel::segmentIndexForFocus(ViewFocus::RoutingMap, unused),
            "RoutingMap must not own a segment");
    require(unused == 99, "failed lookup must leave the output untouched");
}

void testTransitionClassification() {
    const ViewFocus kAllFoci[] = {ViewFocus::Arsenal, ViewFocus::Timeline, ViewFocus::Audition,
                                  ViewFocus::RoutingMap};

    for (ViewFocus current : kAllFoci) {
        for (ViewFocus previous : kAllFoci) {
            const auto kind = WorkspaceFocusModel::classifyTransition(current, previous);

            const bool anyAudition = (current == ViewFocus::Audition || previous == ViewFocus::Audition);
            const bool anyRoutingMap = (current == ViewFocus::RoutingMap || previous == ViewFocus::RoutingMap);
            const bool bothDaw = (current == ViewFocus::Arsenal || current == ViewFocus::Timeline) &&
                                 (previous == ViewFocus::Arsenal || previous == ViewFocus::Timeline);
            const bool distinctDawSwap = bothDaw && current != previous;

            if (anyAudition) {
                require(kind == WorkspaceFocusModel::WorkspaceTransitionKind::Audition,
                        "any Audition pair must classify as Audition");
            } else if (anyRoutingMap) {
                require(kind == WorkspaceFocusModel::WorkspaceTransitionKind::RoutingMap,
                        "any RoutingMap pair must classify as RoutingMap");
            } else if (distinctDawSwap) {
                require(kind == WorkspaceFocusModel::WorkspaceTransitionKind::PlaybackHotSwap,
                        "distinct Arsenal/Timeline pairs must classify as PlaybackHotSwap");
            } else if (bothDaw) {
                require(kind == WorkspaceFocusModel::WorkspaceTransitionKind::Ordinary,
                        "Arsenal/Timeline self-switches must stay ordinary (no hot-swap)");
            } else {
                require(kind == WorkspaceFocusModel::WorkspaceTransitionKind::Ordinary,
                        "remaining pairs must classify as Ordinary");
            }
        }
    }

    // DAW self-switches are ordinary: they must never re-arm the transport.
    require(WorkspaceFocusModel::isOrdinaryWorkspaceTransition(ViewFocus::Arsenal, ViewFocus::Arsenal),
            "Arsenal self-switch must be ordinary (no hot-swap)");
    require(WorkspaceFocusModel::isOrdinaryWorkspaceTransition(ViewFocus::Timeline, ViewFocus::Timeline),
            "Timeline self-switch must be ordinary (no hot-swap)");
    require(!WorkspaceFocusModel::isOrdinaryWorkspaceTransition(ViewFocus::Arsenal, ViewFocus::Timeline),
            "Arsenal <-> Timeline must NOT be ordinary (hot-swap)");
}

void testPanelVisibility() {
    // Ordinary workspaces (Arsenal/Timeline) restore every overlay from
    // remembered-open, including the piano-roll editor flag; Audition hides
    // everything; RoutingMap shows only the mixer over its own map.
    for (ViewFocus focus : {ViewFocus::Arsenal, ViewFocus::Timeline}) {
        const auto vis = WorkspaceFocusModel::derivePanelVisibility(focus, true, true, true);
        require(vis.mixer && vis.pianoRoll && vis.sequencer,
                "ordinary workspace must restore all remembered-open overlays");
    }

    const auto audition = WorkspaceFocusModel::derivePanelVisibility(ViewFocus::Audition, true, true, true);
    require(!audition.mixer && !audition.pianoRoll && !audition.sequencer,
            "Audition must hide all overlays regardless of remembered-open");

    const auto routing = WorkspaceFocusModel::derivePanelVisibility(ViewFocus::RoutingMap, true, true, true);
    require(routing.mixer && !routing.pianoRoll && !routing.sequencer, "RoutingMap must show only the mixer");

    // The piano-roll editor only appears when remembered-open.
    const auto prClosed = WorkspaceFocusModel::derivePanelVisibility(ViewFocus::Timeline, true, false, true);
    require(prClosed.mixer && !prClosed.pianoRoll && prClosed.sequencer,
            "piano-roll editor shows iff pianoRollOpen");
}

void testNames() {
    require(std::string(WorkspaceFocusModel::workspaceFocusName(ViewFocus::Arsenal)) == "arsenal",
            "Arsenal protocol name");
    require(std::string(WorkspaceFocusModel::workspaceFocusName(ViewFocus::Timeline)) == "timeline",
            "Timeline protocol name");
    require(std::string(WorkspaceFocusModel::workspaceFocusName(ViewFocus::Audition)) == "audition",
            "Audition protocol name");
    require(std::string(WorkspaceFocusModel::workspaceFocusName(ViewFocus::RoutingMap)) == "routingMap",
            "RoutingMap protocol name");

    const ViewFocus kAllFoci[] = {ViewFocus::Arsenal, ViewFocus::Timeline, ViewFocus::Audition,
                                  ViewFocus::RoutingMap};
    for (ViewFocus focus : kAllFoci) {
        ViewFocus parsed = ViewFocus::Timeline;
        require(WorkspaceFocusModel::parseWorkspaceFocus(WorkspaceFocusModel::workspaceFocusName(focus), parsed),
                "parse must accept every focus name");
        require(parsed == focus, "focus name must round-trip");
    }

    ViewFocus parsed = ViewFocus::Timeline;
    require(!WorkspaceFocusModel::parseWorkspaceFocus("timelime", parsed), "typo'd focus name must not parse");
    require(!WorkspaceFocusModel::parseWorkspaceFocus("", parsed), "empty focus name must not parse");
    // Phase-3 builds persisted "pianoRoll" as a workspace focus. The piano roll
    // is now a contextual editor, not a workspace, so that legacy value must be
    // rejected (callers fall back to Timeline) rather than resurrected.
    require(!WorkspaceFocusModel::parseWorkspaceFocus("pianoRoll", parsed),
            "legacy pianoRoll focus must not parse (editor, not workspace)");
}

} // namespace

int main() {
    testSegmentMapping();
    testTransitionClassification();
    testPanelVisibility();
    testNames();
    std::cout << "WorkspaceFocusModelTest: all checks passed\n";
    return 0;
}
