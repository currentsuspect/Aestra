// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NomadRootComponent.h"
#include "NomadContent.h"
#include "../NomadAudio/include/TrackManager.h"
#include "../NomadAudio/include/AudioEngine.h"
#include "../NomadCore/include/NomadLog.h"

bool NomadRootComponent::onKeyEvent(const NUIKeyEvent& event) {
    // 1. First, dispatch to focused component (correct behavior)
    if (auto focused = NomadUI::NUIComponent::getFocusedComponent()) {
        if (focused->onKeyEvent(event)) {
             return true;
        }
    }

    // 2. Fallback to global shortcuts
    if (event.pressed) {
        // Space: Play/Stop
        if (event.keyCode == NUIKeyCode::Space) {
            if (event.repeat) return true; // Ignore auto-repeat for transport toggle
            
            if (m_rootContent && m_rootContent->getTrackManager()) {
                NOMAD_LOG_INFO("[Root] Space Fallback. playing=" + std::string(m_rootContent->getTrackManager()->isPlaying() ? "true" : "false"));
                if (m_rootContent->getTrackManager()->isPlaying()) {
                    if (m_rootTransportCallback) m_rootTransportCallback(TransportAction::Stop);
                } else {
                    if (m_rootTransportCallback) m_rootTransportCallback(TransportAction::Play);
                }
                return true;
            }
        }
        
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
