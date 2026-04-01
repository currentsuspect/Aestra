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
    /**
     * @brief Create the piano-roll editor bound to the shared track manager.
     * @param trackManager Shared project track manager.
     */
    PianoRollPanel(std::shared_ptr<TrackManager> trackManager);
    ~PianoRollPanel() override = default;

    /**
     * @brief Advance panel playback state and sync the playhead/minimap.
     * @param deltaTime Frame delta in seconds.
     */
    void onUpdate(double deltaTime) override;
    /**
     * @brief Relayout the panel after a size change.
     * @param width New width in logical pixels.
     * @param height New height in logical pixels.
     */
    void onResize(int width, int height) override;
    
    /**
     * @brief Load a pattern into the piano-roll editor.
     * @param patternId Pattern identifier to edit.
     */
    void loadPattern(PatternID patternId);
    /**
     * @brief Persist the currently edited notes back to the pattern manager.
     */
    void savePattern();
    /**
     * @brief Set the default unit assigned to newly created notes.
     * @param unitId Unit identifier used for new note creation.
     */
    void setEditingUnit(UnitID unitId);
    /**
     * @brief Set the callback fired after the current pattern is edited.
     * @param callback Pattern-edited callback.
     */
    void setOnPatternEdited(std::function<void(PatternID)> callback) { m_onPatternEdited = std::move(callback); }
    /**
     * @brief Get the currently loaded pattern.
     * @return Active pattern identifier.
     */
    PatternID getCurrentPatternId() const { return m_currentPatternId; }
    
    /**
     * @brief Set the horizontal zoom level in pixels per beat.
     * @param ppb Horizontal scale used by the piano roll.
     */
    void setPixelsPerBeat(float ppb);
    /**
     * @brief Set the bar signature used by the ruler.
     * @param bpb Beats per bar.
     */
    void setBeatsPerBar(int bpb);
    /**
     * @brief Bind the live audio engine used for transport/playhead sync.
     * @param engine Audio engine pointer, or nullptr to disable engine sync.
     */
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
