// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-X2b phase 5: the file browser's panes and header chrome (FileBrowserLayout.h),
// checked against the arithmetic computeBrowserLayout() did by hand before, and the
// one rect the folder pane is now drawn, clipped and hit-tested by (navViewport).
// Real constants: search row 56, list header 34, preview dock 72. Header-only.

#include "FileBrowserLayout.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;
void check(bool c, const std::string& what) {
    if (!c) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
bool same(const AestraUI::Layout::NUILocalRect& r, float x, float y, float w, float h) {
    return near(r.x, x) && near(r.y, y) && near(r.width, w) && near(r.height, h);
}

constexpr float kSearch = 56.0f, kHeader = 34.0f, kPreview = 72.0f;

void testMatchesTheHandWrittenLayout() {
    for (float w : {280.0f, 420.0f, 560.0f}) {
        for (float h : {300.0f, 720.0f}) {
            for (bool preview : {false, true}) {
                for (bool emptyQuery : {true, false}) {
                    const float navW = std::round(w * 0.38f);
                    // --- pre-migration computeBrowserLayout(), origin 0,0 ---
                    const float contentY = kSearch, contentH = std::max(0.0f, h - kSearch);
                    const float listX = navW, listW = std::max(0.0f, w - navW);
                    const float previewH = preview ? std::min(kPreview, contentH) : 0.0f;
                    const float trailing = emptyQuery ? 34.0f : 60.0f;
                    const float chromeY = contentY + 5.0f;
                    const float sortW = listW >= 300.0f ? 62.0f : 22.0f;
                    const float sortX = listX + listW - 5.0f - sortW;
                    // --------------------------------------------------------
                    const auto l = AestraUI::resolveFileBrowserLayout(w, h, kSearch, trailing, navW, kHeader, previewH);
                    const std::string at = " at " + std::to_string(int(w)) + "x" + std::to_string(int(h)) +
                                           (preview ? " +preview" : "") + (emptyQuery ? "" : " +query");
                    check(same(l.searchBar, 0, 0, w, kSearch), "search row" + at);
                    check(same(l.search, 26, 4, std::max(0.0f, w - trailing), kSearch - 8), "search field" + at);
                    check(same(l.navPane, 0, contentY, navW, contentH), "nav pane" + at);
                    check(same(l.listHeader, listX, contentY, listW, kHeader), "list header" + at);
                    check(same(l.list, listX, contentY + kHeader, listW, std::max(0.0f, contentH - kHeader - previewH)),
                          "list" + at);
                    check(same(l.back, listX + 5, chromeY, 22, 24) && same(l.forward, listX + 29, chromeY, 22, 24) &&
                              same(l.up, listX + 53, chromeY, 22, 24),
                          "back/forward/up" + at);
                    check(same(l.sort, sortX, chromeY, sortW, 24) && same(l.filter, sortX - 24, chromeY, 22, 24),
                          "sort/filter" + at);
                }
            }
        }
    }
}

void testOneRectForTheFolderRows() {
    // The old render side clipped to navPane.y + header .. navPane.bottom(); the old
    // hit test accepted navPane minus the same header. Both now read navViewport.
    const auto l = AestraUI::resolveFileBrowserLayout(420, 720, kSearch, 34, 160, kHeader, 0);
    check(same(l.navViewport, 0, kSearch + kHeader, 160, 720 - kSearch - kHeader), "rows below the folder header");
    check(same(l.navHeader, 0, kSearch, 160, kHeader), "the folder header is the band above them");
    check(near(l.navHeader.bottom(), l.navViewport.y) && near(l.navViewport.bottom(), l.navPane.bottom()),
          "header and rows tile the pane exactly");
}

void testSortLabelThreshold() {
    // The sort control names its key from a 300 px list exactly, not from 301.
    const auto at300 = AestraUI::resolveFileBrowserLayout(460, 600, kSearch, 34, 160, kHeader, 0);
    const auto at299 = AestraUI::resolveFileBrowserLayout(459, 600, kSearch, 34, 160, kHeader, 0);
    check(near(at300.sort.width, 62.0f), "a 300 px list shows the named sort control");
    check(near(at299.sort.width, 22.0f), "a 299 px list keeps the bare glyph");
}

} // namespace

int main() {
    testMatchesTheHandWrittenLayout();
    testOneRectForTheFolderRows();
    testSortLabelThreshold();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "FileBrowserLayoutTest: all checks passed\n";
    return 0;
}
