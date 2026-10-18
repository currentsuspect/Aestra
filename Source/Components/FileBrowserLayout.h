// © 2026 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The file browser's bands, resolved once by the layout system (V8-X2b phase 5).
//
// FD-47 (v0.8.1 look): one column, top to bottom:
//   kind tabs (Sounds | Patterns | Plugins) · search · location (back, the place
//   chip that opens Places, breadcrumbs) · colour-tag chips · list header
//   (count, sort, filter) · rows · preview dock.
// The folder rail is gone. Places is an overlay over the list (navPane), drawn,
// clipped and hit-tested by the same navViewport rect, as the rail was.
//
// Local space (origin at the browser's top-left); FileBrowser::computeBrowserLayout()
// converts to window space once. Header-only, no widget dependencies, so a test can
// include it without the UI targets AESTRA_CI=ON disables.

#include "../../AestraUI/Layout/NUILayoutAlgorithms.h"
#include "../../AestraUI/Layout/NUILayoutSpace.h"

#include <algorithm>
#include <array>

namespace AestraUI {

struct FileBrowserBands {
    float kindRow = 40.0f;     //!< The Sounds | Patterns | Plugins segment.
    float searchRow = 36.0f;   //!< The search field (28) with 4 px above and below.
    float locationRow = 30.0f; //!< 0 when an embedded view (Patterns, Plugins) owns the body.
    float chipsRow = 0.0f;     //!< 28 when there are colour tags to show, else 0.
    float listHeader = 26.0f;  //!< 0 when an embedded view owns the body.
    float preview = 0.0f;      //!< The preview dock under the rows; 0 when hidden.
};

struct FileBrowserLocalLayout {
    Layout::NUILocalRect kindRow;               //!< The segment band.
    std::array<Layout::NUILocalRect, 3> kinds;  //!< Sounds, Patterns, Plugins.
    Layout::NUILocalRect searchBar;             //!< The search band.
    Layout::NUILocalRect search;                //!< The query field inside it.
    Layout::NUILocalRect location;              //!< The location band.
    Layout::NUILocalRect back;                  //!< Back, at the start of the location band.
    Layout::NUILocalRect path;                  //!< Place chip and breadcrumbs: the rest of the band.
    Layout::NUILocalRect chips;                 //!< Colour-tag chips band.
    Layout::NUILocalRect listHeader;            //!< Count on the left, sort and filter on the right.
    Layout::NUILocalRect sort, filter;          //!< Header controls from the right.
    Layout::NUILocalRect list;                  //!< Rows: below the header, above the preview dock.
    Layout::NUILocalRect body;                  //!< Everything under the search band (embedded views fill it).
    Layout::NUILocalRect navPane;               //!< Places overlay: from the location band's foot to the bottom.
    Layout::NUILocalRect navHeader;             //!< The overlay's title band.
    Layout::NUILocalRect navViewport;           //!< The overlay's rows: drawn, clipped and clicked alike.
};

/**
 * @brief Stacks the bands top to bottom at @p width x @p height. The search field
 * leaves @p searchTrailing px at its end for the scope chip and the clear button.
 * The sort control names its key ("Name", "Date", ...) once the browser is 240 px wide.
 */
inline FileBrowserLocalLayout resolveFileBrowserLayout(float width, float height, const FileBrowserBands& bands,
                                                       float searchTrailing, Layout::NUILayoutRecorder* trace = nullptr) {
    using namespace Layout;
    const auto at = [trace](const char* scope, const char* from) {
        if (trace) trace->scope(scope, from);
    };
    constexpr float kPad = 10.0f;
    FileBrowserLocalLayout l;

    at("kind", "");
    const auto kind = splitVertical(NUILocalRect(0.0f, 0.0f, width, height), bands.kindRow, trace);
    at("search", "kind.trailing");
    const auto search = splitVertical(kind.trailing, bands.searchRow, trace);
    at("location", "search.trailing");
    const auto location = splitVertical(search.trailing, bands.locationRow, trace);
    at("chips", "location.trailing");
    const auto chips = splitVertical(location.trailing, bands.chipsRow, trace);
    at("header", "chips.trailing");
    const auto header = splitVertical(chips.trailing, bands.listHeader, trace);
    at("list", "header.trailing");
    const auto list = splitVertical(header.trailing, std::max(0.0f, header.trailing.height - bands.preview), trace);

    l.kindRow = kind.leading;
    // Three equal tabs inside a 28 px segment, 8 px from the top, 2 px inset.
    const float segW = std::max(0.0f, width - kPad * 2.0f);
    const float tabW = std::max(0.0f, (segW - 4.0f - 4.0f) / 3.0f);
    for (int i = 0; i < 3; ++i) {
        l.kinds[static_cast<size_t>(i)] =
            NUILocalRect(kPad + 2.0f + static_cast<float>(i) * (tabW + 2.0f), 10.0f, tabW, 24.0f);
    }

    l.searchBar = search.leading;
    l.search = NUILocalRect(kPad + 24.0f, search.leading.y + 4.0f,
                            std::max(0.0f, width - kPad - 24.0f - searchTrailing), 28.0f);

    l.location = location.leading;
    if (bands.locationRow > 0.0f) {
        l.back = NUILocalRect(kPad - 4.0f, location.leading.y + 4.0f, 22.0f, 22.0f);
        l.path = NUILocalRect(l.back.right() + 4.0f, location.leading.y + 4.0f,
                              std::max(0.0f, width - l.back.right() - 4.0f - kPad), 22.0f);
    }
    l.chips = chips.leading;

    l.listHeader = header.leading;
    if (bands.listHeader > 0.0f) {
        const NUILocalRect controls(0.0f, header.leading.y + 2.0f, width, 22.0f);
        const float sortWidth = width >= 240.0f ? 62.0f : 22.0f;
        const auto right = arrangeTrailingRow(controls, {sortWidth, 22.0f}, 22.0f, 2.0f, kPad - 4.0f, trace);
        l.sort = right[0];
        l.filter = right[1];
    }
    l.list = list.leading;
    l.body = search.trailing;

    // Places drops over everything under the location band.
    const float overlayTop = location.leading.bottom();
    l.navPane = NUILocalRect(0.0f, overlayTop, width, std::max(0.0f, height - overlayTop));
    const float navHeaderH = std::min(l.navPane.height, 34.0f);
    l.navHeader = NUILocalRect(0.0f, overlayTop, width, navHeaderH);
    l.navViewport = NUILocalRect(0.0f, overlayTop + navHeaderH, width, std::max(0.0f, l.navPane.height - navHeaderH));
    return l;
}

} // namespace AestraUI
