// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "NUICustomWindow.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "SettingsDialog.h"
#include "UnifiedHUD.h"
#include "../AestraPlat/include/AestraPlatform.h"
#include <memory>
#include <functional>

using namespace AestraUI;

class AestraContent;

/**
 * @brief Root component that contains the custom window
 */
class AestraRootComponent : public NUIComponent {
public:
    enum class TransportAction { Stop, Play, Pause };

private:
    std::shared_ptr<NUICustomWindow> m_rootCustomWindow;
    std::shared_ptr<Aestra::SettingsDialog> m_rootSettingsDialog;
    std::shared_ptr<UnifiedHUD> m_rootUnifiedHUD;
    class AestraContent* m_rootContent{nullptr};
    std::function<void(TransportAction)> m_rootTransportCallback;

public:
    AestraRootComponent() = default;
    
    void setTransportCallback(std::function<void(TransportAction)> cb) {
        m_rootTransportCallback = cb;
    }
    
    void setContent(class AestraContent* content) {
        m_rootContent = content;
    }
    
    void setCustomWindow(std::shared_ptr<NUICustomWindow> window) {
        m_rootCustomWindow = window;
        addChild(m_rootCustomWindow);
    }
    
    void setSettingsDialog(std::shared_ptr<Aestra::SettingsDialog> dialog) {
        m_rootSettingsDialog = dialog;
    }
    
    void setUnifiedHUD(std::shared_ptr<UnifiedHUD> hud) {
        m_rootUnifiedHUD = hud;
        addChild(m_rootUnifiedHUD);
    }
    
    std::shared_ptr<UnifiedHUD> getUnifiedHUD() const {
        return m_rootUnifiedHUD;
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
    
    bool onKeyEvent(const NUIKeyEvent& event) override;
    
    void onResize(int width, int height) override {
        if (m_rootCustomWindow) {
            m_rootCustomWindow->setBounds(NUIRect(0, 0, width, height));
        }
        
        // Resize all children (including audio settings dialog)
        for (auto& child : getChildren()) {
            if (child) {
                child->onResize(width, height);
            }
        }
        
        NUIComponent::onResize(width, height);
    }
};
