// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ExportDialogWidgetsTest — V8-S1. The export dialog is built from NUI widgets, not hand-placed rects.
//   1. every control sits inside the dialog and no two controls in a state overlap;
//   2. each state shows only its own controls (nothing hidden can take a click);
//   3. the stems checkbox reveals its explanation and the layout still fits;
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

    std::cout << "[1] settings screen\n";
    std::vector<std::shared_ptr<NUIComponent>> shown;
    visibleControls(std::shared_ptr<NUIComponent>(&dialog, [](NUIComponent*) {}), shown);
    check(shown.size() >= 12, "output, browse, three dropdowns, tail, stems, export and cancel are shown (" +
                                  std::to_string(shown.size()) + ")");
    const NUIRect dialogRect(1280 * 0.5f - 260.0f, 400.0f - 200.0f, 520.0f, 400.0f);
    check(allInside(shown, dialogRect), "every control is inside the dialog");
    check(noOverlap(shown), "no two controls overlap");

    std::cout << "[2] stems\n";
    std::shared_ptr<NUICheckbox> stems;
    for (const auto& c : dialog.getChildren())
        if (auto cb = std::dynamic_pointer_cast<NUICheckbox>(c))
            stems = cb;
    check(stems != nullptr, "the stems checkbox exists");
    if (stems) {
        const size_t before = shown.size();
        click(stems);
        check(stems->isChecked(), "clicking it checks it");
        std::vector<std::shared_ptr<NUIComponent>> after;
        visibleControls(std::shared_ptr<NUIComponent>(&dialog, [](NUIComponent*) {}), after);
        dialog.onResize(1280, 800);
        after.clear();
        visibleControls(std::shared_ptr<NUIComponent>(&dialog, [](NUIComponent*) {}), after);
        check(after.size() == before + 1, "its explanation appears");
        check(allInside(after, dialogRect) && noOverlap(after), "the layout still fits and does not overlap");
        click(stems);
    }

    std::cout << "[3] a bad request ends on the failure screen\n";
    bool callbackFired = false, callbackOk = true;
    dialog.setOnExportComplete([&](bool ok, const std::string&, double, double) {
        callbackFired = true;
        callbackOk = ok;
    });
    std::shared_ptr<NUITextInput> output;
    std::shared_ptr<NUIButton> exportButton, doneButton;
    for (const auto& c : dialog.getChildren()) {
        if (auto t = std::dynamic_pointer_cast<NUITextInput>(c); t && !output)
            output = t;
        if (auto b = std::dynamic_pointer_cast<NUIButton>(c)) {
            if (b->getText() == "Export")
                exportButton = b;
            if (b->getText() == "Done" || b->getText() == "Close")
                doneButton = b;
        }
    }
    check(output && exportButton && doneButton, "found the output field and buttons");
    if (output && exportButton && doneButton) {
        output->setText("");
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
