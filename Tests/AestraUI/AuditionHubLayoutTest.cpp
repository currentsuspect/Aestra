// Audition hub geometry: the three regions, the nav items, and shelves of tiles that wrap with the width.
#include "../../Source/Panels/AuditionHubLayout.h"

#include <cstdlib>
#include <iostream>

using Aestra::AuditionHubLayout;
using AestraUI::NUIRect;

namespace {
void check(bool ok, const char* what) {
    if (!ok) { std::cerr << "[FAIL] " << what << "\n"; std::exit(1); }
}
}

int main() {
    const NUIRect bounds{100.0f, 50.0f, 1400.0f, 800.0f};
    const auto f = AuditionHubLayout::frame(bounds);
    check(f.nav.right() == f.content.x && f.player.y == f.content.bottom(), "nav, content and player tile the hub without gaps");
    check(f.player.height == AuditionHubLayout::kPlayerH && f.content.right() == bounds.right(), "the player bar runs the full width under the content");
    for (int i = 1; i < AuditionHubLayout::kPages; ++i)
        check(f.navItems[i].y > f.navItems[i - 1].y && !f.navItems[i].contains(f.navItems[i - 1].x + 1.0f, f.navItems[i - 1].y + 1.0f), "nav items stack without overlapping");
    check(f.toggleA.right() <= f.toggleB.x && f.toggleB.right() <= f.listenOn.x && f.listenOn.right() <= f.loudness.x, "the player's right-hand controls sit in a row, left to right");
    check(f.transportPrev.right() <= f.transportPlay.x && f.transportPlay.right() <= f.transportNext.x, "prev, play and next are in order");

    check(AuditionHubLayout::columns(1168.0f) == 6 && AuditionHubLayout::columns(700.0f) >= 3, "a wide page fits six tiles, a narrow one fewer");
    const NUIRect page{0.0f, 0.0f, 1168.0f, 700.0f};
    const auto shelves = AuditionHubLayout::shelves(page, 100.0f, {7, 0, 3});
    check(shelves.size() == 3 && shelves[0].tiles.size() == 7, "one shelf per count");
    check(shelves[0].tiles[6].y > shelves[0].tiles[0].y && shelves[0].tiles[6].x == shelves[0].tiles[0].x, "the seventh tile wraps to a second row at the left");
    check(shelves[1].tiles.empty() && shelves[1].header.y > shelves[0].tiles[6].y, "an empty shelf keeps its header, below the one before");
    check(shelves[2].header.y > shelves[1].header.y, "shelves stack in order");
    for (size_t i = 1; i < shelves[0].tiles.size(); ++i)
        if (shelves[0].tiles[i].y == shelves[0].tiles[i - 1].y) check(shelves[0].tiles[i].x >= shelves[0].tiles[i - 1].right() + AuditionHubLayout::kGap - 0.01f, "tiles in a row keep their gap");
    check(AuditionHubLayout::contentHeight(shelves, 100.0f) > 300.0f, "the page is as tall as its shelves");
    std::cout << "[PASS] AuditionHubLayoutTest\n";
    return 0;
}
