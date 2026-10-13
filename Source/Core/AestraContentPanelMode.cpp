// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SPEC 3 section 1.1: each floating panel is Floating or Docked, and Contextual or Keep Open.
// Kept out of AestraContent.cpp, which is at its size pin.
#include "AestraContent.h"

#include "../AestraUI/Widgets/PanelModeMenu.h"
#include "../AestraUI/Widgets/PluginUIController.h"
#include "FloatingPanelDescriptors.h"
#include "PanelPlacement.h"
#include "UISurfaceResolution.h"
#include "Panels/ArsenalPanel.h"
#include "Panels/WindowPanel.h"

#include <algorithm>

using namespace Aestra;

namespace {
const Content::FloatingPanelGeometry* specFor(Audio::ViewType view) {
    for (const auto& g : Content::kFloatingPanelGeometry)
        if (g.view == view)
            return &g;
    return nullptr;
}
} // namespace

AestraUI::PanelMode AestraContent::panelMode(Audio::ViewType view) const {
    const auto* spec = specFor(view);
    return spec ? readPanelMode(surfaceStore(), spec->storeKey, /*isPluginEditor=*/false) : defaultPanelMode(false);
}

void AestraContent::applyPanelMode(Audio::ViewType view) {
    auto panel = panelForView(view);
    if (!panel)
        return;
    const AestraUI::PanelMode mode = panelMode(view);
    panel->setLocked(mode.docked);
    panel->setContextual(!mode.keepOpen);
}

void AestraContent::wireEditorMode() {
    if (!m_pluginController)
        return;
    m_pluginController->setEditorModeProvider(
        [this]() { return readPanelMode(surfaceStore(), kPluginEditorsModeKey, /*isPluginEditor=*/true); },
        [this](const AestraUI::PanelMode& next) { writePanelMode(surfaceStore(), kPluginEditorsModeKey, next); });
}

void AestraContent::wirePanelMode(const std::shared_ptr<Audio::WindowPanel>& panel, Audio::ViewType view) {
    panel->setOnMenuRequested([this, view](const AestraUI::NUIPoint& at) { showPanelMenu(view, at); });
    panel->setOnOutsidePress([this, view]() { noteOutsidePress(view); });
    applyPanelMode(view);
    if (view == Audio::ViewType::Sequencer)
        if (auto arsenal = std::dynamic_pointer_cast<Audio::ArsenalPanel>(panel))
            arsenal->setOnPlaceInTimeline([this](Audio::PatternID id) { placePatternInTimeline(id); });
}

void AestraContent::showPanelMenu(Audio::ViewType view, const AestraUI::NUIPoint& at) {
    const auto* spec = specFor(view);
    if (!spec || !m_overlayLayer)
        return;
    AestraUI::PanelModeMenuActions actions;
    actions.setMode = [this, view, key = std::string(spec->storeKey)](AestraUI::PanelMode next) {
        writePanelMode(surfaceStore(), key, next);
        applyPanelMode(view);
    };
    actions.resetPosition = [this, view, spec]() {
        savePanelPreference(view, defaultSurfacePreference(spec->defaultWidth, spec->defaultHeight,
                                                           spec->defaultAnchorX, spec->defaultAnchorY, spec->defaultMaximized));
        applyPanelPreference(view);
        setDirty(true);
    };
    auto menu = AestraUI::buildPanelModeMenu(panelMode(view), std::move(actions));
    menu->setOnHide([this, weak = std::weak_ptr<AestraUI::NUIContextMenu>(menu)]() {
        if (auto shown = weak.lock(); shown && m_overlayLayer)
            m_overlayLayer->removeChild(shown);
    });
    m_overlayLayer->addChild(menu);
    menu->showAt(at);
}

// A contextual panel sees EVERY left press (the overlay forwards them all), including the click on its own
// toggle button in the transport. So it only notes the press; closeContextualPanels() decides a frame
// later, after whatever the press did (opening or closing a view) has happened.
void AestraContent::noteOutsidePress(Audio::ViewType view) {
    if (m_outsidePressViews.empty())
        m_outsidePressSerial = m_viewChangeSerial;
    if (std::find(m_outsidePressViews.begin(), m_outsidePressViews.end(), view) == m_outsidePressViews.end())
        m_outsidePressViews.push_back(view);
}

void AestraContent::closeContextualPanels() {
    if (m_outsidePressViews.empty())
        return;
    const auto noted = std::move(m_outsidePressViews);
    m_outsidePressViews.clear();
    const auto toClose = contextualViewsToClose(noted, m_viewChangeSerial != m_outsidePressSerial, [this](Audio::ViewType view) {
        auto panel = panelForView(view);
        return panel && panel->isVisible() && panel->isContextual();
    });
    for (const auto view : toClose)
        setViewOpen(view, false);
}

void AestraContent::gatherPanels() {
    for (const auto& g : Content::kFloatingPanelGeometry) {
        auto panel = panelForView(g.view);
        if (panel && panel->isVisible())
            applyPanelPreference(g.view);
    }
    if (m_pluginController)
        m_pluginController->gatherEditors();
    setDirty(true);
}

void AestraContent::resetPanelPositions() {
    for (const auto& g : Content::kFloatingPanelGeometry) {
        savePanelPreference(g.view, defaultSurfacePreference(g.defaultWidth, g.defaultHeight, g.defaultAnchorX,
                                                             g.defaultAnchorY, g.defaultMaximized));
        if (auto panel = panelForView(g.view); panel && panel->isVisible())
            applyPanelPreference(g.view);
    }
    if (m_pluginController)
        m_pluginController->resetEditorPositions();
    setDirty(true);
}
