// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ExportDialogWidgetsTest — V8-S1. The export dialog is built from NUI widgets, not hand-placed rects.
//   1. every control sits inside the dialog and no two controls in a state overlap;
//   2. each state shows only its own controls (nothing hidden can take a click);
//   3. the stems segment turns stems on and locks the format to WAV, and the layout still fits;
//   4. a bad request lands on the failure screen with its reason, and Close reports it.

#include "ExportDialog.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "NUIThemeSystem.h"

#include <cstdlib>
#include <functional>
#include <iostream>

using namespace AestraUI;

namespace {
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << "  " << (cond ? "PASS " : "FAIL ") << what << "\n";
    if (!cond)
        ++g_failures;
}

void visibleControls(const std::shared_ptr<NUIComponent>& node, std::vector<std::shared_ptr<NUIComponent>>& out) {
    for (const auto& child : node->getChildren())
        if (child->isVisible())
            out.push_back(child);
}

NUIPoint centre(const NUIRect& r) { return {r.x + r.width * 0.5f, r.y + r.height * 0.5f}; }

void click(const std::shared_ptr<NUIComponent>& c) {
    NUIMouseEvent down;
    down.type = NUIMouseEventType::Down;
    down.position = centre(c->getBounds());
    down.button = NUIMouseButton::Left;
    down.pressed = true;
    NUIMouseEvent up = down;
    up.type = NUIMouseEventType::Up;
    up.pressed = false;
    up.released = true;
    c->onMouseEvent(down);
    c->onMouseEvent(up);
}

bool allInside(const std::vector<std::shared_ptr<NUIComponent>>& v, const NUIRect& dialog) {
    for (const auto& c : v) {
        const NUIRect b = c->getBounds();
        if (b.x < dialog.x - 0.5f || b.y < dialog.y || b.right() > dialog.right() + 0.5f || b.bottom() > dialog.bottom())
            return false;
    }
    return true;
}

bool noOverlap(const std::vector<std::shared_ptr<NUIComponent>>& v) {
    for (size_t i = 0; i < v.size(); ++i)
        for (size_t j = i + 1; j < v.size(); ++j) {
            const NUIRect a = v[i]->getBounds(), b = v[j]->getBounds();
            if (a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f)
                return false;
        }
    return true;
}
} // namespace

int main() {
    ExportDialog dialog;
    dialog.setBounds({0, 0, 1280, 800});

    // show() needs an engine and a track manager only to start a render; this test never renders.
    // Reaching the settings screen through show() is what a user does, so use a real (empty) project.
    Aestra::Audio::AudioEngine engine;
    Aestra::Audio::TrackManager tracks;
    dialog.show("", engine, tracks);
    dialog.onResize(1280, 800);

    std::cout << "[0] a saved project offers its own name (#1296)\n";
    {
        // Opened with a project on disk, the name field starts from the project's name, in the project's folder.
        // It offered "Untitled" in the working directory for a project opened from the command line.
        ExportDialog named;
        named.setBounds({0, 0, 1280, 800});
        named.show("/music/beats/Night Session.aes", engine, tracks);
        std::string offered;
        for (const auto& c : named.getChildren())
            if (auto t = std::dynamic_pointer_cast<NUITextInput>(c)) {
                offered = t->getText();
                break;
            }
        check(offered.rfind("Night Session", 0) == 0, "the name starts from the project's name (got \"" + offered + "\")");
        bool inProjectFolder = false;
        for (const auto& c : named.getChildren())
            if (auto b = std::dynamic_pointer_cast<NUIButton>(c); b && b->getText() == "in beats")
                inProjectFolder = true;
        check(inProjectFolder, "and the folder is the project's (\"in beats\")");
    }

    std::cout << "[1] settings screen\n";
    std::vector<std::shared_ptr<NUIComponent>> shown;
    visibleControls(std::shared_ptr<NUIComponent>(&dialog, [](NUIComponent*) {}), shown);
    check(shown.size() == 8, "name, folder, format, depth, rate, tail, export and cancel are shown (" +
                                 std::to_string(shown.size()) + ")");
    // Board 3: 560 x 392, centred.
    const NUIRect dialogRect(1280 * 0.5f - 280.0f, 400.0f - 196.0f, 560.0f, 392.0f);
    check(allInside(shown, dialogRect), "every control is inside the dialog");
    check(noOverlap(shown), "no two controls overlap");

    std::cout << "[2] stems\n";
    // Stems are a painted segmented control above the name field. Walk up from the field on the right half
    // ("Stems + master") until a click turns stems on.
    std::shared_ptr<NUITextInput> nameField;
    std::shared_ptr<NUIDropdown> formatMenu;
    for (const auto& c : dialog.getChildren()) {
        if (auto t = std::dynamic_pointer_cast<NUITextInput>(c); t && !nameField)
            nameField = t;
        if (auto d = std::dynamic_pointer_cast<NUIDropdown>(c); d && d->isVisible()) // the format menu is added last
            formatMenu = d;
    }
    check(nameField && formatMenu, "found the name field and the format menu");
    if (nameField && formatMenu) {
        const NUIRect field = nameField->getBounds();
        NUIMouseEvent down;
        down.type = NUIMouseEventType::Down;
        down.button = NUIMouseButton::Left;
        down.pressed = true;
        for (float y = field.y - 1.0f; y > dialogRect.y && !dialog.exportsStems(); y -= 2.0f) {
            down.position = {dialogRect.x + dialogRect.width * 0.75f, y};
            dialog.onMouseEvent(down);
        }
        check(dialog.exportsStems(), "clicking Stems + master turns stems on");
        check(!formatMenu->isEnabled(), "and locks the format to WAV");
        dialog.onResize(1280, 800);
        std::vector<std::shared_ptr<NUIComponent>> after;
        visibleControls(std::shared_ptr<NUIComponent>(&dialog, [](NUIComponent*) {}), after);
        check(after.size() == shown.size(), "the same controls stay shown");
        check(allInside(after, dialogRect) && noOverlap(after), "the layout still fits and does not overlap");
        down.position.x = dialogRect.x + dialogRect.width * 0.25f;
        dialog.onMouseEvent(down);
        check(!dialog.exportsStems() && formatMenu->isEnabled(), "One file turns stems off again");
    }

    std::cout << "[3] a bad request ends on the failure screen\n";
    bool callbackFired = false, callbackOk = true;
    dialog.setOnExportComplete([&](bool ok, const std::string&, double, double) {
        callbackFired = true;
        callbackOk = ok;
    });
    // An empty name now becomes "Untitled", so the bad request is a tail that is not a number.
    std::shared_ptr<NUITextInput> tail;
    std::shared_ptr<NUIButton> exportButton, doneButton;
    for (const auto& c : dialog.getChildren()) {
        if (auto t = std::dynamic_pointer_cast<NUITextInput>(c))
            tail = t; // the tail field is the last text input
        if (auto b = std::dynamic_pointer_cast<NUIButton>(c)) {
            if (b->getText() == "Export")
                exportButton = b;
            if (b->getText() == "Done" || b->getText() == "Close")
                doneButton = b;
        }
    }
    check(tail && exportButton && doneButton, "found the tail field and buttons");
    if (tail && exportButton && doneButton) {
        tail->setText("not a number");
        click(exportButton);
        check(!exportButton->isVisible() && doneButton->isVisible(), "Export gives way to Close");
        check(doneButton->getText() == "Close", "the button says Close after a failure");
        click(doneButton);
        check(callbackFired && !callbackOk, "Close reports the failure");
        check(!dialog.isVisible(), "and the dialog closes");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
