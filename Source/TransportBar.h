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
#include "../NomadUI/Core/NUIButton.h"
#include "../NomadUI/Core/NUILabel.h"
#include "../NomadUI/Core/NUIIcon.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
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
    
    // View Toggle Callbacks
    void setOnToggleMixer(std::function<void()> callback) { m_onToggleMixer = callback; }
    void setOnToggleSequencer(std::function<void()> callback) { m_onToggleSequencer = callback; }
    void setOnTogglePianoRoll(std::function<void()> callback) { m_onTogglePianoRoll = callback; }
    void setOnTogglePlaylist(std::function<void()> callback) { m_onTogglePlaylist = callback; }

    // Component overrides
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;

private:
    // UI Components
    std::shared_ptr<NomadUI::NUIButton> m_playButton;
    std::shared_ptr<NomadUI::NUIButton> m_stopButton;
    std::shared_ptr<NomadUI::NUIButton> m_recordButton;
    
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
    
    // View Icons
    std::shared_ptr<NomadUI::NUIIcon> m_mixerIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_sequencerIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_pianoRollIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_playlistIcon;
    
    // Callbacks
    std::function<void()> m_onPlay;
    std::function<void()> m_onPause;
    std::function<void()> m_onStop;
    std::function<void(float)> m_onTempoChange;
    
    // View Toggle Callbacks
    std::function<void()> m_onToggleMixer;
    std::function<void()> m_onToggleSequencer;
    std::function<void()> m_onTogglePianoRoll;
    std::function<void()> m_onTogglePlaylist;
    
    // Internal state
    TransportState m_state;
    float m_tempo;
    double m_position;
    
    void createIcons();
    void createButtons();
    void updateButtonStates();
    void layoutComponents();
    void renderButtonIcons(NomadUI::NUIRenderer& renderer);
};

} // namespace Nomad
