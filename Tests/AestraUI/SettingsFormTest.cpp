// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V82-P12: every Settings page lays out through SettingsForm (canvas-2 board 2). The form must line the controls
// up in one column, keep rows a fixed height, and — because a page can be taller than the dialog — scroll, hiding
// the controls it scrolls away so they cannot take clicks at the dialog's edge.

#include "Settings/SettingsForm.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Aestra;
using AestraUI::NUIComponent;
using AestraUI::NUIRect;

namespace {

std::shared_ptr<NUIComponent> control() {
    return std::make_shared<NUIComponent>();
}

bool near(float a, float b) {
    return std::fabs(a - b) < 0.01f;
}

void testRowsLineUp() {
    SettingsForm form;
    auto a = control(), b = control(), toggle = control();
    form.section("DEVICE");
    form.row("Driver", a);
    form.row("Output", b);
    form.row("Master limiter", toggle, "", 40.0f);
    form.layout(NUIRect(100.0f, 50.0f, 700.0f, 500.0f));

    const float column = 100.0f + SettingsForm::kSidePadding + SettingsForm::kLabelWidth;
    assert(near(a->getBounds().x, column) && near(b->getBounds().x, column) && near(toggle->getBounds().x, column));
    assert(near(a->getBounds().width, SettingsForm::kControlWidth) && near(toggle->getBounds().width, 40.0f));
    assert(near(b->getBounds().y - a->getBounds().y, SettingsForm::kRowHeight) && "rows are one height");
    assert(near(a->getBounds().height, SettingsForm::kControlHeight));
    assert(a->isVisible() && b->isVisible() && toggle->isVisible());
    std::printf("[PASS] controls share one column at label width %.0f, rows %.0f px apart\n", SettingsForm::kLabelWidth,
                SettingsForm::kRowHeight);
}

void testTallPageScrollsAndHides() {
    SettingsForm form;
    std::vector<std::shared_ptr<NUIComponent>> controls;
    form.section("MANY");
    for (int i = 0; i < 30; ++i) {
        controls.push_back(control());
        form.row("Row " + std::to_string(i), controls.back());
    }
    const NUIRect area(0.0f, 0.0f, 600.0f, 300.0f);
    form.layout(area);
    assert(form.contentHeight() > area.height);
    assert(controls.front()->isVisible());
    assert(!controls.back()->isVisible() && "a row below the area must not take clicks");

    AestraUI::NUIMouseEvent wheel;
    wheel.position = {300.0f, 150.0f};
    wheel.wheelDelta = -100.0f; // far past the end: clamps
    assert(form.onMouseWheel(wheel));
    assert(controls.back()->isVisible() && "scrolled to the bottom, the last row shows");
    assert(!controls.front()->isVisible() && "and the first one is scrolled away");
    assert(controls.back()->getBounds().bottom() <= area.bottom() + 0.01f && "never scrolls past the end");

    wheel.position = {900.0f, 150.0f}; // outside the area
    wheel.wheelDelta = 100.0f;
    assert(!form.onMouseWheel(wheel) && "the wheel outside the page does nothing");
    std::printf("[PASS] a tall page scrolls within its area and hides the rows it scrolls away\n");
}

void testShortPageDoesNotScroll() {
    SettingsForm form;
    auto a = control();
    form.row("Colours", a);
    form.layout(NUIRect(0.0f, 0.0f, 600.0f, 400.0f));
    AestraUI::NUIMouseEvent wheel;
    wheel.position = {10.0f, 10.0f};
    wheel.wheelDelta = -1.0f;
    const float y = a->getBounds().y;
    assert(!form.onMouseWheel(wheel) && near(a->getBounds().y, y));
    std::printf("[PASS] a page that fits does not scroll\n");
}

void testBlocksReportTheirRect() {
    SettingsForm form;
    form.section("THIS COPY");
    const size_t status = form.block(nullptr, 64.0f);
    auto check = control();
    form.row("Check for updates", check, "", 40.0f);
    form.layout(NUIRect(0.0f, 0.0f, 600.0f, 400.0f));
    const NUIRect r = form.boundsOf(status);
    assert(near(r.height, 64.0f) && near(r.x, SettingsForm::kSidePadding));
    assert(near(check->getBounds().y, r.bottom() + (SettingsForm::kRowHeight - SettingsForm::kControlHeight) * 0.5f));
    std::printf("[PASS] a block the page draws itself reports its rect and pushes the rows below it\n");
}

} // namespace

int main() {
    testRowsLineUp();
    testTallPageScrollsAndHides();
    testShortPageDoesNotScroll();
    testBlocksReportTheirRect();
    return 0;
}
