// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A drop goes to the target on top on screen, not to the one that registered last (#1328).
//
// The bug: NUIDragDropManager::findTargetAt walked targets in reverse registration order. The timeline
// registers on its first frame and stays visible under floating panels, so whenever it registered after
// a panel it took that panel's drops: rejected below its rows, or a clip silently added under the panel
// (mixer #395, Arsenal 4 Nov 2026). Both were patched by the panel re-registering once per drag.
//
// The tree mirrors AestraContent: Root { Workspace { Timeline }, Overlay (pass-through) { panels } }.
// Every target here registers before the timeline, and nothing re-registers during the drag.

#include "NUIComponent.h"
#include "NUIDragDrop.h"
#include "NUIContextMenu.h"

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

class Target : public NUIComponent, public IDropTarget {
public:
    explicit Target(std::string name) : name_(std::move(name)) { setId(name_); }
    int drops = 0;
    DropFeedback onDragEnter(const DragData&, const NUIPoint&) override { return DropFeedback::Copy; }
    DropFeedback onDragOver(const DragData&, const NUIPoint&) override { return DropFeedback::Copy; }
    void onDragLeave() override {}
    DropResult onDrop(const DragData&, const NUIPoint&) override {
        ++drops;
        DropResult r;
        r.accepted = true;
        return r;
    }
    NUIRect getDropBounds() const override { return getBounds(); }

private:
    std::string name_;
};

// A drop target that is not a component (no place in the tree): only registration order can rank it.
class LooseTarget : public IDropTarget {
public:
    NUIRect bounds;
    int drops = 0;
    DropFeedback onDragEnter(const DragData&, const NUIPoint&) override { return DropFeedback::Copy; }
    DropFeedback onDragOver(const DragData&, const NUIPoint&) override { return DropFeedback::Copy; }
    void onDragLeave() override {}
    DropResult onDrop(const DragData&, const NUIPoint&) override { ++drops; return DropResult{}; }
    NUIRect getDropBounds() const override { return bounds; }
};

struct Rig {
    std::shared_ptr<NUIComponent> root = std::make_shared<NUIComponent>();
    std::shared_ptr<NUIComponent> workspace = std::make_shared<NUIComponent>();
    std::shared_ptr<NUIComponent> overlay = std::make_shared<NUIComponent>();
    std::shared_ptr<Target> timeline = std::make_shared<Target>("timeline");
    std::shared_ptr<Target> arsenal = std::make_shared<Target>("arsenal");  // floating panel
    std::shared_ptr<Target> row = std::make_shared<Target>("arsenal-row");  // target inside it
    std::shared_ptr<NUIComponent> rowLabel = std::make_shared<NUIComponent>(); // non-target child of the row
    std::shared_ptr<Target> mixer = std::make_shared<Target>("mixer");      // overlaps Arsenal
    std::shared_ptr<NUIComponent> pianoRoll = std::make_shared<NUIComponent>(); // covers, takes no drops
    DragData data;

    Rig() {
        root->setBounds({0, 0, 1366, 768});
        workspace->setBounds({0, 0, 1366, 768});
        overlay->setBounds({0, 0, 1366, 768});
        overlay->setDropPassThrough(true);
        root->addChild(workspace);
        root->addChild(overlay);

        timeline->setBounds({0, 60, 1366, 708});
        workspace->addChild(timeline);

        arsenal->setBounds({260, 90, 600, 500});
        row->setBounds({270, 130, 580, 60});
        rowLabel->setBounds({280, 140, 200, 40});
        row->addChild(rowLabel);
        arsenal->addChild(row);
        overlay->addChild(arsenal);

        mixer->setBounds({700, 400, 600, 300});
        mixer->setVisible(false);
        overlay->addChild(mixer);

        pianoRoll->setBounds({0, 600, 600, 168});
        pianoRoll->setVisible(false);
        overlay->addChild(pianoRoll);

        // The wrong order on purpose: the row before its panel, every panel before the timeline.
        auto& dnd = NUIDragDropManager::getInstance();
        dnd.registerDropTarget(row);
        dnd.registerDropTarget(arsenal);
        dnd.registerDropTarget(mixer);
        dnd.registerDropTarget(timeline);

        data.type = DragDataType::File;
        data.filePath = "/tmp/kick.wav";
        data.displayName = "kick.wav";
    }
    ~Rig() {
        auto& dnd = NUIDragDropManager::getInstance();
        if (dnd.isDragging()) dnd.cancelDrag();
        dnd.unregisterDropTarget(row.get());
        dnd.unregisterDropTarget(arsenal.get());
        dnd.unregisterDropTarget(mixer.get());
        dnd.unregisterDropTarget(timeline.get());
    }

    void dragTo(const NUIPoint& at) {
        auto& dnd = NUIDragDropManager::getInstance();
        dnd.beginDrag(data, {10, 10}, nullptr);
        dnd.updateDrag({60, 10}); // past the threshold
        dnd.updateDrag(at);
        dnd.endDrag(at);
    }
    int total() const { return timeline->drops + arsenal->drops + row->drops + mixer->drops; }
};

