// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// SPEC 3 §1.1 (ruled 2026-09-23, refined 2026-10-11): each panel's mode lives in the UI-state
// store beside its geometry, so it is a user preference and never written into a project.
//
// Defaults: plugin editors are Floating + Contextual (they close when you press outside them).
// The workspace panels (Mixer, Piano Roll, Arsenal/Sequencer, History, Takes) are Floating +
// Keep Open, as they always were: closing the piano roll when you click the timeline would
// break editing, so the owner kept today's behaviour as their default. Every panel can switch.
// Plugin editors share ONE mode ("editors"), not one per instance, so the store does not
// accumulate an entry for every plugin ever opened.

#include "PanelMode.h"
#include "UISurfaceStore.h"

#include <optional>
#include <string>
#include <vector>

namespace Aestra {

/// The key the editors' shared mode is stored under.
inline constexpr char kPluginEditorsModeKey[] = "editors";

inline AestraUI::PanelMode defaultPanelMode(bool isPluginEditor) {
    AestraUI::PanelMode mode;
    mode.docked = false;
    mode.keepOpen = !isPluginEditor;
    return mode;
}

inline std::string panelDockedKey(const std::string& panelKey) { return panelKey + ".docked"; }
inline std::string panelKeepOpenKey(const std::string& panelKey) { return panelKey + ".keepOpen"; }

/// The stored mode for @p panelKey, each field independently falling back to the default.
template <typename Store>
AestraUI::PanelMode readPanelMode(const Store* store, const std::string& panelKey, bool isPluginEditor) {
    AestraUI::PanelMode mode = defaultPanelMode(isPluginEditor);
    if (!store)
        return mode;
    if (const std::optional<bool> docked = store->boolPreference(panelDockedKey(panelKey)))
        mode.docked = *docked;
    if (const std::optional<bool> keepOpen = store->boolPreference(panelKeepOpenKey(panelKey)))
        mode.keepOpen = *keepOpen;
    return mode;
}

/// Writes only what differs from the default, and removes nothing: a field set back to its
/// default is stored as that value, so a later change of the default never silently flips it.
template <typename Store>
void writePanelMode(Store* store, const std::string& panelKey, const AestraUI::PanelMode& mode) {
    if (!store)
        return;
    store->setBoolPreference(panelDockedKey(panelKey), mode.docked);
    store->setBoolPreference(panelKeepOpenKey(panelKey), mode.keepOpen);
}

/// Which of the panels that saw a press outside themselves should close, a frame after the press.
/// A press that itself opened or closed a view (the click on a transport toggle) leaves every
/// panel as the press left it: closing here as well would undo the toggle. Otherwise a panel
/// closes only if it is still open and still contextual.
template <typename View, typename IsOpenAndContextual>
std::vector<View> contextualViewsToClose(const std::vector<View>& sawOutsidePress, bool viewChangedDuringPress,
                                         IsOpenAndContextual&& isOpenAndContextual) {
    std::vector<View> toClose;
    if (viewChangedDuringPress)
        return toClose;
    for (const View& view : sawOutsidePress)
        if (isOpenAndContextual(view))
            toClose.push_back(view);
    return toClose;
}

} // namespace Aestra
