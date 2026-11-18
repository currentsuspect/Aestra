// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Every button belongs to a group and the group sets its motion (NUIButtonMotion.h): colour pop everywhere, a dip
// and one bounce on pressable groups, none on rows. Header-only, links nothing.

#include "../../AestraUI/Core/NUIButtonMotion.h"

#include <algorithm>
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
constexpr float kDt = 1.0f / 60.0f;

// Hold for `held` frames, release, run `after` frames. Returns {lowest scale, highest scale after release}.
struct Run { float low = 1.0f, high = 1.0f, end = 1.0f; };
Run pressRun(AestraUI::ButtonGroup g, int held = 20, int after = 90) {
    AestraUI::ButtonMotion m(g);
    Run r;
    for (int i = 0; i < held; ++i) { m.update(kDt, true, true); r.low = std::min(r.low, m.scale()); }
    for (int i = 0; i < after; ++i) { m.update(kDt, true, false); r.high = std::max(r.high, m.scale()); }
    r.end = m.scale();
    return r;
}
} // namespace

int main() {
    using namespace AestraUI;

    // Colour pop: the same shape for every group, at the group's own speed.
    for (auto g : {ButtonGroup::Action, ButtonGroup::Icon, ButtonGroup::Toggle, ButtonGroup::Row}) {
        ButtonMotion m(g);
        check(m.hover() == 0.0f, "rests unhovered");
        for (int i = 0; i < 30; ++i) m.update(kDt, true, false);
        check(m.hover() == 1.0f, "hover reaches full");
        for (int i = 0; i < 30; ++i) m.update(kDt, false, false);
        check(m.hover() == 0.0f, "and returns to rest");
    }
    check(buttonFeel(ButtonGroup::Row).hoverSeconds < buttonFeel(ButtonGroup::Action).hoverSeconds,
          "rows pop faster than pills");

    // Pressable groups dip while held and bounce once past 1 before settling.
    for (auto g : {ButtonGroup::Action, ButtonGroup::Icon, ButtonGroup::Toggle}) {
        const Run r = pressRun(g);
        const ButtonFeel feel = buttonFeel(g);
        check(r.low < 1.0f && r.low >= feel.pressScale - 0.03f, "dips to about the group's press scale while held");
        check(r.high > 1.0f && r.high < 1.12f, "springs a little past 1 on release (a small bounce)");
        check(std::fabs(r.end - 1.0f) < 0.001f, "and settles at exactly 1");
    }
    check(pressRun(ButtonGroup::Icon).low < pressRun(ButtonGroup::Action).low, "icons dip more than pills");

    // Rows never move.
    const Run row = pressRun(ButtonGroup::Row);
    check(row.low == 1.0f && row.high == 1.0f && row.end == 1.0f, "rows do not scale");

    // A latch flipping kicks the scale; it settles.
    ButtonMotion t(ButtonGroup::Toggle);
    t.kick();
    float peak = 1.0f;
    for (int i = 0; i < 90; ++i) { t.update(kDt, false, false); peak = std::max(peak, t.scale()); }
    check(peak > 1.0f && std::fabs(t.scale() - 1.0f) < 0.001f && !t.animating(), "a latch bounces once and settles");

    // A stalled frame does not throw the spring.
    ButtonMotion s(ButtonGroup::Action);
    s.update(kDt, true, true);
    s.update(5.0f, true, true);
    check(s.scale() > 0.8f && s.scale() < 1.1f, "a long frame is clamped");

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ButtonMotionTest: all checks passed\n";
    return 0;
}
