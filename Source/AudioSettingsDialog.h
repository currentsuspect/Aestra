/**
 * @file AudioSettingsDialog.h
 * @brief Audio settings dialog for NOMAD DAW
 */

#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIButton.h"
#include "../NomadUI/Core/NUILabel.h"
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
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const NomadUI::NUIKeyEvent& event) override;
    void setVisible(bool visible);
    
private:
    void createUI();
    void layoutComponents();
    void updateDeviceList();
    void updateSampleRateList();
    void updateBufferSizeList();
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
    
    // Device list
    std::vector<Audio::AudioDeviceInfo> m_devices;
    uint32_t m_selectedDeviceId;
    
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
    std::shared_ptr<NomadUI::NUIDropdown> m_deviceDropdown;
    std::shared_ptr<NomadUI::NUIDropdown> m_sampleRateDropdown;
    std::shared_ptr<NomadUI::NUIDropdown> m_bufferSizeDropdown;
    
    // Labels
    std::shared_ptr<NomadUI::NUILabel> m_deviceLabel;
    std::shared_ptr<NomadUI::NUILabel> m_sampleRateLabel;
    std::shared_ptr<NomadUI::NUILabel> m_bufferSizeLabel;
    
    // Callbacks
    std::function<void()> m_onApply;
    std::function<void()> m_onCancel;
    std::function<void()> m_onStreamRestore;
    
    // Original settings (for cancel)
    uint32_t m_originalDeviceId;
    uint32_t m_originalSampleRate;
    uint32_t m_originalBufferSize;
    
    // Test sound state (simple flag + phase, tone generated in audio callback)
    bool m_isPlayingTestSound;
    double m_testSoundPhase;
    static constexpr double TEST_FREQUENCY = 440.0; // A4 note
};

} // namespace Nomad