void testPanelOverTimeline() {
    Rig rig;
    rig.dragTo({500, 400}); // Arsenal's empty space, timeline row behind it
    check(rig.arsenal->drops == 1, "a drop on a panel's empty space goes to the panel");
    check(rig.timeline->drops == 0, "the timeline behind the panel gets nothing");
}

void testRowInsidePanel() {
    {
        Rig rig;
        rig.dragTo({600, 160}); // on the row, outside its label
        check(rig.row->drops == 1 && rig.arsenal->drops == 0, "a drop on a row goes to the row, not its panel");
    }
    Rig again; // one rig at a time: two trees registered at once fall back to registration order
    again.dragTo({300, 160}); // on the row's label (not a target): the nearest target holding it
    check(again.row->drops == 1, "a drop on a non-target child goes to the nearest target holding it");
    check(again.timeline->drops == 0, "and never to the timeline");
}

void testUncoveredTimeline() {
    {
        Rig rig;
        rig.dragTo({1000, 300}); // outside every panel; the overlay layer is pass-through
        check(rig.timeline->drops == 1, "the timeline takes drops where nothing covers it");
    }
    Rig hidden;
    hidden.arsenal->setVisible(false);
    hidden.dragTo({500, 400});
    check(hidden.timeline->drops == 1, "a hidden panel does not cover the timeline");
}

void testCoverThatTakesNoDrops() {
    Rig rig;
    rig.pianoRoll->setVisible(true);
    rig.dragTo({300, 700}); // over the piano roll, timeline behind it
    check(rig.total() == 0, "a panel that takes no drops still covers the timeline: the drop is rejected");
}

void testOverlappingPanelsFollowScreenOrder() {
    {
        Rig rig;
        rig.mixer->setVisible(true); // added after Arsenal: on top where they overlap
        rig.dragTo({800, 500});
        check(rig.mixer->drops == 1 && rig.arsenal->drops == 0, "where panels overlap, the one in front takes the drop");
    }

    Rig raised;
    raised.mixer->setVisible(true);
    raised.arsenal->bringToFront();
    raised.dragTo({800, 500});
    check(raised.arsenal->drops == 1 && raised.mixer->drops == 0, "bringing the other panel forward hands it the drop");
}

void testCoverAppearsBetweenMoveAndRelease() {
    Rig rig;
    auto& dnd = NUIDragDropManager::getInstance();
    dnd.beginDrag(rig.data, {10, 10}, nullptr);
    dnd.updateDrag({60, 10});
    dnd.updateDrag({500, 400});   // over Arsenal
    rig.mixer->setBounds({400, 300, 400, 300});
    rig.mixer->setVisible(true);  // opens over the pointer before the release
    dnd.endDrag({500, 400});
    check(rig.mixer->drops == 1 && rig.arsenal->drops == 0, "the release goes to what is on top at release time");
}

void testClosedMenuLeftInTheTree() {
    // NUIContextMenu keeps its own shown flag (isVisible() is not virtual), and closed menus can stay
    // children of the overlay with their last bounds.
    Rig rig;
    auto menu = std::make_shared<NUIContextMenu>();
    menu->addItem("Rename", [] {});
    rig.overlay->addChild(menu);
    menu->setBounds({900, 200, 200, 200});
    rig.dragTo({1000, 300});
    check(rig.timeline->drops == 1, "a closed menu left in the tree does not cover the timeline");
    menu->showAt(NUIPoint{900, 200});
    const NUIRect shown = menu->getBounds();
    rig.dragTo({shown.x + shown.width / 2, shown.y + shown.height / 2});
    check(rig.timeline->drops == 1, "an open menu covers the timeline");
    menu->hide();
    rig.overlay->removeChild(menu);
}

void testTargetsOutsideTheTreeKeepRegistrationOrder() {
    Rig rig;
    LooseTarget loose;
    auto shared = std::shared_ptr<LooseTarget>(&loose, [](LooseTarget*) {});
    loose.bounds = {0, 0, 1366, 768};
    NUIDragDropManager::getInstance().registerDropTarget(shared);
    rig.dragTo({1000, 300});
    check(loose.drops == 1 && rig.timeline->drops == 0, "a target with no place in the tree keeps registration order");
    NUIDragDropManager::getInstance().unregisterDropTarget(&loose);
}

} // namespace

int main() {
    std::cout << "DropTargetZOrderTest\n";
    testPanelOverTimeline();
    testRowInsidePanel();
    testUncoveredTimeline();
    testCoverThatTakesNoDrops();
    testOverlappingPanelsFollowScreenOrder();
    testCoverAppearsBetweenMoveAndRelease();
    testClosedMenuLeftInTheTree();
    testTargetsOutsideTheTreeKeepRegistrationOrder();
    return g_failures == 0 ? 0 : 1;
}
