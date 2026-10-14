// © 2026 Aestra Studios — All Rights Reserved.

#include "Widgets/AestraPanelWindow.h"
#include "ConfirmationDialog.h"
#include "NUIThemeSystem.h"
#include "RecoveryDialog.h"

#include <iostream>

using namespace Aestra;
using namespace AestraUI;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++failures;
    }
}

NUIMouseEvent leftButton(float x, float y, bool pressed) {
    NUIMouseEvent event;
    event.type = pressed ? NUIMouseEventType::Down : NUIMouseEventType::Up;
    event.position = {x, y};
    event.button = NUIMouseButton::Left;
    event.pressed = pressed;
    event.released = !pressed;
    return event;
}

void testRecoveryRequiresMatchingPressAndRelease() {
    RecoveryDialog dialog;
    dialog.setBounds({0.0f, 0.0f, 800.0f, 600.0f});
    RecoveryResponse response = RecoveryResponse::None;
    int callbacks = 0;
    dialog.show("", [&](RecoveryResponse value) {
        response = value;
        ++callbacks;
    });

    const auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
    // These mirror RecoveryDialog::calculateLayout(). The title-bar term is the shared chrome's rather than a second
    // copy of 32, so the two cannot drift if the chrome's bar height is retuned. The card is 900 wide where it can
    // be, and three buttons sit in a row from 32 px inside its left edge.
    const float dialogW = 800.0f - 40.0f;
    const float dialogX = (800.0f - dialogW) * 0.5f;
    const float dialogH = 392.0f + AestraUI::AestraPanelWindow::TITLE_BAR_H;
    const float dialogY = (600.0f - dialogH) * 0.5f;
    const float lastSavedX = dialogX + 32.0f + 204.0f + theme.spacingM + 176.0f + theme.spacingM + 188.0f * 0.5f;
    const float buttonY = dialogY + dialogH - 38.0f - 28.0f + 19.0f;
    const float discardX = lastSavedX;

    check(dialog.onMouseEvent(leftButton(discardX, buttonY, true)), "recovery consumes the button press");
    check(dialog.isDialogVisible() && callbacks == 0, "recovery waits for the matching release");
    check(dialog.onMouseEvent(leftButton(discardX, buttonY, false)), "recovery consumes the button release");
    check(!dialog.isDialogVisible() && response == RecoveryResponse::OpenLastSaved && callbacks == 1,
          "one recovery click activates Open last saved version exactly once");
}

void testConfirmationReleaseCannotEscapeModal() {
    ConfirmationDialog dialog;
    dialog.setBounds({0.0f, 0.0f, 800.0f, 600.0f});
    DialogResponse response = DialogResponse::None;
    int callbacks = 0;
    dialog.show("Unsaved Changes", "Save before closing?", [&](DialogResponse value) {
        response = value;
        ++callbacks;
    });

    const auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
    const float dialogX = (800.0f - 400.0f) * 0.5f;
    // Mirrors ConfirmationDialog::calculateLayout(), title bar included.
    const float dialogH = 172.0f + AestraUI::AestraPanelWindow::TITLE_BAR_H;
    const float dialogY = (600.0f - dialogH) * 0.5f;
    const float totalWidth = 84.0f + 104.0f + 96.0f + theme.spacingS * 2.0f;
    const float startX = dialogX + 400.0f - theme.spacingL - totalWidth;
    const float dontSaveX = startX + 84.0f + theme.spacingS + 20.0f;
    const float buttonY = dialogY + dialogH - theme.spacingL - theme.layout.dialogActionHeight + 10.0f;

    dialog.onMouseEvent(leftButton(dontSaveX, buttonY, true));
    dialog.onMouseEvent(leftButton(dontSaveX, buttonY, false));
    check(!dialog.isDialogVisible() && response == DialogResponse::DontSave && callbacks == 1,
          "confirmation consumes the full click and activates Don't Save once");
}

} // namespace

int main() {
    testRecoveryRequiresMatchingPressAndRelease();
    testConfirmationReleaseCannotEscapeModal();

    if (failures == 0) {
        std::cout << "Modal dialog interaction tests passed\n";
        return 0;
    }
    std::cout << failures << " modal dialog interaction test(s) failed\n";
    return 1;
}
