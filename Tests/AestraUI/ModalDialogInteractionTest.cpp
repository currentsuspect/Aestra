// © 2026 Aestra Studios — All Rights Reserved.

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
    const float dialogX = (800.0f - 450.0f) * 0.5f;
    const float dialogY = (600.0f - 180.0f) * 0.5f;
    const float startX = dialogX + (450.0f - (240.0f + theme.spacingM)) * 0.5f;
    const float discardX = startX + 120.0f + theme.spacingM + 20.0f;
    const float buttonY = dialogY + 180.0f - theme.layout.dialogActionHeight - theme.spacingM + 10.0f;

    check(dialog.onMouseEvent(leftButton(discardX, buttonY, true)), "recovery consumes the button press");
    check(dialog.isDialogVisible() && callbacks == 0, "recovery waits for the matching release");
    check(dialog.onMouseEvent(leftButton(discardX, buttonY, false)), "recovery consumes the button release");
    check(!dialog.isDialogVisible() && response == RecoveryResponse::Discard && callbacks == 1,
          "one recovery click activates Discard exactly once");
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
    const float dialogY = (600.0f - 172.0f) * 0.5f;
    const float totalWidth = 84.0f + 104.0f + 96.0f + theme.spacingS * 2.0f;
    const float startX = dialogX + 400.0f - theme.spacingL - totalWidth;
    const float dontSaveX = startX + 84.0f + theme.spacingS + 20.0f;
    const float buttonY = dialogY + 172.0f - theme.spacingL - theme.layout.dialogActionHeight + 10.0f;

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
