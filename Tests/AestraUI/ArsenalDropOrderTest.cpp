// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A sample dragged onto an open Arsenal lands in Arsenal, not on the timeline under it.
//
// The bug (owner, 4 Nov 2026, Angel Dust session): dragging a sample from the browser onto Arsenal
// stopped working; the drop went to the timeline behind the panel and was rejected ("Invalid track
// position"). The drop manager checked the most recently registered target first, and the timeline
// registers itself on its first frame — after Arsenal when a project opens with Arsenal in front.
// The drop manager now picks the target on top on screen (#1328), so nothing re-registers per drag.
//
// The tree mirrors AestraContent: a workspace layer with a full-window timeline stand-in, then the
// pass-through overlay layer holding Arsenal. The timeline registers after the panel.
//   A. The pointer moves over Arsenal: the sample loads into Arsenal.
//   B. The pointer is over the timeline when Arsenal opens under it, and the release comes with no
//      further move: it still loads into Arsenal.

#include "ArsenalPanel.h"
#include "NUIDragDrop.h"
#include "UnitRow.h"

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
    std::shared_ptr<NUIComponent> root = std::make_shared<NUIComponent>();
    std::shared_ptr<NUIComponent> workspace = std::make_shared<NUIComponent>();
    std::shared_ptr<NUIComponent> overlay = std::make_shared<NUIComponent>();
    std::shared_ptr<Aestra::Audio::TrackManager> tm = std::make_shared<Aestra::Audio::TrackManager>();
    std::shared_ptr<Aestra::Audio::ArsenalPanel> arsenal = std::make_shared<Aestra::Audio::ArsenalPanel>(tm);
    std::shared_ptr<TimelineStandIn> timeline = std::make_shared<TimelineStandIn>();
    std::string loadedPath;
    DragData data;

    Rig() {
        for (auto& layer : {root, workspace, overlay}) layer->setBounds({0.0f, 0.0f, 1366.0f, 768.0f});
        overlay->setDropPassThrough(true);
        root->addChild(workspace);
        root->addChild(overlay);
        workspace->addChild(timeline);
        overlay->addChild(arsenal);
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

void testMoveOverArsenal() {
    Rig rig;
    auto& dnd = NUIDragDropManager::getInstance();
    const NUIPoint start{150.0f, 464.0f};
    dnd.beginDrag(rig.data, start, nullptr);
    dnd.updateDrag({start.x + 40.0f, start.y}); // past the threshold: the drag starts (over the timeline)
    rig.arsenal->onUpdate(0.016);                // a frame while the drag is in flight
    dnd.updateDrag({800.0f, 600.0f});            // over Arsenal (and the timeline behind it)
    check(dnd.isDragging(), "A: the drag is in flight");
    dnd.endDrag({800.0f, 600.0f});
    check(!rig.timeline->dropped, "A: the sample does not land on the timeline behind Arsenal");
    check(rig.loadedPath == rig.data.filePath, "A: it loads into Arsenal");
}

void testArsenalOpensUnderStillPointer() {
    Rig rig;
    auto& dnd = NUIDragDropManager::getInstance();
    const NUIPoint over{800.0f, 600.0f};
    rig.arsenal->setVisible(false);
    dnd.beginDrag(rig.data, {150.0f, 464.0f}, nullptr);
    dnd.updateDrag(over);           // Arsenal is closed: the cached target is the timeline
    rig.arsenal->setVisible(true);  // it opens under the pointer
    rig.arsenal->onUpdate(0.016);
    dnd.endDrag(over);              // released with no move in between
    check(!rig.timeline->dropped, "B: a release with no move after Arsenal opens does not land on the timeline");
    check(rig.loadedPath == rig.data.filePath, "B: it loads into Arsenal");
}

// A plugin dropped on Arsenal is only accepted once the load worked; a failed load refuses the drop and says why.
void testPluginDropResult() {
    Rig rig;
    DragData plugin;
    plugin.type = DragDataType::Plugin;
    plugin.sourceClipIdString = "surge-xt";
    plugin.displayName = "Surge XT";

    int calls = 0;
    PluginLoadResult next{false, "Couldn't load Surge XT: plugin helper missing"};
    const auto loader = [&](const std::string& id) {
        ++calls;
        return id == "surge-xt" ? next : PluginLoadResult{};
    };
    rig.arsenal->setOnPluginDropped(loader);
    rig.arsenal->setOnPluginDroppedToUnit([&](Aestra::Audio::UnitID, const std::string& id) { return loader(id); });

    const auto refused = rig.arsenal->onDrop(plugin, {800.0f, 600.0f});
    check(calls == 1, "C: the loader ran");
    check(!refused.accepted, "C: a failed load refuses the drop");
    check(refused.message == "Couldn't load Surge XT: plugin helper missing", "C: the refusal carries the message");

    next = PluginLoadResult{};
    const auto accepted = rig.arsenal->onDrop(plugin, {800.0f, 600.0f});
    check(accepted.accepted, "D: a good load accepts the drop");
}

} // namespace

int main() {
    std::cout << "ArsenalDropOrderTest\n";
    testMoveOverArsenal();
    testArsenalOpensUnderStillPointer();
    testPluginDropResult();
    return g_failures == 0 ? 0 : 1;
}
