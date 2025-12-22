// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../../NomadAudio/include/UnitManager.h"
#include <functional>
#include <memory>

namespace Nomad { 
    namespace Audio { 
        class TrackManager; 
        struct PatternID;
    } 
}

namespace NomadUI {

class UnitRow : public NUIComponent {
public:
    UnitRow(std::shared_ptr<Nomad::Audio::TrackManager> trackManager, Nomad::Audio::UnitManager& manager, Nomad::Audio::UnitID unitId, Nomad::Audio::PatternID patternId);

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    // Called by parent to refresh state reference
    void updateState();

private:
    std::shared_ptr<Nomad::Audio::TrackManager> m_trackManager;
    Nomad::Audio::UnitManager& m_manager;
    Nomad::Audio::UnitID m_unitId;
    Nomad::Audio::PatternID m_patternId; // The active pattern being edited

    // Cached state
    std::string m_name;
    uint32_t m_color;
    Nomad::Audio::UnitGroup m_group;
    bool m_isEnabled = true;
    bool m_isArmed = false;
    bool m_isMuted = false;
    bool m_isSolo = false;
    std::string m_audioClip; // Audio clip filename
    int m_mixerChannel = -1; // Mixer route

    // Layout
    float m_controlWidth = 220.0f;
    
    // Interaction
    bool m_isHovered = false;
    
    // Helpers
    void drawControlBlock(NUIRenderer& renderer, const NUIRect& bounds);
    void drawContextBlock(NUIRenderer& renderer, const NUIRect& bounds);
    
    void handleControlClick(const NUIMouseEvent& event, const NUIRect& bounds);
    void handleContextClick(const NUIMouseEvent& event, const NUIRect& bounds);
};

} // namespace NomadUI
