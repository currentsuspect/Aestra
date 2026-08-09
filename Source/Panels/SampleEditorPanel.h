// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "NUIComponent.h"
#include "NUIButton.h"
#include "NUISlider.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace AestraUI {
class NUILabel;
}

namespace Aestra {
namespace Audio {

class TrackManager;
class WaveformDisplayComponent;
class ADSRDisplayComponent;

/**
 * @brief Sample Editor panel for editing loaded audio samples.
 *
 * Features:
 * - Waveform display with horizontal zoom
 * - ADSR envelope (Attack, Decay, Sustain, Release) with visual curve display
 * - Loop points (start/end) with mode toggle: one-shot, loop, ping-pong
 * - Keyboard pitch controls (root note, coarse ±24 st, fine ±100 cents)
 * - Normalize and Reverse buttons
 */
class SampleEditorPanel : public WindowPanel {
public:
    enum class LoopMode { OneShot, Loop, PingPong };

    SampleEditorPanel(std::shared_ptr<TrackManager> trackManager);
    ~SampleEditorPanel() override = default;

    // Load a sample for editing
    void loadSample(const std::string& path);
    void loadPreparedSample(const std::string& path, double sampleRate, uint32_t sampleLength,
                            std::vector<float> waveformData);

    // ADSR parameters (0-1 normalized)
    struct ADSRParams {
        float attack{0.01f};   // 0.001s - 2.0s
        float decay{0.1f};     // 0.001s - 2.0s
        float sustain{1.0f};   // 0.0 - 1.0
        float release{0.1f};   // 0.001s - 5.0s
    };

    void setADSR(const ADSRParams& params);
    ADSRParams getADSR() const { return m_adsr; }

    // Loop points (normalized 0.0 - 1.0)
    struct LoopPoints {
        float start{0.0f};
        float end{1.0f};
        LoopMode mode{LoopMode::OneShot};
    };

    void setLoopPoints(const LoopPoints& lp);
    LoopPoints getLoopPoints() const { return m_loopPoints; }

    // Pitch/tune
    struct PitchTune {
        int rootMidiNote{60}; // C3 default
        int coarse{0};        // ±24 semitones
        float fine{0.0f}; // ±100 cents
    };

    void setPitchTune(const PitchTune& pt);
    PitchTune getPitchTune() const { return m_pitchTune; }
    void setVoiceCount(int voices);
    int getVoiceCount() const;
    void setMonoMode(bool mono);
    bool isMonoMode() const { return m_monoMode; }

    // Normalize and Reverse
    void normalize();
    void reverse();

    // Callbacks
    std::function<void(const ADSRParams&)> onADSRChanged;
    std::function<void(const LoopPoints&)> onLoopPointsChanged;
    std::function<void(const PitchTune&)> onPitchTuneChanged;
    std::function<void(int)> onVoiceCountChanged;
    std::function<void(bool)> onMonoModeChanged;
    std::function<void()> onControlCommitRequested;
    std::function<void()> onNormalizeRequested;
    std::function<void()> onReverseRequested;
    std::function<void()> onSampleModified;

    void onResize(int width, int height) override;
    AestraUI::NUICursorStyle getResizeCursorStyleForPoint(const AestraUI::NUIPoint& point) const;

private:
    std::shared_ptr<TrackManager> m_trackManager;

    // Waveform data
    std::vector<float> m_waveformData; // Min/max interleaved pairs
    double m_sampleRate{44100.0};
    uint32_t m_sampleLength{0}; // Total frames
    float m_waveformZoom{1.0f}; // Horizontal zoom factor

    // ADSR
    ADSRParams m_adsr;

    // Loop
    LoopPoints m_loopPoints;
    bool m_monoMode{false};

    // Pitch/Tune
    PitchTune m_pitchTune;

    // UI Components
    std::shared_ptr<WaveformDisplayComponent> m_waveformDisplay;
    std::shared_ptr<ADSRDisplayComponent> m_adsrDisplay;

    std::shared_ptr<AestraUI::NUISlider> m_pitchCoarseSlider;
    std::shared_ptr<AestraUI::NUISlider> m_pitchFineSlider;
    std::shared_ptr<AestraUI::NUISlider> m_pitchRootSlider;
    std::shared_ptr<AestraUI::NUISlider> m_voiceCountSlider;

