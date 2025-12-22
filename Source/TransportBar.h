// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file TransportBar.h
 * @brief Transport bar component for NOMAD DAW
 * 
 * Provides playback controls, tempo display, and position tracking.
 * 
 * @version 1.0.0
 * @license Proprietary
 */

#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Widgets/NUIButton.h"
#include "../NomadUI/Widgets/NUIDropdown.h"
#include "../NomadUI/Common/MusicHelpers.h"
#include "../NomadUI/Core/NUILabel.h"
#include "../NomadUI/Core/NUIIcon.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "ViewTypes.h"
#include "TransportInfoContainer.h"

#include <memory>
#include <functional>
#include <string>

namespace Nomad {

/**
 * @brief Transport state
 */
enum class TransportState {
    Stopped,
    Playing,
    Paused,
    Recording
};

/**
 * @brief Transport bar component
 * 
 * Provides playback controls and displays transport information.
 */
class TransportBar : public NomadUI::NUIComponent {
public:
    TransportBar();
    ~TransportBar() = default;

    // Transport control
    void play();
    void pause();
    void stop();
    void togglePlayPause();
    
    TransportState getState() const { return m_state; }
    
    // Tempo control
    void setTempo(float bpm);
    float getTempo() const { return m_tempo; }
    
    // Position control
    void setPosition(double seconds);
    double getPosition() const { return m_position; }
    
    // Callbacks
    void setOnPlay(std::function<void()> callback) { m_onPlay = callback; }
    void setOnPause(std::function<void()> callback) { m_onPause = callback; }
    void setOnStop(std::function<void()> callback) { m_onStop = callback; }
    void setOnTempoChange(std::function<void(float)> callback) { m_onTempoChange = callback; }
    void setOnMetronomeToggle(std::function<void(bool)> callback) { m_onMetronomeToggle = callback; }
    void setOnTimeSignatureChange(std::function<void(int)> callback) { m_onTimeSignatureChange = callback; }
    void setMetronomeActive(bool active) { m_metronomeActive = active; setDirty(true); }
    void setTimeSignature(int beatsPerBar) { m_beatsPerBar = beatsPerBar; setDirty(true); }
    int getTimeSignature() const { return m_beatsPerBar; }
    
    // View Toggle Callbacks
    void setOnToggleView(std::function<void(Audio::ViewType)> callback) { m_onToggleView = callback; }
    
    // Access to info container for time signature callback wiring
    TransportInfoContainer* getInfoContainer() const { return m_infoContainer.get(); }
    
    // Push state from authority
    void setViewToggled(Audio::ViewType view, bool active);

    // Global Tool & Scale Callbacks


    // Component overrides
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;

private:
    // UI Components
    std::shared_ptr<NomadUI::NUIButton> m_playButton;
    std::shared_ptr<NomadUI::NUIButton> m_stopButton;
    std::shared_ptr<NomadUI::NUIButton> m_recordButton;
    std::shared_ptr<NomadUI::NUIButton> m_metronomeButton;
    


    // View Toggle Buttons
    std::shared_ptr<NomadUI::NUIButton> m_mixerButton;
    std::shared_ptr<NomadUI::NUIButton> m_sequencerButton;
    std::shared_ptr<NomadUI::NUIButton> m_pianoRollButton;
    std::shared_ptr<NomadUI::NUIButton> m_playlistButton;

    std::shared_ptr<TransportInfoContainer> m_infoContainer;  // Modular info container
    
    // Icons
    std::shared_ptr<NomadUI::NUIIcon> m_playIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_pauseIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_stopIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_recordIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_metronomeIcon;


    
    // View Icons
    std::shared_ptr<NomadUI::NUIIcon> m_mixerIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_sequencerIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_pianoRollIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_playlistIcon;
    
    std::function<void()> m_onPlay;
    std::function<void()> m_onPause;
    std::function<void()> m_onStop;
    std::function<void(float)> m_onTempoChange;
    std::function<void(bool)> m_onMetronomeToggle;
    std::function<void(int)> m_onTimeSignatureChange;
    
    // Tool/Scale Callbacks
    std::function<void(NomadUI::GlobalTool)> m_onToolChanged;
    std::function<void(int, NomadUI::ScaleType)> m_onScaleChanged;
    
    // View Toggle Callbacks
    std::function<void(Audio::ViewType)> m_onToggleView;
    
    // Internal state
    TransportState m_state;
    float m_tempo;
    double m_position;

    // View Toggles state
    bool m_mixerActive{false};
    bool m_sequencerActive{false};
    bool m_pianoRollActive{false};
    bool m_playlistActive{true}; // Always on by default
    bool m_metronomeActive{false};
    int m_beatsPerBar{4};  // Time signature numerator (4 for 4/4)
    
    void createIcons();
    void createButtons();
    void updateButtonStates();
    void layoutComponents();
    void renderButtonIcons(NomadUI::NUIRenderer& renderer);
};

} // namespace Nomad
