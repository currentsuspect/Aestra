// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
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
    void setEditingUnit(UnitID unitId);
    void setOnPatternEdited(std::function<void(PatternID)> callback) { m_onPatternEdited = std::move(callback); }
    
    // View config
    void setPixelsPerBeat(float ppb);
    void setBeatsPerBar(int bpb);
    
private:
    void updateGhostChannels();

    std::shared_ptr<TrackManager> m_trackManager;
    std::shared_ptr<AestraUI::PianoRollView> m_pianoRoll;
    PatternID m_currentPatternId;
    UnitID m_editingUnitId{0};
    std::function<void(PatternID)> m_onPatternEdited;
};

} // namespace Audio
} // namespace Aestra
