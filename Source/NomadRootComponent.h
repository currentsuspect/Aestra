// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUICustomWindow.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "SettingsDialog.h"
#include "UnifiedHUD.h"
#include <memory>

using namespace NomadUI;

/**
 * @brief Root component that contains the custom window
 */
class NomadRootComponent : public NUIComponent {
public:
    NomadRootComponent() = default;
    
    void setCustomWindow(std::shared_ptr<NUICustomWindow> window) {
        m_customWindow = window;
        addChild(m_customWindow);
    }
    
    void setSettingsDialog(std::shared_ptr<Nomad::SettingsDialog> dialog) {
        m_settingsDialog = dialog;
    }
    
    void setUnifiedHUD(std::shared_ptr<UnifiedHUD> hud) {
        m_unifiedHUD = hud;
        addChild(m_unifiedHUD);
    }
    
    std::shared_ptr<UnifiedHUD> getUnifiedHUD() const {
        return m_unifiedHUD;
    }
    
    void onUpdate(double deltaTime) override {
        NUIComponent::updateGlobalTooltip(deltaTime);
        NUIComponent::onUpdate(deltaTime);
    }

    void onRender(NUIRenderer& renderer) override {
        // Don't draw background here - let custom window handle it
        // Just render children (custom window and audio settings dialog)
        renderChildren(renderer);
        
        // Audio settings dialog is handled by its own render method
        
        // Fix: Render global tooltips last so they appear on top of everything
        NUIComponent::renderGlobalTooltip(renderer);
    }
    
    void onResize(int width, int height) override {
        if (m_customWindow) {
            m_customWindow->setBounds(NUIRect(0, 0, width, height));
        }
        
        // Resize all children (including audio settings dialog)
        for (auto& child : getChildren()) {
            if (child) {
                child->onResize(width, height);
            }
        }
        
        NUIComponent::onResize(width, height);
    }
    
private:
    std::shared_ptr<NUICustomWindow> m_customWindow;
    std::shared_ptr<Nomad::SettingsDialog> m_settingsDialog;
    std::shared_ptr<UnifiedHUD> m_unifiedHUD;
};
