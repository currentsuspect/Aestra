// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "NUICustomWindow.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "SettingsDialog.h"
#include "UnifiedHUD.h"
#include "TransportTypes.h"
#include "../AestraPlat/include/AestraPlatform.h"
#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
#include "AccountSession.h"
#include "EntitlementStore.h"
#include "LocalAccountCache.h"
#include "MembershipViewModel.h"
#endif
#include <memory>
#include <functional>

using namespace AestraUI;

class AestraContent;

/**
 * @brief Root component that contains the custom window
 */
class AestraRootComponent : public NUIComponent {
public:
    using TransportAction = Aestra::TransportAction;

private:
    std::shared_ptr<NUICustomWindow> m_rootCustomWindow;
    std::shared_ptr<Aestra::SettingsDialog> m_rootSettingsDialog;
    std::shared_ptr<UnifiedHUD> m_rootUnifiedHUD;
    class AestraContent* m_rootContent{nullptr};
    std::function<void(TransportAction)> m_rootTransportCallback;
    std::function<void()> m_rootSaveCallback;

public:
    AestraRootComponent() = default;
    
    void setTransportCallback(std::function<void(TransportAction)> cb) {
        m_rootTransportCallback = cb;
    }

    void setSaveCallback(std::function<void()> cb) {
        m_rootSaveCallback = cb;
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
        updateMembershipBadge();
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

private:
    void updateMembershipBadge() {
        if (!m_rootCustomWindow || !m_rootCustomWindow->getTitleBar()) {
            return;
        }

        std::string tier = "Core";
        std::string status = "Signed out";
        bool verified = false;
#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
        Aestra::License::EntitlementStore entitlements;
        Aestra::License::LocalAccountCache accountCache;
        Aestra::License::AccountSession accountSession(accountCache, entitlements);
        Aestra::License::MembershipViewModel viewModel(accountSession, entitlements);
        const Aestra::License::MembershipViewState state = viewModel.current();
        tier = Aestra::License::membershipBadgeTierText(state);
        status = Aestra::License::membershipBadgeStatusText(state);
        verified = state.verified;
#endif
        m_rootCustomWindow->getTitleBar()->setMembershipBadge(tier, status, verified);
    }
};
