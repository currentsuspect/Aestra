// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "../AestraUI/Widgets/NUIPianoRollWidgets.h"
#include "TrackManager.h"
#include "../Components/TimelineMinimapBar.h"
#include "../Components/TimelineSummaryCache.h"
#include "../AestraUI/Core/NUIComponent.h"
#include "NUIButton.h"
#include <memory>
#include <functional>

namespace Aestra {
namespace Audio {
class AudioEngine;

/**
 * @brief Piano Roll Panel - MIDI editor with piano keyboard
 */
class PianoRollPanel : public WindowPanel {
public:
    PianoRollPanel(std::shared_ptr<TrackManager> trackManager);
    ~PianoRollPanel() override = default;

    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    
    // Pattern management
    void loadPattern(PatternID patternId);
    void savePattern();
    void setEditingUnit(UnitID unitId);
    void setOnPatternEdited(std::function<void(PatternID)> callback) { m_onPatternEdited = std::move(callback); }
    PatternID getCurrentPatternId() const { return m_currentPatternId; }
    
    // View config
    void setPixelsPerBeat(float ppb);
    void setBeatsPerBar(int bpb);
    void setAudioEngine(AudioEngine* engine) { m_audioEngine = engine; }
    
private:
    void updateGhostChannels();
    void rebuildTimelineMinimap();
    void layoutTimelineMinimap();

    std::shared_ptr<TrackManager> m_trackManager;
    AudioEngine* m_audioEngine{nullptr};
    std::shared_ptr<AestraUI::PianoRollView> m_pianoRoll;
    std::shared_ptr<AestraUI::TimelineMinimapBar> m_timelineMinimap;
    AestraUI::TimelineSummaryCache m_timelineSummaryCache;
    AestraUI::TimelineSummarySnapshot m_timelineSummarySnapshot;
    PatternID m_currentPatternId;
    UnitID m_editingUnitId{0};
    std::function<void(PatternID)> m_onPatternEdited;
    double m_patternDurationBeats{4.0};
};

} // namespace Audio
} // namespace Aestra
