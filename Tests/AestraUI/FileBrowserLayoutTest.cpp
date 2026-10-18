// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The file browser's bands (FileBrowserLayout.h, FD-47): tabs, search, location,
// chips, list header, rows, preview dock; and the one rect the Places overlay is
// drawn, clipped and hit-tested by (navViewport). Header-only.

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

// FD-47: one column of bands; Places is an overlay under the location band.
void testBandsStackTopToBottom() {
    for (float w : {248.0f, 288.0f, 420.0f}) {
        for (bool preview : {false, true}) {
            for (bool chips : {false, true}) {
                AestraUI::FileBrowserBands b;
                b.chipsRow = chips ? 30.0f : 0.0f;
                b.preview = preview ? 112.0f : 0.0f;
                const auto l = AestraUI::resolveFileBrowserLayout(w, 700.0f, b, 92.0f);
                const std::string at = " at " + std::to_string(int(w)) + (preview ? " +preview" : "") + (chips ? " +chips" : "");
                check(same(l.kindRow, 0, 0, w, 40), "kind tabs on top" + at);
                check(same(l.searchBar, 0, 40, w, 36), "search under the tabs" + at);
                check(same(l.location, 0, 76, w, 30), "location under search" + at);
                const float chipsH = chips ? 30.0f : 0.0f;
                check(same(l.chips, 0, 106, w, chipsH), "chips under location" + at);
                check(same(l.listHeader, 0, 106 + chipsH, w, 26), "header under chips" + at);
                const float listY = 106 + chipsH + 26;
                check(same(l.list, 0, listY, w, 700 - listY - (preview ? 112 : 0)), "rows fill to the dock" + at);
                check(near(l.kinds[0].x, 12) && near(l.kinds[2].right(), w - 12) &&
                          near(l.kinds[0].width, l.kinds[1].width) && near(l.kinds[1].width, l.kinds[2].width),
                      "three equal tabs inside the segment" + at);
                check(near(l.search.right(), w - 92), "the field leaves room for the scope chip" + at);
                check(near(l.body.y, 76) && near(l.body.bottom(), 700), "embedded views fill under search" + at);
            }
        }
    }
}

void testPlacesOverlay() {
    const auto l = AestraUI::resolveFileBrowserLayout(288, 700, AestraUI::FileBrowserBands{}, 92);
    check(same(l.navPane, 0, l.location.bottom(), 288, 700 - l.location.bottom()), "Places drops from under the location band");
    check(near(l.navHeader.bottom(), l.navViewport.y) && near(l.navViewport.bottom(), l.navPane.bottom()),
          "its title band and rows tile it exactly");
}

void testEmbeddedViewsDropTheFolderBands() {
    AestraUI::FileBrowserBands b;
    b.locationRow = 0.0f;
    b.listHeader = 0.0f;
    const auto l = AestraUI::resolveFileBrowserLayout(288, 700, b, 10);
    check(l.location.height == 0.0f && l.listHeader.height == 0.0f && l.back.width == 0.0f,
          "Patterns and Plugins have no location or list header");
}

void testSortLabelThreshold() {
    const auto at240 = AestraUI::resolveFileBrowserLayout(240, 600, AestraUI::FileBrowserBands{}, 92);
    const auto at239 = AestraUI::resolveFileBrowserLayout(239, 600, AestraUI::FileBrowserBands{}, 92);
    check(near(at240.sort.width, 62.0f), "a 240 px browser names its sort key");
    check(near(at239.sort.width, 22.0f), "a narrower one keeps the bare glyph");
}

} // namespace

int main() {
    testBandsStackTopToBottom();
    testPlacesOverlay();
    testEmbeddedViewsDropTheFolderBands();
    testSortLabelThreshold();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "FileBrowserLayoutTest: all checks passed\n";
    return 0;
}
