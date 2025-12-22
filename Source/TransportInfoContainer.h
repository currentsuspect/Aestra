// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file TransportInfoContainer.h
 * @brief Container for BPM and Timer display components
 * 
 * Houses modular BPM and Timer components with proper vertical alignment.
 * 
 * @version 1.0.0
 * @license Proprietary
 */

#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUILabel.h"
#include "../NomadUI/Core/NUIIcon.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"

#include <memory>
#include <functional>

namespace Nomad {

/**
 * @brief BPM Display Component
 * 
 * Shows current BPM with smooth scrolling animation when changed.
 * Includes arrow controls for adjusting BPM inline.
 */
class BPMDisplay : public NomadUI::NUIComponent {
public:
    BPMDisplay();
    ~BPMDisplay() = default;

    void setBPM(float bpm);
    float getBPM() const { return m_currentBPM; }
    
    // BPM adjustment
    void incrementBPM(float amount);
    void decrementBPM(float amount);
    
    // Callback when BPM changes via arrows
    void setOnBPMChange(std::function<void(float)> callback) { m_onBPMChange = callback; }
    
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;

private:
    float m_currentBPM;
    float m_targetBPM;
    float m_displayBPM; // For smooth scrolling animation
    std::shared_ptr<NomadUI::NUIIcon> m_upArrow;
    std::shared_ptr<NomadUI::NUIIcon> m_downArrow;
    
    std::function<void(float)> m_onBPMChange;
    
    // Visual feedback state
    bool m_upArrowHovered;
    bool m_downArrowHovered;
    bool m_upArrowPressed;
    bool m_downArrowPressed;
    bool m_isHovered;           // Component hover for glow
    float m_pulseAnimation;      // Flash on BPM change (0-1)
    float m_holdTimer;           // For continuous increment when holding
    float m_holdDelay;           // Initial delay before repeat
    
    NomadUI::NUIRect getUpArrowBounds() const;
    NomadUI::NUIRect getDownArrowBounds() const;
};

/**
 * @brief Time Signature Display Component
 * 
 * Shows current time signature (e.g., 4/4) with click-to-cycle functionality.
 */
class TimeSignatureDisplay : public NomadUI::NUIComponent {
public:
    TimeSignatureDisplay();
    ~TimeSignatureDisplay() = default;

    void setBeatsPerBar(int beats) { m_beatsPerBar = beats; setDirty(true); }
    int getBeatsPerBar() const { return m_beatsPerBar; }
    
    // Cycle to next time signature (2/4 -> 3/4 -> 4/4 -> 5/4 -> 6/8 -> 7/8 -> 2/4...)
    void cycleNext();
    
    // Callback when time signature changes
    void setOnTimeSignatureChange(std::function<void(int)> callback) { m_onTimeSignatureChange = callback; }
    
    void onRender(NomadUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;

private:
    int m_beatsPerBar{4};
    bool m_isHovered{false};
    std::function<void(int)> m_onTimeSignatureChange;
    
    std::string getDisplayText() const;
};

/**
 * @brief Timer Display Component
 * 
 * Shows current playback position in MM:SS:MS format.
 */
class TimerDisplay : public NomadUI::NUIComponent {
public:
    TimerDisplay();
    ~TimerDisplay() = default;

    void setTime(double seconds);
    double getTime() const { return m_currentTime; }
    
    // Set playing state to change color
    void setPlaying(bool playing) { m_isPlaying = playing; }
    bool isPlaying() const { return m_isPlaying; }
    
    void onRender(NomadUI::NUIRenderer& renderer) override;

private:
    double m_currentTime;
    bool m_isPlaying;
    
    std::string formatTime(double seconds) const;
};

/**
 * @brief Transport Info Container
 * 
 * Parent container that houses BPM and Timer displays with proper alignment.
 * Maintains their visual position while providing modular structure.
 */
class TransportInfoContainer : public NomadUI::NUIComponent {
public:
    TransportInfoContainer();
    ~TransportInfoContainer() = default;

    // Access to child components
    BPMDisplay* getBPMDisplay() const { return m_bpmDisplay.get(); }
    TimerDisplay* getTimerDisplay() const { return m_timerDisplay.get(); }
    TimeSignatureDisplay* getTimeSignatureDisplay() const { return m_timeSignatureDisplay.get(); }
    
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;

private:
    std::shared_ptr<BPMDisplay> m_bpmDisplay;
    std::shared_ptr<TimerDisplay> m_timerDisplay;
    std::shared_ptr<TimeSignatureDisplay> m_timeSignatureDisplay;
    
    void layoutComponents();
};

} // namespace Nomad
