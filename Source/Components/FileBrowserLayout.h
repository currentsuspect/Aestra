// © 2026 Aestra Studios All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V8-X2b phase 5: the file browser's panes and header chrome, resolved once by the
// layout system. Before this, the folder pane's clipped, scrollable region was
// computed twice — once where it was drawn and once where it was hit-tested — under
// a comment saying the two must match, after rows clipped under the header had been
// clickable. Now both read `navViewport`.
//
// Local space (origin at the browser's top-left); FileBrowser::computeBrowserLayout()
// converts to window space once. Header-only, no widget dependencies, so a test can
// include it without the UI targets AESTRA_CI=ON disables.

#include "../../AestraUI/Layout/NUILayoutAlgorithms.h"
#include "../../AestraUI/Layout/NUILayoutSpace.h"

namespace AestraUI {

struct FileBrowserLocalLayout {
    Layout::NUILocalRect searchBar;   //!< The search row across the top.
    Layout::NUILocalRect search;      //!< The query field inside it (26 in; leaves `searchTrailing` at the end).
    Layout::NUILocalRect navPane;     //!< Folder column, below the search row.
    Layout::NUILocalRect navHeader;   //!< The folder-name band at the top of navPane.
    Layout::NUILocalRect navViewport; //!< navPane below its folder-name header: drawn, clipped and clicked alike.
    Layout::NUILocalRect listHeader;  //!< The list column's header band.
    Layout::NUILocalRect list;        //!< File rows: below the header, above the preview dock.
    Layout::NUILocalRect back, forward, up; //!< Header chrome from the left.
    Layout::NUILocalRect sort, filter;      //!< Header chrome from the right.
};

/**
 * @brief Search row over content; content is nav column | list column; each column a
 * header band over its body; the list leaves `previewHeight` for the preview dock.
 * The sort control names its key ("Name", "Date", ...) once the list is 300 px wide;
 * narrower lists keep the bare 22 px glyph.
 */
inline FileBrowserLocalLayout resolveFileBrowserLayout(float width, float height, float searchRowHeight,
                                                       float searchTrailing, float navWidth, float listHeaderHeight,
                                                       float previewHeight, Layout::NUILayoutRecorder* trace = nullptr) {
    using namespace Layout;
    const auto at = [trace](const char* scope, const char* from) {
        if (trace) trace->scope(scope, from);
    };
    FileBrowserLocalLayout l;
    at("rows", "");
    const auto rows = splitVertical(NUILocalRect(0.0f, 0.0f, width, height), searchRowHeight, trace);
    at("cols", "rows.trailing");
    const auto cols = splitHorizontal(rows.trailing, navWidth, trace);
    at("nav", "cols.leading");
    const auto nav = splitVertical(cols.leading, listHeaderHeight, trace);
    at("listCol", "cols.trailing");
    const auto listCol = splitVertical(cols.trailing, listHeaderHeight, trace);
    at("list", "listCol.trailing");
    const auto list = splitVertical(listCol.trailing, listCol.trailing.height - previewHeight, trace);

    // Header chrome sits 5 px down in a 24 px row: back · forward · up from the
    // left, sort · filter from the right, each 5 px in and 2 px apart.
    const NUILocalRect chrome(listCol.leading.x, listCol.leading.y + 5.0f, listCol.leading.width, 24.0f);
    at("navButtons", "listCol.leading");
    const auto left = arrangeLeadingRow(chrome, {22.0f, 22.0f, 22.0f}, 24.0f, 2.0f, 5.0f, trace);
    at("tools", "listCol.leading");
    const float sortWidth = listCol.leading.width >= 300.0f ? 62.0f : 22.0f;
    const auto right = arrangeTrailingRow(chrome, {sortWidth, 22.0f}, 24.0f, 2.0f, 5.0f, trace);

    l.searchBar = rows.leading;
    l.search = NUILocalRect(26.0f, 4.0f, width - searchTrailing > 0.0f ? width - searchTrailing : 0.0f,
                            searchRowHeight - 8.0f);
    l.navPane = cols.leading;
    l.navHeader = nav.leading;
    l.navViewport = nav.trailing;
    l.listHeader = listCol.leading;
    l.list = list.leading;
    l.back = left[0];
    l.forward = left[1];
    l.up = left[2];
    l.sort = right[0];
    l.filter = right[1];
    return l;
}

} // namespace AestraUI
