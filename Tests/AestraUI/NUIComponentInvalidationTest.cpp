// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// SPEC-028 R1: how a widget feeds the frame's invalidation region. repaint() invalidates the widget's bounds (plus its
// effect margin), repaint(rect) a region inside it, moving or resizing invalidates the old and the new position, and
// the existing setDirty() callers get the same bounds-wide behaviour for free.

#include "Core/NUIComponent.h"
#include "Core/NUIDirtyRegion.h"
#include "Core/NUIPerfProbe.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

using namespace AestraUI;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
bool covers(const NUIDirtyRegion& region, const NUIRect& r) {
    for (const auto& c : region.rects())
        if (c.x <= r.x + 1e-3f && c.y <= r.y + 1e-3f && c.right() >= r.right() - 1e-3f && c.bottom() >= r.bottom() - 1e-3f)
            return true;
    return false;
}
NUIDirtyRegion& region() { return NUIComponent::frameDirtyRegion(); }

} // namespace

int main() {
    auto root = std::make_shared<NUIComponent>();
    root->setBounds(NUIRect(0, 0, 1366, 768));
    auto widget = std::make_shared<NUIComponent>();
    root->addChild(widget);
    widget->setBounds(NUIRect(200, 100, 80, 30));
    region().clear();

    std::cout << "[1] repaint() invalidates the widget's bounds, and only those\n";
    {
        widget->repaint();
        check(region().rects().size() == 1 && covers(region(), NUIRect(200, 100, 80, 30)), "the widget's bounds are invalidated");
        check(region().area() < 80.0f * 30.0f * 1.5f, "and not the root's (the whole window is not invalidated)");
        check(root->isDirty(), "the existing dirty bit still propagates to the root (the renderer still reads it)");
    }

    std::cout << "[2] repaint(rect) invalidates a region inside the widget\n";
    {
        region().clear();
        widget->repaint(NUIRect(10, 5, 20, 10)); // widget-local
        check(region().rects().size() == 1 && covers(region(), NUIRect(210, 105, 20, 10)),
              "a widget-local rectangle becomes the window-space rectangle");
        check(region().area() <= 20.0f * 10.0f + 1.0f, "and the rest of the widget is not invalidated");
    }

    std::cout << "[3] an effect margin widens the invalidation\n";
    {
        region().clear();
        widget->setInvalidationMargin(6.0f); // a shadow
        widget->repaint();
        check(covers(region(), NUIRect(194, 94, 92, 42)), "the shadow around the bounds is invalidated too");
        widget->setInvalidationMargin(0.0f);
    }

    std::cout << "[4] moving or resizing invalidates where it was and where it is\n";
    {
        region().clear();
        widget->setBounds(NUIRect(500, 100, 80, 30));
        check(covers(region(), NUIRect(200, 100, 80, 30)), "the old position is invalidated (no stale pixels left behind)");
        check(covers(region(), NUIRect(500, 100, 80, 30)), "and the new one");
        region().clear();
        widget->setBounds(NUIRect(500, 100, 80, 30)); // unchanged
        check(region().empty(), "setting the same bounds invalidates nothing");
    }

    std::cout << "[5] plain setDirty() callers get the bounds-wide behaviour\n";
    {
        region().clear();
        widget->setDirty(true);
        check(covers(region(), NUIRect(500, 100, 80, 30)), "setDirty() on a widget invalidates its bounds");
    }

    std::cout << "[6] a clean frame has an empty region\n";
    {
        region().clear();
        check(region().empty(), "after the frame consumes the region, nothing is pending");
    }

    std::cout << "[7] the frame-stats report carries the dirty area (SPEC-028 R0)\n";
    {
        PerfProbe::takeReport(1); // drain anything earlier
        PerfProbe::recordDirtyArea(0.004f, 1); // a transport number
        PerfProbe::recordDirtyArea(0.012f, 3);
        const std::string report = PerfProbe::takeReport(2);
        check(report.find("[DirtyArea] frames=2 avg=0.80% max=1.20% rects avg=2.0") != std::string::npos,
              "the report names the average and worst share of the window and the rectangle count: " + report);
        check(PerfProbe::takeReport(1).find("[DirtyArea]") == std::string::npos, "and the counters reset each window");
    }

    if (g_failures == 0) {
        std::cout << "NUIComponentInvalidationTest: all checks passed\n";
        return 0;
    }
    std::cout << "NUIComponentInvalidationTest: " << g_failures << " failure(s)\n";
    return 1;
}