    std::shared_ptr<AestraUI::NUIButton> m_normalizeBtn;
    std::shared_ptr<AestraUI::NUIButton> m_reverseBtn;
    std::shared_ptr<AestraUI::NUIButton> m_oneShotModeBtn;
    std::shared_ptr<AestraUI::NUIButton> m_loopModeBtn;
    std::shared_ptr<AestraUI::NUIButton> m_pingPongModeBtn;
    std::shared_ptr<AestraUI::NUIButton> m_monoModeBtn;
    std::shared_ptr<AestraUI::NUIButton> m_polyModeBtn;
    std::shared_ptr<AestraUI::NUILabel> m_waveformHintLabel;
    std::shared_ptr<AestraUI::NUILabel> m_modeLabel;
    std::shared_ptr<AestraUI::NUILabel> m_voiceCountLabel;
    std::shared_ptr<AestraUI::NUILabel> m_voiceCountValueLabel;
    std::shared_ptr<AestraUI::NUILabel> m_pitchLabel;
    std::shared_ptr<AestraUI::NUILabel> m_pitchRootLabel;
    std::shared_ptr<AestraUI::NUILabel> m_pitchRootValueLabel;
    std::shared_ptr<AestraUI::NUILabel> m_pitchCoarseLabel;
    std::shared_ptr<AestraUI::NUILabel> m_pitchFineLabel;
    std::shared_ptr<AestraUI::NUILabel> m_adsrLabel;

    // Layout container
    std::shared_ptr<AestraUI::NUIComponent> m_contentContainer;
    bool m_suppressControlCallbacks{false};

    void buildUI();
    void requestControlCommit();
    void onWaveformZoomChanged(float zoom);
    void onADSRDisplayChanged(const ADSRParams& params);
    void onLoopControlChanged();
    void onPitchControlChanged();
    void onVoiceCountControlChanged();
    void setMonoModeInternal(bool mono, bool notify);
    void setLoopMode(LoopMode mode);
    void updateModeButtons();
    void updateMonoPolyControls();
};

/**
 * @brief Waveform display component with zoom and loop point overlays.
 */
class WaveformDisplayComponent : public AestraUI::NUIComponent {
public:
    WaveformDisplayComponent();

    void setWaveformData(const std::vector<float>& data);
    void setZoom(float zoom);
    void setLoopPoints(float start, float end);
    void setScrollOffset(float offset);

    float getScrollOffset() const { return m_scrollOffset; }
    float getZoom() const { return m_zoom; }

    std::function<void(float)> onZoomChanged;
    std::function<void(float)> onScrollChanged;
    std::function<void(float, float)> onLoopDragStarted; // start or end handle
    std::function<void(float, float)> onLoopDragged;     // new position

    void onRender(AestraUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;

private:
    std::vector<float> m_waveformData; // Min/max pairs
    float m_zoom{1.0f};
    float m_scrollOffset{0.0f}; // 0.0 - 1.0
    float m_loopStart{0.0f};
    float m_loopEnd{1.0f};

    // Dragging state
    bool m_draggingLoopStart{false};
    bool m_draggingLoopEnd{false};
    bool m_draggingViewport{false};
    float m_dragStartX{0.0f};
    float m_dragStartScroll{0.0f};
};

/**
 * @brief ADSR envelope visualization component.
 */
class ADSRDisplayComponent : public AestraUI::NUIComponent {
public:
    ADSRDisplayComponent();

    void setADSR(float attack, float decay, float sustain, float release);

    std::function<void(float, float, float, float)> onADSRChanged;
    std::function<void()> onADSRCommitRequested;

    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    void onMouseLeave() override;
    AestraUI::NUICursorStyle getCursorStyleForPoint(const AestraUI::NUIPoint& point) const;

private:
    enum class Handle {
        None,
        Attack,
        Decay,
        Sustain,
        Release
    };

    float m_attack{0.01f};
    float m_decay{0.1f};
    float m_sustain{1.0f};
    float m_release{0.1f};
    Handle m_hoveredHandle{Handle::None};
    Handle m_draggingHandle{Handle::None};
    float m_hoverPulseTime{0.0f};
    AestraUI::NUIPoint m_dragStartMouse;
    float m_dragStartAttack{0.01f};
    float m_dragStartDecay{0.1f};
    float m_dragStartSustain{1.0f};
    float m_dragStartRelease{0.1f};

    Handle getHandleAtPoint(const AestraUI::NUIPoint& point) const;
};

} // namespace Audio
} // namespace Aestra
