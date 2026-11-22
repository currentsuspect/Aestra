// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A dialog that has never been shown must not exist on screen (founder QA, 22 Nov 2026).
//
// The bug: SettingsDialog (and ConfirmationDialog) keep their own visibility flag, and the constructors only
// initialised that one. The base NUIComponent::visible_ stayed true, so the lazily built dialog sat in the
// window root as a full-window, drawn, hit-testable component that takes no drops. The drag-drop manager
// saw it on top of every target ("Drop landed nowhere ... drawn on top: (unnamed)") until File > Settings
// was opened and closed once, which finally called setVisible(false).
//
// Every dialog class that keeps its own flag is listed here: base isVisible() must be false after
// construction, true after show(), false after hide(); and a hidden one must not take a drop's place.

#include "ConfirmationDialog.h"
#include "ExportDialog.h"
#include "MissingAssetsDialog.h"
#include "NUIComponent.h"
#include "NUIDragDrop.h"
#include "RecoveryDialog.h"
#include "SettingsDialog.h"

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

// Reads the BASE flag: the dialogs' own isVisible() overloads shadow it and answer from their own member.
bool baseVisible(const NUIComponent& c) { return c.isVisible(); }

void testLifecycle(const std::string& name, NUIComponent& dialog, void (*show)(NUIComponent&),
                   void (*hide)(NUIComponent&)) {
    std::cout << name << "\n";
    check(!baseVisible(dialog), name + ": base isVisible() is false before show()");
    show(dialog);
    check(baseVisible(dialog), name + ": base isVisible() is true after show()");
    hide(dialog);
    check(!baseVisible(dialog), name + ": base isVisible() is false after hide()");
}

class Target : public NUIComponent, public IDropTarget {
public:
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
};

// The dialogs are added to the root after the target, full-window, exactly as AestraWindowManager does.
void testDropReachesTargetUnderNeverShownDialogs() {
    std::cout << "drop under never-shown dialogs\n";
    auto root = std::make_shared<NUIComponent>();
    root->setBounds({0, 0, 1366, 768});
    auto target = std::make_shared<Target>();
    target->setBounds({260, 90, 600, 500});
    root->addChild(target);

    auto settings = std::make_shared<Aestra::SettingsDialog>();
    auto confirm = std::make_shared<Aestra::ConfirmationDialog>();
    auto exportDlg = std::make_shared<ExportDialog>();
    for (const std::shared_ptr<NUIComponent>& d :
         {std::static_pointer_cast<NUIComponent>(settings), std::static_pointer_cast<NUIComponent>(confirm),
          std::static_pointer_cast<NUIComponent>(exportDlg)}) {
        d->setBounds({0, 0, 1366, 768});
        root->addChild(d);
    }

    auto& dnd = NUIDragDropManager::getInstance();
    dnd.registerDropTarget(target);
    DragData data;
    data.type = DragDataType::File;
    data.filePath = "/tmp/kick.wav";
    data.displayName = "kick.wav";
    const NUIPoint at{500, 300};
    dnd.beginDrag(data, {10, 10}, nullptr);
    dnd.updateDrag({60, 10});
    dnd.updateDrag(at);
    const std::string why = dnd.describeDropPoint(at);
    dnd.endDrag(at);
    dnd.unregisterDropTarget(target.get());

    std::cout << "  (" << why << ")\n";
    check(target->drops == 1, "the sample lands on the target behind the hidden dialogs");
    check(why.find("(unnamed)") == std::string::npos, "the drop log never says (unnamed) for the cover");
}

// The "Drop landed nowhere" line names the covering class, not "(unnamed)".
void testCoverIsNamedInTheLog() {
    std::cout << "cover named in the drop log\n";
    struct NamedCover : NUIComponent {};
    auto root = std::make_shared<NUIComponent>();
    root->setBounds({0, 0, 800, 600});
    auto target = std::make_shared<Target>();
    target->setBounds({0, 0, 800, 600});
    auto cover = std::make_shared<NamedCover>();
    cover->setBounds({0, 0, 800, 600});
    root->addChild(target);
    root->addChild(cover);
    auto& dnd = NUIDragDropManager::getInstance();
    dnd.registerDropTarget(target);
    const std::string why = dnd.describeDropPoint({100, 100});
    dnd.unregisterDropTarget(target.get());
    std::cout << "  (" << why << ")\n";
    check(why.find("NamedCover") != std::string::npos, "a covering component is named by its class");
}

} // namespace

int main() {
    {
        Aestra::SettingsDialog d;
        testLifecycle("SettingsDialog", d,
                      [](NUIComponent& c) { static_cast<Aestra::SettingsDialog&>(c).show(); },
                      [](NUIComponent& c) { static_cast<Aestra::SettingsDialog&>(c).hide(); });
    }
    {
        Aestra::ConfirmationDialog d;
        testLifecycle("ConfirmationDialog", d,
                      [](NUIComponent& c) {
                          static_cast<Aestra::ConfirmationDialog&>(c).show("t", "m", [](auto) {});
                      },
                      [](NUIComponent& c) { static_cast<Aestra::ConfirmationDialog&>(c).hide(); });
    }
    {
        Aestra::RecoveryDialog d;
        testLifecycle("RecoveryDialog", d,
                      [](NUIComponent& c) { static_cast<Aestra::RecoveryDialog&>(c).show("/nonexistent", [](Aestra::RecoveryResponse) {}); },
                      [](NUIComponent& c) { static_cast<Aestra::RecoveryDialog&>(c).hide(); });
    }
    {
        Aestra::MissingAssetsDialog d;
        testLifecycle("MissingAssetsDialog", d,
                      [](NUIComponent& c) { static_cast<Aestra::MissingAssetsDialog&>(c).show({}, nullptr); },
                      [](NUIComponent& c) { static_cast<Aestra::MissingAssetsDialog&>(c).hide(); });
    }
    {
        // show() needs a live engine; its constructor and hide() paths are the ones that matter here.
        ExportDialog d;
        std::cout << "ExportDialog\n";
        check(!baseVisible(d), "ExportDialog: base isVisible() is false before show()");
        d.hide();
        check(!baseVisible(d), "ExportDialog: base isVisible() is false after hide()");
    }
    testDropReachesTargetUnderNeverShownDialogs();
    testCoverIsNamedInTheLog();

    std::cout << (g_failures ? "FAILED" : "ALL PASSED") << " (" << g_failures << " failure(s))\n";
    return g_failures ? 1 : 0;
}
