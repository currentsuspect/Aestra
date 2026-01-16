// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "../AestraUI/Widgets/NUIPianoRollWidgets.h"
#include "TrackManager.h"
#include "../AestraUI/Core/NUIComponent.h"
#include "NUIButton.h"
#include <memory>
#include <functional>

namespace Aestra {
namespace Audio {

/**
 * @brief Piano Roll Panel - MIDI editor with piano keyboard
 */
class PianoRollPanel : public WindowPanel {
public:
    PianoRollPanel(std::shared_ptr<TrackManager> trackManager);
    ~PianoRollPanel() override = default;

    void onUpdate(double deltaTime) override;
    
    // Pattern management
    void loadPattern(PatternID patternId);
    void savePattern();
    
    // View config
    void setPixelsPerBeat(float ppb);
    void setBeatsPerBar(int bpb);
    
private:
    void updateGhostChannels();

    std::shared_ptr<TrackManager> m_trackManager;
    std::shared_ptr<AestraUI::PianoRollView> m_pianoRoll;
    PatternID m_currentPatternId;
};

} // namespace Audio
} // namespace Aestra
