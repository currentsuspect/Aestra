// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once


namespace AestraUI {

/**
 * @file PanelMode.h
 * @brief SPEC 3 §1.1: how a panel behaves, as two independent choices (owner ruling 2026-09-23).
 *
 *  - **Floating / Docked**: a floating panel moves and resizes freely, as every panel always has;
 *    a docked panel is locked where it is (no title-bar drag, no edge resize) and reopens there.
 *  - **Contextual / Keep Open**: a contextual panel closes when you press outside it; a
 *    Keep Open panel stays until you close it.
 *
 * Header-only and store-free: AestraUI cannot include the app's store, so the app supplies and
 * persists the value (see Source/Core/PanelPlacement.h).
 */
struct PanelMode {
    bool docked = false;
    bool keepOpen = true;
    bool operator==(const PanelMode& o) const { return docked == o.docked && keepOpen == o.keepOpen; }
};

} // namespace AestraUI
