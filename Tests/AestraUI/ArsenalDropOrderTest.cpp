// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A sample dragged onto an open Arsenal lands in Arsenal, not on the timeline under it.
//
// The bug (owner, 4 Nov 2026, Angel Dust session): dragging a sample from the browser onto Arsenal
// stopped working; the drop went to the timeline behind the panel and was rejected ("Invalid track
// position"). The drop manager checks the most recently registered target first, and the timeline
// registers itself on its first frame — after Arsenal when a project opens with Arsenal in front.
// Arsenal now claims the front of the list once per drag, as the mixer does (#395).
//
// Stand-in for the timeline: a full-window drop target registered after the panel.
//   A. The pointer moves over Arsenal after the claim: the sample loads into Arsenal.
//   B. The pointer was already over Arsenal (cached target: the timeline) when the claim happens, and
//      the release comes with no further move: it still loads into Arsenal.

#include "ArsenalPanel.h"
#include "NUIDragDrop.h"

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

class TimelineStandIn : public NUIComponent, public IDropTarget {
public:
    bool dropped = false;
    DropFeedback onDragEnter(const DragData&, const NUIPoint&) override { return DropFeedback::Copy; }
    DropFeedback onDragOver(const DragData&, const NUIPoint&) override { return DropFeedback::Copy; }
    void onDragLeave() override {}
    DropResult onDrop(const DragData&, const NUIPoint&) override { dropped = true; return DropResult{}; }
    NUIRect getDropBounds() const override { return getBounds(); }
};

struct Rig {
    std::shared_ptr<Aestra::Audio::TrackManager> tm = std::make_shared<Aestra::Audio::TrackManager>();
    std::shared_ptr<Aestra::Audio::ArsenalPanel> arsenal = std::make_shared<Aestra::Audio::ArsenalPanel>(tm);
    std::shared_ptr<TimelineStandIn> timeline = std::make_shared<TimelineStandIn>();
    std::string loadedPath;
    DragData data;

    Rig() {
        arsenal->setBounds({260.0f, 90.0f, 1100.0f, 670.0f});
        arsenal->setVisible(true);
        arsenal->refreshUnits();
        arsenal->setOnSampleDroppedToUnit([this](Aestra::Audio::UnitID, const std::string& path) { loadedPath = path; });
        arsenal->registerDropTargets(true); // the panel opens with the project
        timeline->setBounds({0.0f, 0.0f, 1366.0f, 768.0f});
        NUIDragDropManager::getInstance().registerDropTarget(timeline); // the timeline's first frame comes later
        data.type = DragDataType::File;
        data.filePath = "/tmp/kick - ok.wav";
        data.displayName = "kick - ok.wav";
    }
    ~Rig() {
        auto& dnd = NUIDragDropManager::getInstance();
        if (dnd.isDragging()) dnd.cancelDrag();
        dnd.unregisterDropTarget(timeline.get());
        arsenal->unregisterDropTargets();
    }
};

void testMoveAfterClaim() {
    Rig rig;
    auto& dnd = NUIDragDropManager::getInstance();
    const NUIPoint start{150.0f, 464.0f};
    dnd.beginDrag(rig.data, start, nullptr);
    dnd.updateDrag({start.x + 40.0f, start.y}); // past the threshold: the drag starts
    rig.arsenal->onUpdate(0.016);                // a frame while the drag is in flight
    dnd.updateDrag({800.0f, 600.0f});            // over Arsenal (and the timeline behind it)
    check(dnd.isDragging(), "A: the drag is in flight");
    dnd.endDrag({800.0f, 600.0f});
    check(!rig.timeline->dropped, "A: the sample does not land on the timeline behind Arsenal");
    check(rig.loadedPath == rig.data.filePath, "A: it loads into Arsenal");
}

void testReleaseWithoutMoveAfterClaim() {
    Rig rig;
    auto& dnd = NUIDragDropManager::getInstance();
    const NUIPoint over{800.0f, 600.0f};
    dnd.beginDrag(rig.data, {150.0f, 464.0f}, nullptr);
    dnd.updateDrag(over);         // already over Arsenal: the cached target is the timeline
    rig.arsenal->onUpdate(0.016); // the claim happens now
    dnd.endDrag(over);            // released with no move in between
    check(!rig.timeline->dropped, "B: a release with no move after the claim does not land on the timeline");
    check(rig.loadedPath == rig.data.filePath, "B: it loads into Arsenal");
}

} // namespace

int main() {
    std::cout << "ArsenalDropOrderTest\n";
    testMoveAfterClaim();
    testReleaseWithoutMoveAfterClaim();
    return g_failures == 0 ? 0 : 1;
}
