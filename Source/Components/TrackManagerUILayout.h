// © 2026 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V8-X2b phase 4: the timeline's regions and track rows, resolved once by the
// layout system instead of re-derived by hand in each TrackManagerUI file.
//
// Before this, layoutTracks(), the drop preview, both "place on timeline"
// paths, three hit tests and two scroll-range calculations each restated
// "time band height + index * (row height + spacing) - scroll", and two of
// them carried a comment saying they MUST match layoutTracks() exactly. That
// agreement was kept by hand. Everything here answers in the component's own
// LOCAL space (origin at its top-left); TrackManagerUI's bounds are
// window-absolute, and the one conversion between the two is localToWindow()
// at the call site — the convention the TrackManagerUIToolbar.cpp:186 comment
// used to explain in prose.
//
// Header-only and free of widget dependencies, like TrackManagerUIMath.h, so
// a test can include it without the UI targets that AESTRA_CI=ON disables.

#include "../../AestraUI/Layout/NUILayoutAlgorithms.h"
#include "../../AestraUI/Layout/NUILayoutSpace.h"
#include "TrackManagerUIMath.h"

#include <cstddef>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief The timeline's fixed regions, in the component's Local space.
 *
 * Top to bottom: the time band (minimap row, then ruler row), then the track
 * rows. The scrollbar gutter takes the trailing kTimelineScrollbarWidth of
 * every row below the band; the minimap starts at the track-controls boundary
 * and stops at the same gutter.
 */
struct TimelineLayout {
    AestraUI::Layout::NUILocalRect minimap;   //!< Overview row, cropped to the plane column.
    AestraUI::Layout::NUILocalRect scrollbar; //!< Vertical scrollbar gutter, below the time band.
    AestraUI::Layout::NUILocalRect rows;      //!< Track-row viewport: below the band, left of the gutter.
};

/** @brief Splits a component of `width` x `height` into the timeline's regions. */
/// `recorder` (V8-X2b criterion 4) names each split so an explanation can walk
/// from a row back up to the component: rows <- body.leading <- bands.trailing.
inline TimelineLayout resolveTimelineLayout(float width, float height, float trackControlsWidth,
                                            AestraUI::Layout::NUILayoutRecorder* recorder = nullptr) {
    using namespace AestraUI::Layout;
    const NUILocalRect component(0.0f, 0.0f, width, height);
    const auto name = [recorder](const char* scope, const char* from) {
        if (recorder) {
            recorder->scope(scope, from);
        }
    };

    name("bands", "");
    const auto bands = splitVertical(component, kTimelineTimeBandHeight, recorder);
    name("body", "bands.trailing");
    const auto body = splitHorizontal(bands.trailing, width - kTimelineScrollbarWidth, recorder);

    name("bandColumns", "bands.leading");
    const auto bandColumns = splitHorizontal(bands.leading, width - kTimelineScrollbarWidth, recorder);
    name("plane", "bandColumns.leading");
    const auto plane = splitHorizontal(bandColumns.leading, trackControlsWidth, recorder);
    name("minimapRow", "plane.trailing");
    const auto minimapRow = splitVertical(plane.trailing, kTimelineMinimapHeight, recorder);

    TimelineLayout layout;
    layout.minimap = minimapRow.leading;
    layout.scrollbar = body.trailing;
    layout.rows = body.leading;
    return layout;
}

/** @brief Track row i, in Local space, plus whether it falls inside the row viewport. */
inline std::vector<AestraUI::Layout::NUIScrollItem> arrangeTimelineRows(
    const TimelineLayout& layout, float rowHeight, float rowSpacing, std::size_t rowCount, float scrollOffset,
    AestraUI::Layout::NUILayoutRecorder* recorder = nullptr) {
    if (recorder) {
        recorder->scope("rows", "body.leading");
    }
    return AestraUI::Layout::arrangeScrollingStack(layout.rows, AestraUI::Layout::NUIAxis::Vertical, rowHeight,
                                                   rowSpacing, rowCount, scrollOffset, recorder);
}

/** @brief Local-space top edge of row `index` (which need not exist yet: drops target new rows). */
inline float timelineRowTopY(const TimelineLayout& layout, float rowHeight, float rowSpacing, int index,
                             float scrollOffset) {
    return layout.rows.y + (static_cast<float>(index) * (rowHeight + rowSpacing)) - scrollOffset;
}

/**
 * @brief Which row slot a Local-space y falls in: -1 above the first row (the
 * time band), and possibly >= the row count below the last one.
 */
inline int timelineRowSlotAt(const TimelineLayout& layout, float rowHeight, float rowSpacing, float scrollOffset,
                             float localY) {
    return AestraUI::Layout::scrollingStackSlotAt(layout.rows, AestraUI::Layout::NUIAxis::Vertical, rowHeight,
                                                  rowSpacing, scrollOffset, localY);
}

/** @brief Height of all rows together: what the vertical scroll range is measured against. */
inline float timelineRowsContentHeight(float rowHeight, float rowSpacing, std::size_t rowCount) {
    return AestraUI::Layout::scrollingStackContentExtent(rowHeight, rowSpacing, rowCount);
}

/** @brief Furthest the rows can scroll: content height beyond what the viewport shows, never negative. */
inline float timelineMaxVerticalScroll(const TimelineLayout& layout, float rowHeight, float rowSpacing,
                                       std::size_t rowCount) {
    const float overflow = timelineRowsContentHeight(rowHeight, rowSpacing, rowCount) - layout.rows.height;
    return overflow > 0.0f ? overflow : 0.0f;
}

} // namespace Audio
} // namespace Aestra
