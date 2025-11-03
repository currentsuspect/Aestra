// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file AudioSettingsDialog.h
 * @brief Audio settings dialog for NOMAD DAW
 */

#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIButton.h"
#include "../NomadUI/Core/NUILabel.h"
#include "../NomadUI/Core/NUIIcon.h"
#include "../NomadUI/Core/NUISlider.h"
#include "../NomadUI/Widgets/NUIDropdown.h"
#include "../NomadAudio/include/AudioDeviceManager.h"
#include <memory>
#include <functional>
#include <vector>

namespace Nomad {

// Forward declarations
namespace Audio {
    class TrackManager;  // Not used anymore but keep for potential future use
    class Track;
}

/**
 * @brief Audio settings dialog
 * 
 * Provides UI for configuring audio device, sample rate, and buffer size.
 */
class AudioSettingsDialog : public NomadUI::NUIComponent {
public:
    AudioSettingsDialog(Nomad::Audio::AudioDeviceManager* audioManager, 
                       std::shared_ptr<Audio::TrackManager> trackManager = nullptr);
    ~AudioSettingsDialog() override = default;
    
    // Callbacks
    void setOnApply(std::function<void()> callback) { m_onApply = callback; }
    void setOnCancel(std::function<void()> callback) { m_onCancel = callback; }
    void setOnStreamRestore(std::function<void()> callback) { m_onStreamRestore = callback; }
    
    // Show/hide
    void show();
    void hide();
    bool isVisible() const { return m_visible; }
    
    // Get selected settings
    uint32_t getSelectedDeviceId() const { return m_selectedDeviceId; }
    uint32_t getSelectedSampleRate() const { return m_selectedSampleRate; }
    uint32_t getSelectedBufferSize() const { return m_selectedBufferSize; }
    
    // Test sound state (for audio callback)
    bool isPlayingTestSound() const { return m_isPlayingTestSound; }
    double& getTestSoundPhase() { return m_testSoundPhase; }
    
    // NUIComponent overrides
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const NomadUI::NUIKeyEvent& event) override;
    void setVisible(bool visible);
    
private:
    void createUI();
    void layoutComponents();
    void updateDriverList();
    void updateDeviceList();
    void updateSampleRateList();
    void updateBufferSizeList();
    void updateASIOInfo();
    void loadCurrentSettings();
    void applySettings();
    void cancelSettings();
    
    // Test sound functionality
    void playTestSound();
    void stopTestSound();
    
    // Rendering helpers
    void renderBackground(NomadUI::NUIRenderer& renderer);
    void renderDialog(NomadUI::NUIRenderer& renderer);
    
    Audio::AudioDeviceManager* m_audioManager;
    std::shared_ptr<Audio::TrackManager> m_trackManager;
    
    // UI state
    bool m_visible;
    NomadUI::NUIRect m_dialogBounds;
    NomadUI::NUIRect m_closeButtonBounds; // For click detection
    bool m_closeButtonHovered; // For hover effect
    float m_blinkAnimation; // For "can't close" blink effect
    std::string m_errorMessage; // Error message to display
    float m_errorMessageAlpha; // Fade out animation for error
    
    // Device list
    std::vector<Audio::AudioDeviceInfo> m_devices;
    uint32_t m_selectedDeviceId;
    
    // Driver list
    std::vector<Audio::AudioDriverType> m_drivers;
    Audio::AudioDriverType m_selectedDriverType;
    
    // ASIO drivers (for display)
    std::vector<Audio::ASIODriverInfo> m_asioDrivers;
    
    // Sample rate list
    std::vector<uint32_t> m_sampleRates;
    uint32_t m_selectedSampleRate;
    
    // Buffer size list
    std::vector<uint32_t> m_bufferSizes;
    uint32_t m_selectedBufferSize;
    
