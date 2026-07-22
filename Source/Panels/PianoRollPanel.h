// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "../AestraUI/Widgets/NUIPianoRollWidgets.h"
#include "TrackManager.h"
#include "../Components/TimelineMinimapBar.h"
#include "../Components/TimelineSummaryCache.h"
#include "../AestraUI/Core/NUIComponent.h"
#include "NUIButton.h"
#include "Models/PatternSource.h"
#include <memory>
#include <functional>
#include <vector>

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
    /** @brief Unit currently bound for editing (0 = none) — musical typing target. */
    UnitID getEditingUnitId() const { return m_editingUnitId; }
    /**
     * @brief Set the callback fired after the current pattern is edited.
     * @param callback Pattern-edited callback.
     */
    void setOnPatternEdited(std::function<void(PatternID)> callback) { m_onPatternEdited = std::move(callback); }
    /**
     * @brief Set the callback fired when the editing unit is switched from the
     *        Piano Roll's own unit dropdown (so the rest of the app can follow).
     */
    void setOnEditingUnitChanged(std::function<void(UnitID)> callback) { m_onEditingUnitChanged = std::move(callback); }
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
    void setPlatformBridge(AestraUI::NUIPlatformBridge* bridge);
    bool handleKeyEvent(const AestraUI::NUIKeyEvent& event);
    
private:
    void adjustPatternLengthBars(int barsDelta);
    void rebuildPatternSwitcher();
    void rebuildUnitSwitcher();
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
    std::function<void(UnitID)> m_onEditingUnitChanged;
    double m_patternDurationBeats{8.0};

    // Undo/redo support
    std::vector<MidiNote> m_notesBeforeEdit; // Captured state before user edits
    // Current pattern's notes owned by other units: hidden from editing,
    // rendered as unit-colored ghosts, merged back verbatim on save.
    std::vector<MidiNote> m_otherUnitNotes;
    bool m_applyingUndoRedo{false};           // Guard flag to prevent re-entry
    bool m_switchingUnit{false};              // Guards setEditingUnit against save-echo recursion
    bool m_wasVisible{false};
    double m_lastPlayheadBeat{-1.0}; // Gates idle repaint; only redraw when the playhead actually moves
};

} // namespace Audio
} // namespace Aestra
