// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V81-M1, FD-28 point 7: the navigator for sessions that overflow the mixer. One tick per channel in the
// strips' proportion, a bracket for what is on screen (drag it), click a tick to centre that channel, click a
// group label to jump to the group, flags for clipped and soloed. Pure widget: positions in strip-row
// coordinates, the owner supplies the scroll.

#include "UIMixerNavigator.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace AestraUI;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

NUIMouseEvent down(const NUIPoint& p) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Down;
    e.position = p;
    e.button = NUIMouseButton::Left;
    e.pressed = true;
    return e;
}
NUIMouseEvent up(const NUIPoint& p) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Up;
    e.position = p;
    e.button = NUIMouseButton::Left;
    e.released = true;
    return e;
}
NUIMouseEvent move(const NUIPoint& p) {
    NUIMouseEvent e;
    e.type = NUIMouseEventType::Move;
    e.position = p;
    return e;
}
NUIPoint centre(const NUIRect& r) { return {r.x + r.width * 0.5f, r.y + r.height * 0.5f}; }

} // namespace

int main() {
    // 40 strips of 84 px with 6 px spacing, two groups with a 10 px gap between them: a 3 600 px row.
    std::vector<UIMixerNavigator::Tick> ticks;
    float x = 0.0f;
    for (uint32_t i = 0; i < 40; ++i) {
        if (i == 20) x += 10.0f;
        UIMixerNavigator::Tick t;
        t.channelId = 100 + i;
        t.x = x;
        t.w = 84.0f;
        ticks.push_back(t);
        x += 90.0f;
    }
    const float contentW = x - 6.0f;
    std::vector<UIMixerNavigator::Group> groups{{"Drums", 0.0f, 20 * 90.0f - 6.0f}, {"Master", 20 * 90.0f + 10.0f, 20 * 90.0f - 6.0f}};

    UIMixerNavigator nav;
    nav.setBounds({100, 50, 1000, UIMixerNavigator::heightFor(true)});
    nav.setContent(ticks, groups, contentW);
    nav.setViewport(0.0f, 1300.0f);

    float requested = -1.0f;
    uint32_t clickedId = 0;
    nav.onScrollRequested = [&](float s) { requested = s; };
    nav.onTickClicked = [&](uint32_t id) { clickedId = id; };

    std::cout << "[1] ticks keep the strips' order and proportion\n";
    {
        bool ordered = true;
        for (size_t i = 1; i < ticks.size(); ++i)
            ordered = ordered && nav.tickRect(i).x > nav.tickRect(i - 1).x;
        check(ordered, "ticks run left to right in strip order");
        check(nav.tickRect(0).x >= 100.0f - 0.01f && nav.tickRect(39).right() <= 1100.5f, "and fill the navigator's width");
        const float within = nav.tickRect(1).x - nav.tickRect(0).x;
        const float across = nav.tickRect(20).x - nav.tickRect(19).x;
        check(across > within, "the gap between groups shows");
        check(nav.tickRect(5).width >= 2.0f, "a tick is never thinner than 2 px");
    }

    std::cout << "[2] the bracket is what is on screen\n";
    {
        const auto br = nav.bracketRect();
        check(std::fabs(br.width - (1300.0f / contentW * 1000.0f + 2.0f)) < 2.0f, "its width is the visible share of the row");
        nav.setViewport(1000.0f, 1300.0f);
        check(nav.bracketRect().x > br.x, "it moves right as the row scrolls");
        nav.setViewport(0.0f, 1300.0f);
    }

    std::cout << "[3] clicking a tick centres that channel\n";
    {
        requested = -1.0f;
        nav.onMouseEvent(down(centre(nav.tickRect(30))));
        check(clickedId == 130, "the clicked channel is reported");
        const float expected = ticks[30].x + 42.0f - 1300.0f * 0.5f;
        check(std::fabs(requested - expected) < 0.01f, "and the scroll that centres it is asked for");
        nav.onMouseEvent(up(centre(nav.tickRect(30))));
    }

    std::cout << "[4] dragging the bracket scrolls\n";
    {
        nav.setViewport(0.0f, 1300.0f);
        const auto br = nav.bracketRect();
        requested = -1.0f;
        nav.onMouseEvent(down(centre(br)));
        nav.onMouseEvent(move({centre(br).x + 100.0f, centre(br).y}));
        const float expected = 100.0f / 1000.0f * contentW;
        check(std::fabs(requested - expected) < 1.0f, "100 px of drag is 10% of the row");
        nav.onMouseEvent(move({centre(br).x - 50.0f, centre(br).y}));
        check(requested < 0.0f + 1.0f, "dragging back left asks for less (the owner clamps at 0)");
        nav.onMouseEvent(up(centre(br)));
        requested = -1.0f;
        nav.onMouseEvent(move({centre(br).x + 300.0f, centre(br).y}));
        check(requested < 0.0f, "after release a move scrolls nothing");
    }

    std::cout << "[5] a group label jumps to the group\n";
    {
        requested = -1.0f;
        nav.setViewport(0.0f, 1300.0f);
        nav.onMouseEvent(down(centre(nav.groupLabelRect(1))));
        check(std::fabs(requested - groups[1].x) < 0.01f, "the Master label scrolls to the Master group");
        nav.onMouseEvent(up(centre(nav.groupLabelRect(1))));
    }

    std::cout << "[6] the navigator needs height for labels only when there are groups\n";
    {
        check(UIMixerNavigator::heightFor(true) > UIMixerNavigator::heightFor(false), "a label row is added for groups");
        UIMixerNavigator flat;
        flat.setBounds({0, 0, 500, UIMixerNavigator::heightFor(false)});
        flat.setContent(ticks, {}, contentW);
        check(flat.tickRect(0).y < 4.0f, "without groups the ticks start at the top");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
