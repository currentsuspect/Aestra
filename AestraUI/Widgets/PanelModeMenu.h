// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIContextMenu.h"
#include "PanelMode.h"

#include <functional>
#include <memory>

namespace AestraUI {

/// What the user picked from a panel's menu. Each callback gets the new mode already computed.
struct PanelModeMenuActions {
    std::function<void(PanelMode)> setMode;
    std::function<void()> resetPosition;
};

/// The panel's own menu: Dock Panel / Float Panel, Keep Open / Make Contextual, Reset Position.
/// The wording is the owner's (2026-09-23). Items show the choice you would make, not the current state.
inline std::shared_ptr<NUIContextMenu> buildPanelModeMenu(PanelMode current, PanelModeMenuActions actions) {
    auto menu = std::make_shared<NUIContextMenu>();
    menu->addItem(current.docked ? "Float Panel" : "Dock Panel", [current, actions]() {
        PanelMode next = current;
        next.docked = !current.docked;
        if (actions.setMode)
            actions.setMode(next);
    });
    menu->addItem(current.keepOpen ? "Make Contextual" : "Keep Open", [current, actions]() {
        PanelMode next = current;
        next.keepOpen = !current.keepOpen;
        if (actions.setMode)
            actions.setMode(next);
    });
    menu->addSeparator();
    menu->addItem("Reset Position", [actions]() {
        if (actions.resetPosition)
            actions.resetPosition();
    });
    return menu;
}

} // namespace AestraUI
