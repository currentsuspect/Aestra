// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraRootComponent.h"
#include "AestraContent.h"
#include "TrackManager.h"
#include "AudioEngine.h"
#include "../AestraCore/include/AestraLog.h"

bool AestraRootComponent::onKeyEvent(const NUIKeyEvent& event) {
    if (event.keyCode == NUIKeyCode::Space && m_rootContent) {
        if (m_rootContent->onKeyEvent(event)) {
            return true;
        }
    }

    // 1. First, dispatch to focused component (correct behavior)
    if (auto focused = AestraUI::NUIComponent::getFocusedComponent()) {
        if (focused->onKeyEvent(event)) {
             return true;
        }
    }

    // 2. Fallback to global shortcuts
    if (event.pressed) {
        // F12: HUD
        if (event.keyCode == NUIKeyCode::F12) {
            if (m_rootUnifiedHUD) {
                m_rootUnifiedHUD->setVisible(!m_rootUnifiedHUD->isVisible());
                return true;
            }
        }
    }
    
    return false;
}