    // UI Components
    std::shared_ptr<NomadUI::NUIButton> m_applyButton;
    std::shared_ptr<NomadUI::NUIButton> m_cancelButton;
    std::shared_ptr<NomadUI::NUIButton> m_testSoundButton;
    std::shared_ptr<NomadUI::NUIIcon> m_playIcon;  // SVG play icon for test button
    std::shared_ptr<NomadUI::NUIDropdown> m_driverDropdown;
    std::shared_ptr<NomadUI::NUIDropdown> m_deviceDropdown;
    std::shared_ptr<NomadUI::NUIDropdown> m_sampleRateDropdown;
    std::shared_ptr<NomadUI::NUIDropdown> m_bufferSizeDropdown;
    
    // Audio Quality Settings
    std::shared_ptr<NomadUI::NUIDropdown> m_qualityPresetDropdown;
    std::shared_ptr<NomadUI::NUIDropdown> m_resamplingDropdown;
    std::shared_ptr<NomadUI::NUIDropdown> m_ditheringDropdown;
    std::shared_ptr<NomadUI::NUIDropdown> m_interpolationDropdown;  // Legacy
    std::shared_ptr<NomadUI::NUIButton> m_ditheringToggle;  // Legacy
    std::shared_ptr<NomadUI::NUIButton> m_dcRemovalToggle;
    std::shared_ptr<NomadUI::NUIButton> m_softClippingToggle;
    std::shared_ptr<NomadUI::NUIButton> m_precision64BitToggle;
    std::shared_ptr<NomadUI::NUIButton> m_multiThreadingToggle;
    std::shared_ptr<NomadUI::NUIDropdown> m_threadCountDropdown;
    std::shared_ptr<NomadUI::NUIDropdown> m_nomadModeDropdown;  // Nomad Mode toggle
    
    // Labels
    std::shared_ptr<NomadUI::NUILabel> m_driverLabel;
    std::shared_ptr<NomadUI::NUILabel> m_deviceLabel;
    std::shared_ptr<NomadUI::NUILabel> m_sampleRateLabel;
    std::shared_ptr<NomadUI::NUILabel> m_bufferSizeLabel;
    std::shared_ptr<NomadUI::NUILabel> m_asioInfoLabel;
    std::shared_ptr<NomadUI::NUILabel> m_qualitySectionLabel;
    std::shared_ptr<NomadUI::NUILabel> m_qualityPresetLabel;
    std::shared_ptr<NomadUI::NUILabel> m_resamplingLabel;
    std::shared_ptr<NomadUI::NUILabel> m_ditheringLabel;
    std::shared_ptr<NomadUI::NUILabel> m_interpolationLabel;  // Legacy
    std::shared_ptr<NomadUI::NUILabel> m_dcRemovalLabel;
    std::shared_ptr<NomadUI::NUILabel> m_softClippingLabel;
    std::shared_ptr<NomadUI::NUILabel> m_precision64BitLabel;
    std::shared_ptr<NomadUI::NUILabel> m_multiThreadingLabel;
    std::shared_ptr<NomadUI::NUILabel> m_threadCountLabel;
    std::shared_ptr<NomadUI::NUILabel> m_nomadModeLabel;  // Nomad Mode label
    
    // Callbacks
    std::function<void()> m_onApply;
    std::function<void()> m_onCancel;
    std::function<void()> m_onStreamRestore;
    
    // Original settings (for cancel)
    Audio::AudioDriverType m_originalDriverType;
    uint32_t m_originalDeviceId;
    uint32_t m_originalSampleRate;
    uint32_t m_originalBufferSize;
    
    // Test sound state (simple flag + phase, tone generated in audio callback)
    bool m_isPlayingTestSound;
    double m_testSoundPhase;
    static constexpr double TEST_FREQUENCY = 440.0; // A4 note
    
    // FPS optimization - cache dropdown open states to avoid unnecessary re-renders
    bool m_anyDropdownOpen;
    
    // Bug #5 fix: Track if we're blocking events due to dropdown interaction
    // Set to true when PRESSED event occurs while dropdown is open
    // Remains true until RELEASED event, preventing click-through to buttons
    bool m_blockingEventsForDropdown;
};

} // namespace Nomad
