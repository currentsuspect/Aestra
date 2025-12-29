// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ISettingsPage.h"
#include "../NomadAudio/include/AudioDeviceManager.h"
#include "../NomadAudio/include/TrackManager.h"
#include "../NomadUI/Widgets/NUIButton.h"
#include "../NomadUI/Widgets/NUIDropdown.h"
#include "../NomadUI/Core/NUILabel.h"
#include "../NomadUI/Widgets/NUICoreWidgets.h" // For NUIToggle

#include "../NomadAudio/include/AudioEngine.h"

namespace Nomad {

// ============================================================================
// ThreadCountDisplay (Number Dragger)
// ============================================================================
class ThreadCountDisplay : public NomadUI::NUIComponent {
public:
    ThreadCountDisplay();
    ~ThreadCountDisplay() = default;

    void setValue(int val);
    int getValue() const { return m_value; }
    void setRange(int minVal, int maxVal);
    
    void setOnChange(std::function<void(int)> callback) { m_onChange = callback; }
    
    void onRender(NomadUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    void onUpdate(double deltaTime) override;

private:
    int m_value;
    int m_min;
    int m_max;
    std::function<void(int)> m_onChange;
    
    // Visual state
    bool m_isHovered{false};
    bool m_isDragging{false};
    NomadUI::NUIPoint m_dragStartPos;
    int m_dragStartValue;
    
    std::shared_ptr<NomadUI::NUIIcon> m_upArrow;
    std::shared_ptr<NomadUI::NUIIcon> m_downArrow;
    bool m_upArrowHovered{false};
    bool m_downArrowHovered{false};
    bool m_upArrowPressed{false};
    bool m_downArrowPressed{false};
    float m_holdTimer{0.0f};
    
    // Cached bounds
    NomadUI::NUIRect getUpArrowBounds() const;
    NomadUI::NUIRect getDownArrowBounds() const;
};

class AudioSettingsPage : public ISettingsPage {
public:
    AudioSettingsPage(Audio::AudioDeviceManager* audioManager, Audio::AudioEngine* engine);
    ~AudioSettingsPage() override = default;
    
    // Callbacks
    void setOnStreamRestore(std::function<void()> callback) { m_onStreamRestore = callback; }

    // Test Sound Access
    bool isPlayingTestSound() const { return m_isPlayingTestSound; }
    void setPlayingTestSound(bool playing) { m_isPlayingTestSound = playing; }
    double& getTestSoundPhase() { return m_testSoundPhase; }

    // ISettingsPage overrides
    std::string getPageID() const override { return "audio"; }
    std::string getTitle() const override { return "Audio"; }
    void applyChanges() override;
    void cancelChanges() override;
    bool hasUnsavedChanges() const override;
    void onShow() override;
    void onHide() override;

    // NUIComponent overrides
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;

private:
    void createUI();
    void layoutComponents();
    void updateDriverList();
    void updateDeviceList();
    void updateSampleRateList();
    void updateBufferSizeList();
    void updateLatencyEstimate();
    void loadCurrentSettings();
    
    // Persistence
    void saveSettings();
    void loadSettings();

    Audio::AudioDeviceManager* m_audioManager;
    Audio::AudioEngine* m_audioEngine;
    std::function<void()> m_onStreamRestore;
    
    // UI Elements
    std::shared_ptr<NomadUI::NUILabel> m_driverLabel;
    std::shared_ptr<NomadUI::NUIDropdown> m_driverDropdown;
    
    std::shared_ptr<NomadUI::NUILabel> m_deviceLabel;
    std::shared_ptr<NomadUI::NUIDropdown> m_deviceDropdown;
    
    std::shared_ptr<NomadUI::NUILabel> m_sampleRateLabel;
    std::shared_ptr<NomadUI::NUIDropdown> m_sampleRateDropdown;
    
    std::shared_ptr<NomadUI::NUILabel> m_bufferSizeLabel;
    std::shared_ptr<NomadUI::NUIDropdown> m_bufferSizeDropdown;
    
    std::shared_ptr<NomadUI::NUILabel> m_latencyLabel;
    
    // Quality Section
    std::shared_ptr<NomadUI::NUILabel> m_qualityPresetLabel;
    std::shared_ptr<NomadUI::NUIDropdown> m_qualityPresetDropdown;
    
    std::shared_ptr<NomadUI::NUILabel> m_resamplingLabel;
    std::shared_ptr<NomadUI::NUIDropdown> m_resamplingDropdown;
    
    std::shared_ptr<NomadUI::NUILabel> m_ditheringLabel;
    std::shared_ptr<NomadUI::NUIDropdown> m_ditheringDropdown;
    
    std::shared_ptr<NomadUI::NUILabel> m_dcRemovalLabel;
    std::shared_ptr<NomadUI::NUIToggle> m_dcRemovalToggle; // Toggle button
    
    std::shared_ptr<NomadUI::NUILabel> m_softClippingLabel;
    std::shared_ptr<NomadUI::NUIToggle> m_softClippingToggle;
    
    std::shared_ptr<NomadUI::NUILabel> m_multiThreadingLabel;
    std::shared_ptr<NomadUI::NUIToggle> m_multiThreadingToggle;
    
    std::shared_ptr<NomadUI::NUILabel> m_threadCountLabel;
    std::shared_ptr<ThreadCountDisplay> m_threadCountInput;

    std::shared_ptr<NomadUI::NUIButton> m_testSoundButton;

    // Test Sound State
    bool m_isPlayingTestSound;
    double m_testSoundPhase = 0.0;
    
    // Original state for revert
    struct AudioState {
        std::string driver;
        std::string device;
        int sampleRate;
        int bufferSize;
        int qualityPreset;
        int resamplingMode;
        int ditheringMode;
        bool dcRemoval;
        bool softClipping;
        bool multiThreading;
        int threadCount;
    } m_originalState;
    
    bool m_dirty;
};

} // namespace Nomad
