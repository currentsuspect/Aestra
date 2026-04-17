// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraRootComponent.h"
#include "AestraContent.h"
#include "TrackManager.h"
#include "AudioEngine.h"
#include "../AestraCore/include/AestraLog.h"

bool AestraRootComponent::onKeyEvent(const NUIKeyEvent& event) {
    // 1. Global app-level shortcuts (Ctrl+Z, Ctrl+Y, Space) — BEFORE focused component
    if (m_rootContent) {
        if (m_rootContent->onKeyEvent(event)) {
            return true;
        }
    }

    // 2. Dispatch to focused component
    if (auto focused = AestraUI::NUIComponent::getFocusedComponent()) {
        if (focused->onKeyEvent(event)) {
             return true;
        }
    }

    // 3. Fallback: F12 HUD toggle
    if (event.pressed) {
        if (event.keyCode == NUIKeyCode::F12) {
            if (m_rootUnifiedHUD) {
                m_rootUnifiedHUD->setVisible(!m_rootUnifiedHUD->isVisible());
                return true;
            }
        }
    }
    
    return false;
}
