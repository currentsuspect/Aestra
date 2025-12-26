// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file AudioSettingsDialog.cpp
 * @brief Audio settings dialog for NOMAD DAW
 */

#include "AudioSettingsDialog.h"
#include "../NomadUI/Widgets/NUIButton.h"
#include "../NomadUI/Widgets/NUICoreWidgets.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"
#include "../NomadAudio/include/AudioDriverTypes.h"
#include "../NomadAudio/include/TrackManager.h"
#include "../NomadAudio/include/MixerChannel.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace {
constexpr const char* kTestSoundPlaySvg = R"(
    <svg viewBox="0 0 24 24" fill="currentColor">
        <path d="M8 5v14l11-7z"/>
    </svg>
)";

constexpr const char* kTestSoundStopSvg = R"(
    <svg viewBox="0 0 24 24" fill="currentColor">
        <path d="M7 7h10v10H7z"/>
    </svg>
)";
} // namespace

namespace Nomad {

AudioSettingsDialog::AudioSettingsDialog(Audio::AudioDeviceManager* audioManager,
                                        std::shared_ptr<Audio::TrackManager> trackManager)
    : m_audioManager(audioManager)
    , m_trackManager(trackManager)
    , m_visible(false)
    , m_dialogBounds(0, 0, 950, 450)  // Larger, more spacious dialog
    , m_closeButtonHovered(false)
    , m_blinkAnimation(0.0f)
    , m_errorMessage("")
    , m_errorMessageAlpha(0.0f)
    , m_selectedDeviceId(0)
    , m_selectedSampleRate(48000)
    , m_selectedBufferSize(128)
    , m_originalDeviceId(0)
    , m_originalSampleRate(48000)
    , m_originalBufferSize(128)
    , m_isPlayingTestSound(false)
    , m_testSoundPhase(0.0)
    , m_anyDropdownOpen(false)
    , m_blockingEventsForDropdown(false)
    , m_cachedRender(nullptr)
    , m_cacheId(reinterpret_cast<uint64_t>(this))  // Use 'this' pointer as unique ID
    , m_cacheInvalidated(true)
    , m_isRenderingToCache(false)
    , m_activeTab("settings")
{
    createUI();
    loadCurrentSettings();
}

void AudioSettingsDialog::createUI() {
    // Tab bar (segmented control style)
    m_tabBar = std::make_shared<NomadUI::NUITabBar>();
    m_tabBar->addTab({"settings", "Settings", false});
    m_tabBar->addTab({"info", "Info", false});
    m_tabBar->setActiveTab(m_activeTab);
    m_tabBar->setOnTabChanged([this](const std::string& id) {
        m_activeTab = id;
        layoutComponents();
    });
    addChild(m_tabBar);
    
    // Create info tab content
    m_infoTitle = std::make_shared<NomadUI::NUILabel>();
    m_infoTitle->setText("Audio Settings Information");
    addChild(m_infoTitle);
    
    m_infoContent = std::make_shared<NomadUI::NUILabel>();
    m_infoContent->setText(
        "Quality Presets:\n\n"
        "• Economy - Minimal CPU usage, suitable for tracking\n"
        "• Balanced - Recommended for most projects\n"
        "• High-Fidelity - Better quality, higher CPU\n"
        "• Mastering - Maximum quality for final export\n\n"
        "Resampling Quality:\n\n"
        "Controls interpolation when changing playback speed or pitch.\n"
        "Higher quality = better sound but more CPU usage.\n\n"
        "Dithering:\n\n"
        "Adds controlled noise to reduce quantization artifacts.\n"
        "Use Triangular or Noise-Shaped for best results.\n\n"
        "Multi-Threading:\n\n"
        "Enables parallel processing of tracks. Recommended to use\n"
        "hardware threads - 1 for optimal performance.\n\n"
        "Nomad Mode:\n\n"
        "• Off - Clean bypass\n"
        "• Transparent - Reference-grade precision\n"
        "• Euphoric - Warm analog character with harmonic richness"
    );
    m_infoContent->setMultiline(true);
    m_infoContent->setWordWrap(true);
    addChild(m_infoContent);
    
    // Create labels
    m_driverLabel = std::make_shared<NomadUI::NUILabel>();
    m_driverLabel->setText("Audio Driver:");
    addChild(m_driverLabel);

    m_deviceLabel = std::make_shared<NomadUI::NUILabel>();
    m_deviceLabel->setText("Audio Device:");
    addChild(m_deviceLabel);

    m_sampleRateLabel = std::make_shared<NomadUI::NUILabel>();
    m_sampleRateLabel->setText("Sample Rate:");
    addChild(m_sampleRateLabel);

    m_bufferSizeLabel = std::make_shared<NomadUI::NUILabel>();
    m_bufferSizeLabel->setText("Buffer Size:");
    addChild(m_bufferSizeLabel);

    m_asioInfoLabel = std::make_shared<NomadUI::NUILabel>();
    m_asioInfoLabel->setText("");
    addChild(m_asioInfoLabel);

    // Create dropdowns
    m_driverDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_driverDropdown->setPlaceholderText("Select Audio Driver");
    m_driverDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        if (m_suppressDirtyStateUpdates) return;
        Nomad::Log::info("Driver dropdown changed: index=" + std::to_string(index) + 
                        ", value=" + std::to_string(value) + ", text=" + text);
        m_selectedDriverType = static_cast<Audio::AudioDriverType>(value);
        Nomad::Log::info("m_selectedDriverType now = " + std::to_string(static_cast<int>(m_selectedDriverType)));
        markSettingsChanged();
    });
    addChild(m_driverDropdown);

    m_deviceDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_deviceDropdown->setPlaceholderText("Select Audio Device");
    m_deviceDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        if (m_suppressDirtyStateUpdates) return;
        m_selectedDeviceId = static_cast<uint32_t>(value);
        markSettingsChanged();
    });
    addChild(m_deviceDropdown);

    m_sampleRateDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_sampleRateDropdown->setPlaceholderText("Select Sample Rate");
    m_sampleRateDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        if (m_suppressDirtyStateUpdates) return;
        m_selectedSampleRate = static_cast<uint32_t>(value);
        updateLatencyEstimate();
        markSettingsChanged();
    });
    addChild(m_sampleRateDropdown);

    m_bufferSizeDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_bufferSizeDropdown->setPlaceholderText("Select Buffer Size");
    m_bufferSizeDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        if (m_suppressDirtyStateUpdates) return;
        m_selectedBufferSize = static_cast<uint32_t>(value);
        updateLatencyEstimate();
        markSettingsChanged();
    });
    addChild(m_bufferSizeDropdown);

    // Estimated latency helper text (computed from buffer size + sample rate)
    m_latencyLabel = std::make_shared<NomadUI::NUILabel>();
    m_latencyLabel->setText("");
    m_latencyLabel->setFontSize(11.0f);
    m_latencyLabel->setTextColor(NomadUI::NUIThemeManager::getInstance().getColor("textSecondary").withAlpha(0.75f));
    addChild(m_latencyLabel);
    
    // === Audio Quality Settings Section ===
    m_qualitySectionLabel = std::make_shared<NomadUI::NUILabel>();
    m_qualitySectionLabel->setText("Audio Quality:");
    addChild(m_qualitySectionLabel);
    
    // Quality Preset dropdown
    m_qualityPresetLabel = std::make_shared<NomadUI::NUILabel>();
    m_qualityPresetLabel->setText("Quality Preset:");
    addChild(m_qualityPresetLabel);
    
    m_qualityPresetDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_qualityPresetDropdown->setPlaceholderText("Select Quality Preset");
    m_qualityPresetDropdown->addItem("Economy (Low CPU)", static_cast<int>(Audio::QualityPreset::Economy));
    m_qualityPresetDropdown->addItem("Balanced (Rec.)", static_cast<int>(Audio::QualityPreset::Balanced));
    m_qualityPresetDropdown->addItem("High-Fidelity", static_cast<int>(Audio::QualityPreset::HighFidelity));
    m_qualityPresetDropdown->addItem("Mastering (Max)", static_cast<int>(Audio::QualityPreset::Mastering));
    m_qualityPresetDropdown->addItem("Custom", static_cast<int>(Audio::QualityPreset::Custom));
    m_qualityPresetDropdown->setSelectedIndex(1); // Default to Balanced
    m_qualityPresetDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        // Auto-configure advanced settings based on preset
        auto preset = static_cast<Audio::QualityPreset>(value);
        if (preset != Audio::QualityPreset::Custom) {
            Audio::AudioQualitySettings settings;
            settings.applyPreset(preset);
            
            // Update UI to match preset
            m_isApplyingQualityPreset = true;
            m_resamplingDropdown->setSelectedIndex(static_cast<int>(settings.resampling));
            m_ditheringDropdown->setSelectedIndex(static_cast<int>(settings.dithering));
            m_isApplyingQualityPreset = false;
            m_dcRemovalToggle->setText(settings.removeDCOffset ? "ON" : "OFF");
            m_softClippingToggle->setText(settings.enableSoftClipping ? "ON" : "OFF");
        }
        markSettingsChanged();
    });
    addChild(m_qualityPresetDropdown);
    
    // Resampling quality dropdown
    m_resamplingLabel = std::make_shared<NomadUI::NUILabel>();
    m_resamplingLabel->setText("Resampling:");
    addChild(m_resamplingLabel);
    
    m_resamplingDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_resamplingDropdown->setPlaceholderText("Select Resampling Mode");
    m_resamplingDropdown->addItem("Fast (Linear 2pt)", static_cast<int>(Audio::ResamplingMode::Fast));
    m_resamplingDropdown->addItem("Medium (Cubic 4pt)", static_cast<int>(Audio::ResamplingMode::Medium));
    m_resamplingDropdown->addItem("High (Sinc 8pt)", static_cast<int>(Audio::ResamplingMode::High));
    m_resamplingDropdown->addItem("Ultra (Sinc 16pt)", static_cast<int>(Audio::ResamplingMode::Ultra));
    m_resamplingDropdown->addItem("Extreme (Sinc 64pt)", static_cast<int>(Audio::ResamplingMode::Extreme));
    m_resamplingDropdown->addItem("Perfect (512pt) - OFFLINE ONLY", static_cast<int>(Audio::ResamplingMode::Perfect));
    m_resamplingDropdown->setSelectedIndex(1); // Default to Medium
    m_resamplingDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        if (m_isApplyingQualityPreset || m_suppressDirtyStateUpdates) {
            return;
        }

        // Switch to Custom preset when manually changing settings
        m_qualityPresetDropdown->setSelectedIndex(4); // Custom
        
        // Warn about Perfect mode CPU usage
        auto mode = static_cast<Audio::ResamplingMode>(value);
        if (mode == Audio::ResamplingMode::Perfect) {
            Nomad::Log::warning("Perfect mode (512pt) is EXTREMELY CPU intensive!");
            Nomad::Log::warning("   Recommended ONLY for offline rendering/export.");
            Nomad::Log::warning("   Real-time playback may stutter or drop out.");
            Nomad::Log::warning("   Use Extreme (64pt) for real-time mastering.");
        } else if (mode == Audio::ResamplingMode::Extreme) {
            Nomad::Log::info("Extreme mode (64pt) - Mastering grade quality");
            Nomad::Log::info("  Real-time safe on modern CPUs");
        }
        markSettingsChanged();
    });
    addChild(m_resamplingDropdown);
    
    // Dithering mode dropdown
    m_ditheringLabel = std::make_shared<NomadUI::NUILabel>();
    m_ditheringLabel->setText("Dithering:");
    addChild(m_ditheringLabel);
    
    m_ditheringDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_ditheringDropdown->setPlaceholderText("Select Dithering Mode");
    m_ditheringDropdown->addItem("None", static_cast<int>(Audio::DitheringMode::None));
    m_ditheringDropdown->addItem("Triangular (TPDF)", static_cast<int>(Audio::DitheringMode::Triangular));
    m_ditheringDropdown->addItem("High-Pass Shaped", static_cast<int>(Audio::DitheringMode::HighPass));
    m_ditheringDropdown->addItem("Noise-Shaped (Best)", static_cast<int>(Audio::DitheringMode::NoiseShaped));
    m_ditheringDropdown->setSelectedIndex(1); // Default to Triangular
    m_ditheringDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        if (m_isApplyingQualityPreset || m_suppressDirtyStateUpdates) {
            return;
        }
        m_qualityPresetDropdown->setSelectedIndex(4); // Custom
        markSettingsChanged();
    });
    addChild(m_ditheringDropdown);
    
    // DC Removal toggle
    m_dcRemovalLabel = std::make_shared<NomadUI::NUILabel>();
    m_dcRemovalLabel->setText("DC Removal:");
    addChild(m_dcRemovalLabel);
    
    m_dcRemovalToggle = std::make_shared<NomadUI::NUIButton>();
    m_dcRemovalToggle->setText("ON");
    m_dcRemovalToggle->setStyle(NomadUI::NUIButton::Style::Secondary);
    m_dcRemovalToggle->setHoverColor(NomadUI::NUIColor::white().withAlpha(0.5f));
    m_dcRemovalToggle->setOnClick([this]() {
        std::string oldText = m_dcRemovalToggle->getText();
        if (oldText == "ON") {
            m_dcRemovalToggle->setText("OFF");
            Nomad::Log::info("[DC Removal] Button clicked: ON -> OFF");
        } else {
            m_dcRemovalToggle->setText("ON");
            Nomad::Log::info("[DC Removal] Button clicked: OFF -> ON");
        }
        m_qualityPresetDropdown->setSelectedIndex(4); // Custom
        m_cacheInvalidated = true; // Text changed, invalidate cache
        Nomad::Log::info("[DC Removal] Cache invalidated, new text: " + m_dcRemovalToggle->getText());
        markSettingsChanged();
    });
    addChild(m_dcRemovalToggle);
    
    // Soft Clipping toggle
    m_softClippingLabel = std::make_shared<NomadUI::NUILabel>();
    m_softClippingLabel->setText("Soft Clipping:");
    addChild(m_softClippingLabel);
    
    m_softClippingToggle = std::make_shared<NomadUI::NUIButton>();
    m_softClippingToggle->setText("OFF");
    m_softClippingToggle->setStyle(NomadUI::NUIButton::Style::Secondary);
    m_softClippingToggle->setHoverColor(NomadUI::NUIColor::white().withAlpha(0.5f));
    m_softClippingToggle->setOnClick([this]() {
        if (m_softClippingToggle->getText() == "ON") {
            m_softClippingToggle->setText("OFF");
        } else {
            m_softClippingToggle->setText("ON");
        }
        m_qualityPresetDropdown->setSelectedIndex(4); // Custom
        m_cacheInvalidated = true; // Text changed, invalidate cache
        markSettingsChanged();
    });
    addChild(m_softClippingToggle);
    
    // 64-bit Processing toggle
    m_precision64BitLabel = std::make_shared<NomadUI::NUILabel>();
    m_precision64BitLabel->setText("64-bit Float:");
    addChild(m_precision64BitLabel);
    
    m_precision64BitToggle = std::make_shared<NomadUI::NUIButton>();
    m_precision64BitToggle->setText("OFF");
    m_precision64BitToggle->setStyle(NomadUI::NUIButton::Style::Secondary);
    m_precision64BitToggle->setHoverColor(NomadUI::NUIColor::white().withAlpha(0.5f));
    m_precision64BitToggle->setOnClick([this]() {
        if (m_precision64BitToggle->getText() == "ON") {
            m_precision64BitToggle->setText("OFF");
            Nomad::Log::info("64-bit processing: Disabled (32-bit float)");
        } else {
            m_precision64BitToggle->setText("ON");
            Nomad::Log::info("64-bit processing: Enabled (mastering-grade precision)");
        }
        m_qualityPresetDropdown->setSelectedIndex(4); // Custom
        m_cacheInvalidated = true; // Text changed, invalidate cache
        markSettingsChanged();
    });
    addChild(m_precision64BitToggle);
    
    // === MULTI-THREADING - Parallel Audio Processing ===
    m_multiThreadingLabel = std::make_shared<NomadUI::NUILabel>();
    m_multiThreadingLabel->setText("Multi-Threading:");
    addChild(m_multiThreadingLabel);
    
    m_multiThreadingToggle = std::make_shared<NomadUI::NUIButton>();
    m_multiThreadingToggle->setText("ON");
    m_multiThreadingToggle->setStyle(NomadUI::NUIButton::Style::Secondary);
    m_multiThreadingToggle->setHoverColor(NomadUI::NUIColor::white().withAlpha(0.5f));
    m_multiThreadingToggle->setOnClick([this]() {
        if (m_multiThreadingToggle->getText() == "ON") {
            m_multiThreadingToggle->setText("OFF");
            Nomad::Log::info("Multi-threading: Disabled (single-threaded processing)");
        } else {
            m_multiThreadingToggle->setText("ON");
            Nomad::Log::info("Multi-threading: Enabled (parallel track processing)");
        }
        m_cacheInvalidated = true; // Text changed, invalidate cache
        markSettingsChanged();
    });
    addChild(m_multiThreadingToggle);
    
    // Thread count dropdown
    m_threadCountLabel = std::make_shared<NomadUI::NUILabel>();
    m_threadCountLabel->setText("Thread Count:");
    addChild(m_threadCountLabel);
    
    m_threadCountDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_threadCountDropdown->setPlaceholderText("Select Thread Count");
    
    // Detect hardware thread count
    size_t hwThreads = std::thread::hardware_concurrency();
    size_t maxThreads = std::max(size_t(2), std::min(size_t(16), hwThreads > 0 ? hwThreads : 8));
    
    // Add thread count options (2 to maxThreads)
    for (size_t i = 2; i <= maxThreads; ++i) {
        std::string label = std::to_string(i) + " threads";
        if (i == hwThreads - 1) {
            label += " (Rec.)";
        } else if (i == hwThreads) {
            label += " (All cores)";
        }
        m_threadCountDropdown->addItem(label, static_cast<int>(i));
    }
    
    // Select recommended thread count (hardware - 1)
    size_t recommendedThreads = std::max(size_t(2), hwThreads > 0 ? hwThreads - 1 : 4);
    m_threadCountDropdown->setSelectedIndex(static_cast<int>(recommendedThreads) - 2); // -2 because we start from 2 threads
    
    m_threadCountDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        Nomad::Log::info("Thread count changed to: " + std::to_string(value));
        markSettingsChanged();
    });
    addChild(m_threadCountDropdown);
    
    // === NOMAD MODE - Signature Audio Character ===
    m_nomadModeLabel = std::make_shared<NomadUI::NUILabel>();
    m_nomadModeLabel->setText("Nomad Mode:");
    addChild(m_nomadModeLabel);
    
    m_nomadModeDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_nomadModeDropdown->setPlaceholderText("Select Nomad Mode");
    m_nomadModeDropdown->addItem("Off (Bypass)", static_cast<int>(Audio::NomadMode::Off));
    m_nomadModeDropdown->addItem("Transparent (Reference)", static_cast<int>(Audio::NomadMode::Transparent));
    m_nomadModeDropdown->addItem("Euphoric (Analog Soul)", static_cast<int>(Audio::NomadMode::Euphoric));
    m_nomadModeDropdown->setSelectedIndex(0); // Default to Off
    m_nomadModeDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        // Nomad Mode is independent of quality presets
        // Update status message to show what mode we're in
        auto mode = static_cast<Audio::NomadMode>(value);
        if (mode == Audio::NomadMode::Euphoric) {
            Nomad::Log::info("Nomad Mode: Euphoric - Harmonic warmth, smooth transients, rich tails");
        } else if (mode == Audio::NomadMode::Transparent) {
            Nomad::Log::info("Nomad Mode: Transparent - Clinical precision, reference-grade");
        } else {
            Nomad::Log::info("Nomad Mode: Off - Bypassed");
        }
        markSettingsChanged();
    });
    addChild(m_nomadModeDropdown);

    // Create buttons
    m_applyButton = std::make_shared<NomadUI::NUIButton>();
    m_applyButton->setText("Apply");
    m_applyButton->setStyle(NomadUI::NUIButton::Style::Secondary); // Default to Secondary (inactive)
    m_applyButton->setHoverColor(NomadUI::NUIColor::white().withAlpha(0.5f)); // Stronger Bright hover
    m_applyButton->setOnClick([this]() {
        applySettings();
    });
    m_applyButton->setEnabled(false);
    addChild(m_applyButton);
    
    // m_cancelButton = std::make_shared<NomadUI::NUIButton>();
    m_cancelButton.reset(new NomadUI::NUIButton());
    m_cancelButton->setText("Cancel");
    m_cancelButton->setStyle(NomadUI::NUIButton::Style::Secondary); // Secondary style
    m_cancelButton->setHoverColor(NomadUI::NUIColor::white().withAlpha(0.5f)); // Stronger Bright hover
    m_cancelButton->setOnClick([this]() {
        cancelSettings();
    });
    addChild(m_cancelButton);
    
    m_testSoundButton = std::make_shared<NomadUI::NUIButton>();
    m_testSoundButton->setText("Test Sound");
    m_testSoundButton->setStyle(NomadUI::NUIButton::Style::Secondary); // Secondary style
    m_testSoundButton->setHoverColor(NomadUI::NUIColor::white().withAlpha(0.5f));
    m_testSoundButton->setOnClick([this]() {
        if (m_isPlayingTestSound) {
            stopTestSound();
        } else {
            playTestSound();
        }
    });
    addChild(m_testSoundButton);
    
    // Create play icon for test button (SVG)
    m_playIcon = std::make_shared<NomadUI::NUIIcon>(kTestSoundPlaySvg);
    m_playIcon->setIconSize(NomadUI::NUIIconSize::Small);  // Small for compact button
    m_playIcon->setColorFromTheme("primary");
    
    // Load lists
    updateDriverList();
    updateDeviceList();
    updateSampleRateList();
    updateBufferSizeList();
}

void AudioSettingsDialog::show() {
    m_visible = true;
    setVisible(true);
    
    // Reset to settings tab
    m_activeTab = "settings";
    if (m_tabBar) {
        m_tabBar->setActiveTab(m_activeTab);
    }
    
    // CRITICAL FIX: Center the dialog on screen
    // Component bounds represent the full window/parent area
    auto componentBounds = getBounds();
    if (componentBounds.width > 0 && componentBounds.height > 0) {
        // Center the dialog rect within the component bounds
        m_dialogBounds.x = componentBounds.x + (componentBounds.width - m_dialogBounds.width) / 2;
        m_dialogBounds.y = componentBounds.y + (componentBounds.height - m_dialogBounds.height) / 2;
    }
    
    m_suppressDirtyStateUpdates = true;
    loadCurrentSettings();
    updateDriverList();
    updateDeviceList();
    updateSampleRateList();
    updateBufferSizeList();
    captureOriginalQualityStateFromUi();
    m_suppressDirtyStateUpdates = false;
    updateApplyButtonState();
    layoutComponents();
}

void AudioSettingsDialog::hide() {
    // Stop test sound if playing
    if (m_isPlayingTestSound) {
        stopTestSound();
    }
    
    m_visible = false;
    setVisible(false);
}

void AudioSettingsDialog::setVisible(bool visible) {
    m_visible = visible;
    NomadUI::NUIComponent::setVisible(visible);
}

void AudioSettingsDialog::onRender(NomadUI::NUIRenderer& renderer) {
    if (!m_visible) return;

    // Render directly to ensure hover/pressed states update reliably.
    renderBackground(renderer);
    renderDialog(renderer);
    NomadUI::NUIComponent::onRender(renderer);

    if (m_testSoundButton && m_playIcon) {
        auto bounds = m_testSoundButton->getBounds();
        const float iconPadding = 10.0f;
        const float iconX = bounds.x + iconPadding;
        const float iconY = bounds.y + (bounds.height - m_playIcon->getSize().height) / 2.0f;
        m_playIcon->setBounds(NomadUI::NUIRect(iconX, iconY,
                                               m_playIcon->getSize().width,
                                               m_playIcon->getSize().height));
        m_playIcon->onRender(renderer);
    }
    
    // Render dropdown lists after all other components for proper z-order
    if (m_driverDropdown && m_driverDropdown->isOpen()) {
        m_driverDropdown->renderDropdownList(renderer);
    }
    if (m_deviceDropdown && m_deviceDropdown->isOpen()) {
        m_deviceDropdown->renderDropdownList(renderer);
    }
    if (m_sampleRateDropdown && m_sampleRateDropdown->isOpen()) {
        m_sampleRateDropdown->renderDropdownList(renderer);
    }
    if (m_bufferSizeDropdown && m_bufferSizeDropdown->isOpen()) {
        m_bufferSizeDropdown->renderDropdownList(renderer);
    }
    if (m_qualityPresetDropdown && m_qualityPresetDropdown->isOpen()) {
        m_qualityPresetDropdown->renderDropdownList(renderer);
    }
    if (m_resamplingDropdown && m_resamplingDropdown->isOpen()) {
        m_resamplingDropdown->renderDropdownList(renderer);
    }
    if (m_ditheringDropdown && m_ditheringDropdown->isOpen()) {
        m_ditheringDropdown->renderDropdownList(renderer);
    }
    if (m_threadCountDropdown && m_threadCountDropdown->isOpen()) {
        m_threadCountDropdown->renderDropdownList(renderer);
    }
    if (m_nomadModeDropdown && m_nomadModeDropdown->isOpen()) {
        m_nomadModeDropdown->renderDropdownList(renderer);
    }
}

void AudioSettingsDialog::onResize(int width, int height) {
    // Update component bounds (represents the full window area)
    setBounds(NomadUI::NUIRect(0, 0, static_cast<float>(width), static_cast<float>(height)));
    
    // Center the dialog within the new window size
    m_dialogBounds.x = (width - m_dialogBounds.width) / 2;
    m_dialogBounds.y = (height - m_dialogBounds.height) / 2;
    
    layoutComponents();
}

void AudioSettingsDialog::onUpdate(double deltaTime) {
    // Skip expensive updates when hidden
    if (!m_visible) return;
    
    // Animate blink effect - slower decay for visibility
    if (m_blinkAnimation > 0.0f) {
        m_blinkAnimation -= static_cast<float>(deltaTime) * 2.0f; // Slower decay (was 5.0)
        if (m_blinkAnimation < 0.0f) {
            m_blinkAnimation = 0.0f;
        }
        setDirty(true);
    }
    
    // Fade out error message
    if (m_errorMessageAlpha > 0.0f) {
        m_errorMessageAlpha -= static_cast<float>(deltaTime) * 0.5f; // Slow fade
        if (m_errorMessageAlpha < 0.0f) {
            m_errorMessageAlpha = 0.0f;
            m_errorMessage = ""; // Clear message when fully faded
            layoutComponents(); // Restore layout when error message disappears
        }
        setDirty(true);
    }
    
    // Call parent to update children
    NomadUI::NUIComponent::onUpdate(deltaTime);
}

bool AudioSettingsDialog::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    if (!m_visible) return false;
    
    // Check if any dropdown is CURRENTLY open (real-time check, not cached flag)
    // This prevents clicks from passing through to underlying buttons
    bool anyDropdownCurrentlyOpen = (m_driverDropdown && m_driverDropdown->isOpen()) ||
                                     (m_deviceDropdown && m_deviceDropdown->isOpen()) ||
                                     (m_sampleRateDropdown && m_sampleRateDropdown->isOpen()) ||
                                     (m_bufferSizeDropdown && m_bufferSizeDropdown->isOpen()) ||
                                     (m_qualityPresetDropdown && m_qualityPresetDropdown->isOpen()) ||
                                     (m_resamplingDropdown && m_resamplingDropdown->isOpen()) ||
                                     (m_ditheringDropdown && m_ditheringDropdown->isOpen()) ||
                                     (m_threadCountDropdown && m_threadCountDropdown->isOpen()) ||
                                     (m_nomadModeDropdown && m_nomadModeDropdown->isOpen());
    
    // If PRESSED event occurs while dropdown is open, set blocking flag
    // This flag persists until RELEASED to prevent click-through
    if (anyDropdownCurrentlyOpen && event.pressed) {
        m_blockingEventsForDropdown = true;
    }
    
    // If we're blocking events due to dropdown interaction, consume ALL events
    // until the RELEASED event completes the click sequence
    if (m_blockingEventsForDropdown || anyDropdownCurrentlyOpen) {
        // Manually route event to ONLY the open dropdowns, NOT all children
        bool handled = false;
        if (m_driverDropdown && m_driverDropdown->isOpen()) {
            handled = m_driverDropdown->onMouseEvent(event) || handled;
        }
        if (m_deviceDropdown && m_deviceDropdown->isOpen()) {
            handled = m_deviceDropdown->onMouseEvent(event) || handled;
        }
        if (m_sampleRateDropdown && m_sampleRateDropdown->isOpen()) {
            handled = m_sampleRateDropdown->onMouseEvent(event) || handled;
        }
        if (m_bufferSizeDropdown && m_bufferSizeDropdown->isOpen()) {
            handled = m_bufferSizeDropdown->onMouseEvent(event) || handled;
        }
        if (m_qualityPresetDropdown && m_qualityPresetDropdown->isOpen()) {
            handled = m_qualityPresetDropdown->onMouseEvent(event) || handled;
        }
        if (m_resamplingDropdown && m_resamplingDropdown->isOpen()) {
            handled = m_resamplingDropdown->onMouseEvent(event) || handled;
        }
        if (m_ditheringDropdown && m_ditheringDropdown->isOpen()) {
            handled = m_ditheringDropdown->onMouseEvent(event) || handled;
        }
        if (m_threadCountDropdown && m_threadCountDropdown->isOpen()) {
            handled = m_threadCountDropdown->onMouseEvent(event) || handled;
        }
        if (m_nomadModeDropdown && m_nomadModeDropdown->isOpen()) {
            handled = m_nomadModeDropdown->onMouseEvent(event) || handled;
        }
        
        // Clear blocking flag when RELEASED event completes the sequence
        if (event.released) {
            m_blockingEventsForDropdown = false;
        }
        
        // CRITICAL: Always consume the event to prevent buttons from receiving it
        // Even if dropdowns didn't handle it (clicked outside), we block propagation
        return true;
    }
    
    // Track hover state for close button
    bool wasHovered = m_closeButtonHovered;
    m_closeButtonHovered = m_closeButtonBounds.contains(event.position.x, event.position.y);
    
    // Redraw if hover state changed
    if (wasHovered != m_closeButtonHovered) {
        m_cacheInvalidated = true; // Invalidate cache for hover animation
        setDirty(true);
    }
    
    // Check if click is on close button
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        if (m_closeButtonHovered) {
            cancelSettings();
            return true;
        }
        
        // Check if click is outside dialog - trigger blink animation instead of closing
        if (!m_dialogBounds.contains(event.position.x, event.position.y)) {
            // Trigger blink animation
            m_blinkAnimation = 1.0f;
            setDirty(true);
            return true;
        }
    }
    
    // Manual Hover Handling for Buttons to ensure repaints
    // Standard dispatch sometimes misses the invalidation needed for instant feedback
    bool anyHoverChanged = false;
    
    auto updateButtonHover = [&](std::shared_ptr<NomadUI::NUIButton> btn) {
        if (!btn) return;
        bool isOver = btn->getBounds().contains(event.position);
        if (btn->isHovered() != isOver) {
            btn->setHovered(isOver);
            anyHoverChanged = true;
        }
    };
    
    updateButtonHover(m_applyButton);
    updateButtonHover(m_cancelButton);
    updateButtonHover(m_testSoundButton);
    updateButtonHover(m_dcRemovalToggle);
    updateButtonHover(m_softClippingToggle);
    updateButtonHover(m_precision64BitToggle);
    updateButtonHover(m_multiThreadingToggle);

    if (anyHoverChanged) {
        setDirty(true);
        // Force repaint immediately
    }

    // Let children handle events (buttons will handle their own clicks)
    if (NomadUI::NUIComponent::onMouseEvent(event)) {
        return true;
    }

    // BLOCK CLICKTHROUGH:
    // Since we are a modal dialog overlay, we must consume ALL mouse events
    // that occur within our bounds (which is the entire screen/window).
    // This prevents clicks from passing through to the playlist/tracks behind.
    return true;
}

bool AudioSettingsDialog::onKeyEvent(const NomadUI::NUIKeyEvent& event) {
    if (!m_visible) return false;

    // Let focused children (e.g., dropdowns) consume keys first.
    if (NomadUI::NUIComponent::onKeyEvent(event)) {
        return true;
    }

    if (!event.pressed) return false;

    if (event.keyCode == NomadUI::NUIKeyCode::Escape) {
        cancelSettings();
        return true;
    }

    if (event.keyCode == NomadUI::NUIKeyCode::Enter) {
        if (m_applyButton && m_applyButton->isEnabled()) {
            applySettings();
            return true;
        }
    }

    return false;
}

void AudioSettingsDialog::updateDriverList() {
    if (!m_audioManager) return;
    
    m_drivers = m_audioManager->getAvailableDriverTypes();
    m_driverDropdown->clearItems();
    
    // Check if we're in fallback mode
    bool isUsingFallback = m_audioManager->isUsingFallbackDriver();
    Audio::AudioDriverType activeDriver = m_audioManager->getActiveDriverType();
    
    // Add available WASAPI/RtAudio drivers with professional descriptions
    int itemIndex = 0;
    for (const auto& driverType : m_drivers) {
        std::string name;
        bool shouldEnable = true;
        
        switch (driverType) {
            case Audio::AudioDriverType::WASAPI_EXCLUSIVE:
                name = "WASAPI Exclusive (~8-12ms RTL)";
                // Disable if Exclusive is blocked (fallback to Shared occurred)
                if (isUsingFallback && activeDriver == Audio::AudioDriverType::WASAPI_SHARED) {
                    name += " [Blocked]";
                    shouldEnable = false;
                }
                break;
            case Audio::AudioDriverType::WASAPI_SHARED:
                name = "WASAPI Shared (~20-30ms RTL)";
                if (isUsingFallback && activeDriver == Audio::AudioDriverType::WASAPI_SHARED) {
                    name += " [Active - Fallback]";
                }
                break;
            case Audio::AudioDriverType::RTAUDIO:
                name = "RtAudio (Legacy)";
                break;
            default:
                name = "Unknown Driver";
                break;
        }
        
        m_driverDropdown->addItem(name, static_cast<int>(driverType));
        
        // Disable the item if it's blocked
        if (!shouldEnable) {
            m_driverDropdown->setItemEnabled(itemIndex, false);
        }
        
        if (driverType == m_selectedDriverType) {
            m_driverDropdown->setSelectedIndex(itemIndex);
        }
        
        itemIndex++;
    }
    
    // Add ASIO drivers for informational display (not functional yet) - all disabled
    // ASIO enumeration temporarily removed during refactor
}

void AudioSettingsDialog::updateDeviceList() {
    if (!m_audioManager) return;
    
    m_devices = m_audioManager->getDevices();
    m_deviceDropdown->clearItems();
    
    for (const auto& device : m_devices) {
        m_deviceDropdown->addItem(device.name, device.id);
        if (device.id == m_selectedDeviceId) {
            m_deviceDropdown->setSelectedIndex(m_deviceDropdown->getItemCount() - 1);
        }
    }
}

void AudioSettingsDialog::updateSampleRateList() {
    m_sampleRates = {44100, 48000, 88200, 96000, 176400, 192000};
    m_sampleRateDropdown->clearItems();
    
    for (const auto& rate : m_sampleRates) {
        std::string text = std::to_string(rate) + " Hz";
        m_sampleRateDropdown->addItem(text, rate);
        if (rate == m_selectedSampleRate) {
            m_sampleRateDropdown->setSelectedIndex(m_sampleRateDropdown->getItemCount() - 1);
        }
    }
    updateLatencyEstimate();
}

void AudioSettingsDialog::updateBufferSizeList() {
    m_bufferSizes = {64, 128, 256, 512, 1024, 2048};
    m_bufferSizeDropdown->clearItems();
    
    for (const auto& size : m_bufferSizes) {
        std::string text = std::to_string(size) + " samples";
        m_bufferSizeDropdown->addItem(text, size);
        if (size == m_selectedBufferSize) {
            m_bufferSizeDropdown->setSelectedIndex(m_bufferSizeDropdown->getItemCount() - 1);
        }
    }
    updateLatencyEstimate();
}

void AudioSettingsDialog::updateLatencyEstimate() {
    if (!m_latencyLabel) return;

    if (m_selectedSampleRate == 0 || m_selectedBufferSize == 0) {
        m_latencyLabel->setText("");
        return;
    }

    const double oneWayMs = (static_cast<double>(m_selectedBufferSize) / static_cast<double>(m_selectedSampleRate)) * 1000.0;
    const double roundTripMs = oneWayMs * 2.0;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << roundTripMs;
    m_latencyLabel->setText("Estimated latency: " + ss.str() + " ms (in + out)");
}

void AudioSettingsDialog::loadCurrentSettings() {
    if (!m_audioManager) return;
    
    // Load current audio settings from the current config
    const auto& config = m_audioManager->getCurrentConfig();
    m_originalDeviceId = config.deviceId;
    m_originalSampleRate = config.sampleRate;
    m_originalBufferSize = config.bufferSize;
    m_originalDriverType = m_audioManager->getActiveDriverType();
    
    // Set selected values
    m_selectedDeviceId = m_originalDeviceId;
    m_selectedSampleRate = m_originalSampleRate;
    m_selectedBufferSize = m_originalBufferSize;
    m_selectedDriverType = m_originalDriverType;

    updateLatencyEstimate();
}

void AudioSettingsDialog::captureOriginalQualityStateFromUi() {
    m_originalQualityPresetIndex = m_qualityPresetDropdown ? m_qualityPresetDropdown->getSelectedIndex() : -1;
    m_originalResamplingIndex = m_resamplingDropdown ? m_resamplingDropdown->getSelectedIndex() : -1;
    m_originalDitheringIndex = m_ditheringDropdown ? m_ditheringDropdown->getSelectedIndex() : -1;
    m_originalDCRemoval = m_dcRemovalToggle && (m_dcRemovalToggle->getText() == "ON");
    m_originalSoftClipping = m_softClippingToggle && (m_softClippingToggle->getText() == "ON");
    m_originalPrecision64Bit = m_precision64BitToggle && (m_precision64BitToggle->getText() == "ON");
    m_originalMultiThreading = m_multiThreadingToggle && (m_multiThreadingToggle->getText() == "ON");
    m_originalThreadCountIndex = m_threadCountDropdown ? m_threadCountDropdown->getSelectedIndex() : -1;
    m_originalNomadModeIndex = m_nomadModeDropdown ? m_nomadModeDropdown->getSelectedIndex() : -1;
}

bool AudioSettingsDialog::hasUnsavedChanges() const {
    if (m_selectedDriverType != m_originalDriverType) return true;
    if (m_selectedDeviceId != m_originalDeviceId) return true;
    if (m_selectedSampleRate != m_originalSampleRate) return true;
    if (m_selectedBufferSize != m_originalBufferSize) return true;

    if (m_qualityPresetDropdown && m_originalQualityPresetIndex != -1 &&
        m_qualityPresetDropdown->getSelectedIndex() != m_originalQualityPresetIndex) {
        return true;
    }
    if (m_resamplingDropdown && m_originalResamplingIndex != -1 &&
        m_resamplingDropdown->getSelectedIndex() != m_originalResamplingIndex) {
        return true;
    }
    if (m_ditheringDropdown && m_originalDitheringIndex != -1 &&
        m_ditheringDropdown->getSelectedIndex() != m_originalDitheringIndex) {
        return true;
    }

    if (m_dcRemovalToggle && (m_dcRemovalToggle->getText() == "ON") != m_originalDCRemoval) return true;
    if (m_softClippingToggle && (m_softClippingToggle->getText() == "ON") != m_originalSoftClipping) return true;
    if (m_precision64BitToggle && (m_precision64BitToggle->getText() == "ON") != m_originalPrecision64Bit) return true;
    if (m_multiThreadingToggle && (m_multiThreadingToggle->getText() == "ON") != m_originalMultiThreading) return true;

    if (m_threadCountDropdown && m_originalThreadCountIndex != -1 &&
        m_threadCountDropdown->getSelectedIndex() != m_originalThreadCountIndex) {
        return true;
    }
    if (m_nomadModeDropdown && m_originalNomadModeIndex != -1 &&
        m_nomadModeDropdown->getSelectedIndex() != m_originalNomadModeIndex) {
        return true;
    }

    return false;
}

void AudioSettingsDialog::updateApplyButtonState() {
    if (!m_applyButton) return;
    
    bool hasChanges = hasUnsavedChanges();
    m_applyButton->setEnabled(hasChanges);
    
    // Dynamic styling: Light up when there are changes
    if (hasChanges) {
        m_applyButton->setStyle(NomadUI::NUIButton::Style::Primary);
    } else {
        m_applyButton->setStyle(NomadUI::NUIButton::Style::Secondary);
    }
}

void AudioSettingsDialog::markSettingsChanged() {
    if (m_suppressDirtyStateUpdates) return;
    updateApplyButtonState();
    setDirty(true);
}

void AudioSettingsDialog::restoreOriginalUiState() {
    m_suppressDirtyStateUpdates = true;

    // Device settings
    m_selectedDriverType = m_originalDriverType;
    m_selectedDeviceId = m_originalDeviceId;
    m_selectedSampleRate = m_originalSampleRate;
    m_selectedBufferSize = m_originalBufferSize;

    updateDriverList();
    updateDeviceList();
    updateSampleRateList();
    updateBufferSizeList();

    // Quality + processing
    if (m_qualityPresetDropdown && m_originalQualityPresetIndex != -1) {
        m_qualityPresetDropdown->setSelectedIndex(m_originalQualityPresetIndex);
    }

    const bool presetIsCustom = m_qualityPresetDropdown &&
        (m_qualityPresetDropdown->getSelectedValue() == static_cast<int>(Audio::QualityPreset::Custom));

    if (presetIsCustom) {
        if (m_resamplingDropdown && m_originalResamplingIndex != -1) {
            m_resamplingDropdown->setSelectedIndex(m_originalResamplingIndex);
        }
        if (m_ditheringDropdown && m_originalDitheringIndex != -1) {
            m_ditheringDropdown->setSelectedIndex(m_originalDitheringIndex);
        }
        if (m_dcRemovalToggle) m_dcRemovalToggle->setText(m_originalDCRemoval ? "ON" : "OFF");
        if (m_softClippingToggle) m_softClippingToggle->setText(m_originalSoftClipping ? "ON" : "OFF");
    }

    if (m_precision64BitToggle) m_precision64BitToggle->setText(m_originalPrecision64Bit ? "ON" : "OFF");
    if (m_multiThreadingToggle) m_multiThreadingToggle->setText(m_originalMultiThreading ? "ON" : "OFF");
    if (m_threadCountDropdown && m_originalThreadCountIndex != -1) {
        m_threadCountDropdown->setSelectedIndex(m_originalThreadCountIndex);
    }
    if (m_nomadModeDropdown && m_originalNomadModeIndex != -1) {
        m_nomadModeDropdown->setSelectedIndex(m_originalNomadModeIndex);
    }

    updateLatencyEstimate();

    m_suppressDirtyStateUpdates = false;
    updateApplyButtonState();
    setDirty(true);
}

void AudioSettingsDialog::applySettings() {
    if (!m_audioManager) return;
    
    // Stop test sound if playing
    if (m_isPlayingTestSound) {
        stopTestSound();
    }
    
    bool needsReopen = false;
    
    // Debug logging
    Log::info("=== Apply Settings Debug ===");
    Log::info("m_selectedDriverType = " + std::to_string(static_cast<int>(m_selectedDriverType)));
    Log::info("m_originalDriverType = " + std::to_string(static_cast<int>(m_originalDriverType)));
    Log::info("Driver type changed? " + std::string((m_selectedDriverType != m_originalDriverType) ? "YES" : "NO"));
    
    // Check if driver type changed
    if (m_selectedDriverType != m_originalDriverType) {
        Log::info("Driver type changed, applying...");
        
        // Try to apply new driver
        if (m_audioManager->setPreferredDriverType(m_selectedDriverType)) {
            m_originalDriverType = m_selectedDriverType;
            Log::info("Driver type applied successfully");
        } else {
            Log::error("Failed to apply driver type - falling back to working driver");
            
            // Restore stream if it failed
            if (m_onStreamRestore) {
                m_onStreamRestore();
            }
            
            // Update UI to show actual active driver (not what user selected)
            m_selectedDriverType = m_audioManager->getActiveDriverType();
            m_originalDriverType = m_selectedDriverType;
            
            // Reload driver list to reflect actual state
            updateDriverList();
        }
        needsReopen = true;  // Driver change reopens stream
    }
    
    // Apply sample rate if changed (only if driver didn't already reopen)
    if (!needsReopen && m_selectedSampleRate != m_originalSampleRate) {
        Log::info("Sample rate changed to: " + std::to_string(m_selectedSampleRate));
        if (m_audioManager->setSampleRate(m_selectedSampleRate)) {
            m_originalSampleRate = m_selectedSampleRate;
            Log::info("Sample rate applied successfully");
        } else {
            Log::error("Failed to apply sample rate");
            m_errorMessage = "Failed to change sample rate - restored previous setting";
            m_errorMessageAlpha = 1.0f;
            layoutComponents(); // Adjust layout to prevent overlap
            
            // Restore dropdown to show actual value
            m_selectedSampleRate = m_originalSampleRate;
            updateSampleRateList();
        }
        needsReopen = true;
    }
    
    // Apply buffer size if changed (only if neither driver nor sample rate reopened)
    if (!needsReopen && m_selectedBufferSize != m_originalBufferSize) {
        Log::info("Buffer size changed to: " + std::to_string(m_selectedBufferSize));
        if (m_audioManager->setBufferSize(m_selectedBufferSize)) {
            m_originalBufferSize = m_selectedBufferSize;
            Log::info("Buffer size applied successfully");
        } else {
            Log::error("Failed to apply buffer size");
            m_errorMessage = "Buffer size not supported - restored previous setting";
            m_errorMessageAlpha = 1.0f;
            layoutComponents(); // Adjust layout to prevent overlap
            
            // Restore dropdown to show actual value
            m_selectedBufferSize = m_originalBufferSize;
            updateBufferSizeList();
        }
    }
    
    // DON'T close dialog on Apply - let user close manually
    // hide(); // REMOVED
    
    // Apply audio quality settings to all tracks
    if (m_trackManager) {
        Audio::AudioQualitySettings qualitySettings;
        
        // Get preset or custom settings
        int presetValue = m_qualityPresetDropdown->getSelectedValue();
        qualitySettings.preset = static_cast<Audio::QualityPreset>(presetValue);
        
        // Get resampling mode from dropdown
        int resamplingValue = m_resamplingDropdown->getSelectedValue();
        qualitySettings.resampling = static_cast<Audio::ResamplingMode>(resamplingValue);
        
        // Get dithering mode from dropdown
        int ditheringValue = m_ditheringDropdown->getSelectedValue();
        qualitySettings.dithering = static_cast<Audio::DitheringMode>(ditheringValue);

        if (m_audioManager) {
            Audio::DitheringMode selectedDitheringMode = static_cast<Audio::DitheringMode>(m_ditheringDropdown->getSelectedValue());
            // Only enable dithering if it's not None (implementation simplification for now)
            // Future improvements: pass the actual mode (Triangular, NoiseShaped) to the driver
            m_audioManager->setDitheringEnabled(selectedDitheringMode != Audio::DitheringMode::None);
        }
        
        // Get toggle states
        qualitySettings.removeDCOffset = (m_dcRemovalToggle->getText() == "ON");
        qualitySettings.enableSoftClipping = (m_softClippingToggle->getText() == "ON");
        
        // Get 64-bit precision toggle
        qualitySettings.precision = (m_precision64BitToggle->getText() == "ON") 
            ? Audio::InternalPrecision::Float64 
            : Audio::InternalPrecision::Float32;
        
        // Get Nomad Mode from dropdown
        int nomadModeValue = m_nomadModeDropdown->getSelectedValue();
        qualitySettings.nomadMode = static_cast<Audio::NomadMode>(nomadModeValue);
        
        // Additional settings (future expansion)
        qualitySettings.oversampling = Audio::OversamplingMode::None;
        
        // Apply to all tracks
        size_t trackCount = m_trackManager->getTrackCount();
        for (size_t i = 0; i < trackCount; ++i) {
            auto track = m_trackManager->getTrack(i);
            if (track) {
                track->setQualitySettings(qualitySettings);
            }
        }
        
        // Apply multi-threading settings to TrackManager
        bool multiThreadingEnabled = (m_multiThreadingToggle->getText() == "ON");
        m_trackManager->setMultiThreadingEnabled(multiThreadingEnabled);
        
        int threadCount = m_threadCountDropdown->getSelectedValue();
        m_trackManager->setThreadCount(threadCount);
        
        // Log quality settings
        const char* presetNames[] = {"Custom", "Economy", "Balanced", "High-Fidelity", "Mastering"};
        const char* resamplingNames[] = {"Fast", "Medium", "High", "Ultra", "Extreme", "Perfect"};
        const char* ditheringNames[] = {"None", "Triangular", "High-Pass", "Noise-Shaped"};
        const char* nomadModeNames[] = {"Off", "Transparent", "Euphoric"};
        const char* precisionNames[] = {"32-bit Float", "64-bit Float"};
        
        Log::info("Applied audio quality settings:");
        Log::info("  Preset: " + std::string(presetNames[static_cast<int>(qualitySettings.preset)]));
        Log::info("  Resampling: " + std::string(resamplingNames[static_cast<int>(qualitySettings.resampling)]));
        Log::info("  Dithering: " + std::string(ditheringNames[static_cast<int>(qualitySettings.dithering)]));
        Log::info("  Precision: " + std::string(precisionNames[static_cast<int>(qualitySettings.precision)]));
        Log::info("  DC Removal: " + std::string(qualitySettings.removeDCOffset ? "ON" : "OFF"));
        Log::info("  Soft Clipping: " + std::string(qualitySettings.enableSoftClipping ? "ON" : "OFF"));
        Log::info("  Nomad Mode: " + std::string(nomadModeNames[static_cast<int>(qualitySettings.nomadMode)]));
        Log::info("  Multi-Threading: " + std::string(multiThreadingEnabled ? "ON" : "OFF"));
        Log::info("  Thread Count: " + std::to_string(threadCount));
    }
    
    // Treat Apply as the new baseline for dirty tracking
    captureOriginalQualityStateFromUi();
    updateApplyButtonState();

    // Notify callback
    if (m_onApply) {
        m_onApply();
    }
}

void AudioSettingsDialog::cancelSettings() {
    // Stop test sound if playing
    if (m_isPlayingTestSound) {
        stopTestSound();
    }

    restoreOriginalUiState();

    // Close dialog
    hide();
    
    // Notify callback
    if (m_onCancel) {
        m_onCancel();
    }
}

void AudioSettingsDialog::layoutComponents() {
    if (!m_visible) return;
    
    // Invalidate FBO cache after layout changes
    m_cacheInvalidated = true;
    
    // === IMPROVED SPACIOUS LAYOUT ===
    float padding = 24.0f;         // More comfortable padding
    float columnSpacing = 20.0f;   // Spacing between columns
    float labelWidth = 90.0f;      // Label width (slightly tighter)
    float dropdownWidth = 190.0f;  // Wider dropdowns to reduce truncation
    float dropdownHeight = 32.0f;  // Taller dropdowns for better usability
    float buttonWidth = 110.0f;    // Wider buttons
    float buttonHeight = 36.0f;    // Taller buttons
    float buttonSpacing = 12.0f;   // Good button spacing
    float verticalSpacing = 12.0f; // Vertical spacing between rows
    float sectionSpacing = 18.0f;  // Section spacing
    float toggleWidth = 65.0f;     // Toggle button width
    float tabBarHeight = 40.0f;    // Height for tab bar
    
    // Calculate column width to fit all content
    float columnWidth = labelWidth + dropdownWidth + 16.0f; // 16px spacing between label and dropdown
    
    // Layout tab buttons
    float tabBarY = m_dialogBounds.y + 55.0f; // Below title bar
    float tabButtonHeight = 36.0f;
    float tabBarWidth = 240.0f;
    if (m_tabBar) {
        m_tabBar->setBounds(NomadUI::NUIRect(
            m_dialogBounds.x + padding,
            tabBarY,
            tabBarWidth,
            tabButtonHeight
        ));
    }
    
    // Column positions (evenly distributed to fit content)
    float totalContentWidth = (columnWidth * 3) + (columnSpacing * 2);
    float startX = m_dialogBounds.x + (m_dialogBounds.width - totalContentWidth) / 2.0f;
    
    float leftColumnX = startX;
    float middleColumnX = startX + columnWidth + columnSpacing;
    float rightColumnX = startX + (columnWidth + columnSpacing) * 2;
    
    float errorHeight = (m_errorMessageAlpha > 0.0f && !m_errorMessage.empty()) ? 28.0f : 0.0f;
    // Start Y position (below title bar, tabs, and (in settings tab) column headers)
    float headerHeight = 20.0f;
    float headerY = tabBarY + tabButtonHeight + 8.0f;
    float startY = (m_activeTab == "settings")
        ? (headerY + headerHeight + 12.0f + errorHeight)
        : (tabBarY + tabButtonHeight + 20.0f + errorHeight);
    
    // Switch between tabs
    if (m_activeTab == "info") {
        // Hide all settings controls
        m_driverLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_driverDropdown->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_deviceLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_deviceDropdown->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_sampleRateLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_sampleRateDropdown->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_bufferSizeLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_bufferSizeDropdown->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_latencyLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_testSoundButton->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_qualitySectionLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_qualityPresetLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_qualityPresetDropdown->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_resamplingLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_resamplingDropdown->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_ditheringLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_ditheringDropdown->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_dcRemovalLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_dcRemovalToggle->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_softClippingLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_softClippingToggle->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_precision64BitLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_precision64BitToggle->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_multiThreadingLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_multiThreadingToggle->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_threadCountLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_threadCountDropdown->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_nomadModeLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_nomadModeDropdown->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_asioInfoLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        
        // Show info content
        float contentWidth = m_dialogBounds.width - padding * 2;
        float contentHeight = m_dialogBounds.height - startY - buttonHeight - padding * 3;
        
        m_infoTitle->setBounds(NomadUI::NUIRect(leftColumnX, startY, contentWidth, 30.0f));
        m_infoContent->setBounds(NomadUI::NUIRect(leftColumnX + 10.0f, startY + 40.0f, 
                                                   contentWidth - 20.0f, contentHeight - 40.0f));
    } else {
        // Hide info content
        m_infoTitle->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        m_infoContent->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        
        // === LEFT COLUMN: Audio Device Settings ===
        float leftY = startY;
        float leftLabelX = leftColumnX;
        float leftDropdownX = leftColumnX + labelWidth + 16.0f;
        
        // Driver selector
        m_driverLabel->setBounds(NomadUI::NUIRect(leftLabelX, leftY, labelWidth, dropdownHeight));
        m_driverDropdown->setBounds(NomadUI::NUIRect(leftDropdownX, leftY, dropdownWidth, dropdownHeight));
        
        // Device selector
        leftY += dropdownHeight + verticalSpacing;
        m_deviceLabel->setBounds(NomadUI::NUIRect(leftLabelX, leftY, labelWidth, dropdownHeight));
        m_deviceDropdown->setBounds(NomadUI::NUIRect(leftDropdownX, leftY, dropdownWidth, dropdownHeight));
        
        // Sample rate selector
        leftY += dropdownHeight + sectionSpacing;
        m_sampleRateLabel->setBounds(NomadUI::NUIRect(leftLabelX, leftY, labelWidth, dropdownHeight));
        m_sampleRateDropdown->setBounds(NomadUI::NUIRect(leftDropdownX, leftY, dropdownWidth, dropdownHeight));
        
        // Buffer size selector
        leftY += dropdownHeight + verticalSpacing;
        m_bufferSizeLabel->setBounds(NomadUI::NUIRect(leftLabelX, leftY, labelWidth, dropdownHeight));
        m_bufferSizeDropdown->setBounds(NomadUI::NUIRect(leftDropdownX, leftY, dropdownWidth, dropdownHeight));

        // Estimated latency (uses the existing section gap, doesn't shift the next control)
        if (m_latencyLabel) {
            const float latencyY = leftY + dropdownHeight + 2.0f;
            m_latencyLabel->setBounds(NomadUI::NUIRect(leftDropdownX, latencyY, dropdownWidth, 16.0f));
        }
        
        // Test sound button - centered in the column
        leftY += dropdownHeight + sectionSpacing;
        float testButtonWidth = 140.0f;
        float testButtonHeight = 36.0f;
        float columnTotalWidth = labelWidth + dropdownWidth + 16.0f; // Total width of the column
        float testButtonX = leftColumnX + (columnTotalWidth - testButtonWidth) / 2.0f;
        m_testSoundButton->setBounds(NomadUI::NUIRect(testButtonX, leftY, testButtonWidth, testButtonHeight));
        
        // === MIDDLE COLUMN: Audio Quality Settings (Part 1) ===
        float middleY = startY;
        float middleLabelX = middleColumnX;
        float middleDropdownX = middleColumnX + labelWidth + 16.0f;
        
        // Hide the quality section label
        m_qualitySectionLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
        
        // Quality Preset dropdown
        m_qualityPresetLabel->setBounds(NomadUI::NUIRect(middleLabelX, middleY, labelWidth, dropdownHeight));
        m_qualityPresetDropdown->setBounds(NomadUI::NUIRect(middleDropdownX, middleY, dropdownWidth, dropdownHeight));
        
        // Resampling mode dropdown
        middleY += dropdownHeight + verticalSpacing;
        m_resamplingLabel->setBounds(NomadUI::NUIRect(middleLabelX, middleY, labelWidth, dropdownHeight));
        m_resamplingDropdown->setBounds(NomadUI::NUIRect(middleDropdownX, middleY, dropdownWidth, dropdownHeight));
        
        // Dithering mode dropdown
        middleY += dropdownHeight + verticalSpacing;
        m_ditheringLabel->setBounds(NomadUI::NUIRect(middleLabelX, middleY, labelWidth, dropdownHeight));
        m_ditheringDropdown->setBounds(NomadUI::NUIRect(middleDropdownX, middleY, dropdownWidth, dropdownHeight));
        
        // DC Removal toggle
        middleY += dropdownHeight + sectionSpacing;
        m_dcRemovalLabel->setBounds(NomadUI::NUIRect(middleLabelX, middleY, labelWidth, dropdownHeight));
        float middleToggleX = middleDropdownX + dropdownWidth - toggleWidth;
        m_dcRemovalToggle->setBounds(NomadUI::NUIRect(middleToggleX, middleY, toggleWidth, dropdownHeight));
        
        // Soft Clipping toggle
        middleY += dropdownHeight + verticalSpacing;
        m_softClippingLabel->setBounds(NomadUI::NUIRect(middleLabelX, middleY, labelWidth, dropdownHeight));
        m_softClippingToggle->setBounds(NomadUI::NUIRect(middleToggleX, middleY, toggleWidth, dropdownHeight));
        
        // === RIGHT COLUMN: Audio Quality Settings (Part 2) ===
        float rightY = startY;
        float rightLabelX = rightColumnX;
        float rightDropdownX = rightColumnX + labelWidth + 16.0f;
        
        // 64-bit Processing toggle
        m_precision64BitLabel->setBounds(NomadUI::NUIRect(rightLabelX, rightY, labelWidth, dropdownHeight));
        float rightToggleX = rightDropdownX + dropdownWidth - toggleWidth;
        m_precision64BitToggle->setBounds(NomadUI::NUIRect(rightToggleX, rightY, toggleWidth, dropdownHeight));
        
        // Multi-Threading toggle
        rightY += dropdownHeight + verticalSpacing;
        m_multiThreadingLabel->setBounds(NomadUI::NUIRect(rightLabelX, rightY, labelWidth, dropdownHeight));
        m_multiThreadingToggle->setBounds(NomadUI::NUIRect(rightToggleX, rightY, toggleWidth, dropdownHeight));
        
        // Thread Count dropdown
        rightY += dropdownHeight + verticalSpacing;
        m_threadCountLabel->setBounds(NomadUI::NUIRect(rightLabelX, rightY, labelWidth, dropdownHeight));
        m_threadCountDropdown->setBounds(NomadUI::NUIRect(rightDropdownX, rightY, dropdownWidth, dropdownHeight));
        
        // Nomad Mode dropdown (signature feature)
        rightY += dropdownHeight + sectionSpacing;
        m_nomadModeLabel->setBounds(NomadUI::NUIRect(rightLabelX, rightY, labelWidth, dropdownHeight));
        m_nomadModeDropdown->setBounds(NomadUI::NUIRect(rightDropdownX, rightY, dropdownWidth, dropdownHeight));
        
        // Hide ASIO info label
        m_asioInfoLabel->setBounds(NomadUI::NUIRect(0, 0, 0, 0));
    }
    
	    // Position buttons at bottom right with consistent padding
	    float buttonY = m_dialogBounds.y + m_dialogBounds.height - buttonHeight - padding;
	    float buttonX = m_dialogBounds.x + m_dialogBounds.width - (buttonWidth * 2 + buttonSpacing) - padding;
    
    // Apply button
    m_applyButton->setBounds(NomadUI::NUIRect(buttonX, buttonY, buttonWidth, buttonHeight));
    
    // Cancel button
    m_cancelButton->setBounds(NomadUI::NUIRect(buttonX + buttonWidth + buttonSpacing, buttonY, buttonWidth, buttonHeight));
}

void AudioSettingsDialog::renderBackground(NomadUI::NUIRenderer& renderer) {
    // Render semi-transparent overlay (reduced from 0.8 to 0.6 for better FPS)
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    NomadUI::NUIColor overlayColor = themeManager.getColor("backgroundPrimary");
    overlayColor = overlayColor.withAlpha(0.6f);
    
    NomadUI::NUIRect overlay(0, 0,
                             static_cast<float>(renderer.getWidth()),
                             static_cast<float>(renderer.getHeight()));
    renderer.fillRect(overlay, overlayColor);
}

void AudioSettingsDialog::renderDialog(NomadUI::NUIRenderer& renderer) {
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // Dialog background with subtle gradient
    NomadUI::NUIColor bgColor = themeManager.getColor("backgroundSecondary");
    renderer.fillRoundedRect(m_dialogBounds, 12, bgColor);
    
    // Accent border with blink effect - flashes ALERT RED when clicked outside
    NomadUI::NUIColor accentColor = themeManager.getColor("accent");
    NomadUI::NUIColor normalBorder = accentColor.withAlpha(0.3f);
    NomadUI::NUIColor blinkBorder = NomadUI::NUIColor(1.0f, 0.0f, 0.0f, 0.5f); // Alert red at 50% opacity
    
    // Create double blink effect with discrete pulses - always consistent
    float blinkValue = 0.0f;
    if (m_blinkAnimation > 0.0f) {
        float progress = 1.0f - m_blinkAnimation; // 0 to 1
        
        // First blink: 0.0 - 0.35
        // Gap: 0.35 - 0.5
        // Second blink: 0.5 - 0.85
        // Fade out: 0.85 - 1.0
        
        if (progress < 0.35f) {
            // First blink - ramp up then down
            float t = progress / 0.35f; // 0 to 1
            blinkValue = std::sin(t * 3.14159f); // Smooth pulse
        } else if (progress >= 0.5f && progress < 0.85f) {
            // Second blink - ramp up then down
            float t = (progress - 0.5f) / 0.35f; // 0 to 1
            blinkValue = std::sin(t * 3.14159f); // Smooth pulse
        }
        // else blinkValue stays 0 (gaps and fade out)
    }
    
    // Interpolate between normal and blink color
    NomadUI::NUIColor borderColor(
        normalBorder.r + (blinkBorder.r - normalBorder.r) * blinkValue,
        normalBorder.g + (blinkBorder.g - normalBorder.g) * blinkValue,
        normalBorder.b + (blinkBorder.b - normalBorder.b) * blinkValue,
        normalBorder.a + (blinkBorder.a - normalBorder.a) * blinkValue
    );
    
    float borderWidth = 2.0f + (blinkValue * 2.0f); // Thicker border when blinking
    renderer.strokeRoundedRect(m_dialogBounds, 12, borderWidth, borderColor);
    
    // Title bar area - INSIDE the dialog bounds with proper inset (more compact)
    NomadUI::NUIRect titleBar(m_dialogBounds.x + 3, m_dialogBounds.y + 3, m_dialogBounds.width - 6, 42);
    renderer.fillRoundedRect(titleBar, 9, bgColor.lightened(0.05f));
    
    // Title + subtitle: use font metrics for consistent baseline alignment
    NomadUI::NUIColor textColor = themeManager.getColor("textPrimary");
    const float titleFontSize = 14.0f;
    const float subtitleFontSize = 10.0f;
    const float titleX = titleBar.x + 18.0f;
    const float headerPadTop = 5.0f;
    const float headerPadBottom = 5.0f;
    const float titleSubtitleGap = 2.0f;
    const float titleLineHeight = 18.0f;

    const NomadUI::NUIRect titleLineRect(titleX, titleBar.y + headerPadTop, titleBar.width, titleLineHeight);
    const float titleY = std::round(renderer.calculateTextY(titleLineRect, titleFontSize));
    renderer.drawText("Audio Settings", NomadUI::NUIPoint(titleX, titleY), titleFontSize, textColor);
    
    // Close button (X) - symmetrical cross
    float closeSize = 28.0f;
    float closeX = titleBar.x + titleBar.width - closeSize - 10;
    float closeY = titleBar.y + (titleBar.height - closeSize) / 2; // Vertically centered
    m_closeButtonBounds = NomadUI::NUIRect(closeX, closeY, closeSize, closeSize);
    
    // Classic red hover effect
    NomadUI::NUIColor closeColor = m_closeButtonHovered 
        ? NomadUI::NUIColor(0.9f, 0.2f, 0.2f, 1.0f) // Classic red
        : textColor.withAlpha(0.7f); // Normal gray
    
    // Draw symmetrical X with equal-length diagonal lines
    float padding = 8.0f;
    float x1 = closeX + padding;
    float y1 = closeY + padding;
    float x2 = closeX + closeSize - padding;
    float y2 = closeY + closeSize - padding;
    
    // Draw two diagonal lines of equal length
    NomadUI::NUIPoint p1(x1, y1);
    NomadUI::NUIPoint p2(x2, y2);
    NomadUI::NUIPoint p3(x2, y1);
    NomadUI::NUIPoint p4(x1, y2);
    renderer.drawLine(p1, p2, 2.0f, closeColor); // Top-left to bottom-right
    renderer.drawLine(p3, p4, 2.0f, closeColor); // Top-right to bottom-left
    
    // Subtitle
    NomadUI::NUIColor subtitleColor = themeManager.getColor("textSecondary");
    const float subtitleTop = titleLineRect.bottom() + titleSubtitleGap;
    const float subtitleHeight = std::max(0.0f, titleBar.bottom() - headerPadBottom - subtitleTop);
    const NomadUI::NUIRect subtitleLineRect(titleX + 2.0f, subtitleTop, titleBar.width, subtitleHeight);
    const float subtitleY = std::round(renderer.calculateTextY(subtitleLineRect, subtitleFontSize));
    renderer.drawText("Configure your audio hardware and performance",
                      NomadUI::NUIPoint(titleX + 2.0f, subtitleY), subtitleFontSize, subtitleColor);
    
    // === Column Dividers + Headers (settings tab only) ===
    if (m_activeTab == "settings") {
        // Mirror layout metrics from layoutComponents() so dividers/headers align.
        float columnSpacing = 20.0f;
        float labelWidth = 90.0f;
        float dropdownWidth = 190.0f;
        float columnWidth = labelWidth + dropdownWidth + 16.0f;

        float totalContentWidth = (columnWidth * 3) + (columnSpacing * 2);
        float startX = m_dialogBounds.x + (m_dialogBounds.width - totalContentWidth) / 2.0f;

        float leftColumnX = startX;
        float middleColumnX = startX + columnWidth + columnSpacing;
        float rightColumnX = startX + (columnWidth + columnSpacing) * 2;

        float divider1X = leftColumnX + columnWidth + columnSpacing * 0.5f;
        float divider2X = middleColumnX + columnWidth + columnSpacing * 0.5f;

        float tabBarY = m_dialogBounds.y + 55.0f; // Matches layoutComponents()
        float tabButtonHeight = 36.0f;
        float headerY = tabBarY + tabButtonHeight + 8.0f;
        float headerHeight = 20.0f;

        float dividerY1 = headerY - 6.0f; // Starts below the tab buttons
        float dividerY2 = m_dialogBounds.y + m_dialogBounds.height - 50.0f; // Above buttons

        NomadUI::NUIColor dividerColor = themeManager.getColor("textSecondary").withAlpha(0.15f);
        renderer.drawLine(NomadUI::NUIPoint(divider1X, dividerY1),
                          NomadUI::NUIPoint(divider1X, dividerY2),
                          1.0f, dividerColor);
        renderer.drawLine(NomadUI::NUIPoint(divider2X, dividerY1),
                          NomadUI::NUIPoint(divider2X, dividerY2),
                          1.0f, dividerColor);

	        // Column headers with subtle background
	        NomadUI::NUIColor headerBgColor = bgColor.lightened(0.03f);
	        NomadUI::NUIColor headerTextColor = themeManager.getColor("textSecondary").withAlpha(0.85f);
	        float headerInset = 2.0f;

	        NomadUI::NUIRect leftHeaderBg(leftColumnX + headerInset, headerY, columnWidth - headerInset * 2, headerHeight);
	        renderer.fillRoundedRect(leftHeaderBg, 4, headerBgColor);
	        renderer.drawText("Audio Device",
	                          NomadUI::NUIPoint(leftHeaderBg.x + 8, std::round(renderer.calculateTextY(leftHeaderBg, 11.0f))),
	                          11.0f, headerTextColor);

	        NomadUI::NUIRect middleHeaderBg(middleColumnX + headerInset, headerY, columnWidth - headerInset * 2, headerHeight);
	        renderer.fillRoundedRect(middleHeaderBg, 4, headerBgColor);
	        renderer.drawText("Quality",
	                          NomadUI::NUIPoint(middleHeaderBg.x + 8, std::round(renderer.calculateTextY(middleHeaderBg, 11.0f))),
	                          11.0f, headerTextColor);

	        NomadUI::NUIRect rightHeaderBg(rightColumnX + headerInset, headerY, columnWidth - headerInset * 2, headerHeight);
	        renderer.fillRoundedRect(rightHeaderBg, 4, headerBgColor);
	        renderer.drawText("Processing",
	                          NomadUI::NUIPoint(rightHeaderBg.x + 8, std::round(renderer.calculateTextY(rightHeaderBg, 11.0f))),
	                          11.0f, headerTextColor);
	    }
    
    // Error message (if any) - displayed at bottom left with fade animation
    if (m_errorMessageAlpha > 0.0f && !m_errorMessage.empty()) {
        NomadUI::NUIColor errorColor = NomadUI::NUIColor(1.0f, 0.3f, 0.2f, m_errorMessageAlpha); // Red with fade
        
        float buttonHeight = 32.0f; // From layoutComponents
        float padding = 20.0f;
        float errorY = m_dialogBounds.y + m_dialogBounds.height - buttonHeight - padding + 8.0f; // Vertically center with buttons
        float errorX = m_dialogBounds.x + padding;
        
        renderer.drawText(m_errorMessage, 
                         NomadUI::NUIPoint(errorX, errorY), 12, errorColor);
    }
}

void AudioSettingsDialog::playTestSound() {
    if (!m_audioManager) {
        Nomad::Log::error("AudioManager is null, cannot play test sound");
        return;
    }
    
    if (m_isPlayingTestSound) {
        Nomad::Log::warning("Test sound already playing");
        return;
    }
    
    Nomad::Log::info("Starting test sound playback...");
    
    // Just set flag - tone will be generated in Main's audio callback
    m_isPlayingTestSound = true;
    m_testSoundPhase = 0.0;
    m_testSoundButton->setText("Stop Test");
    if (m_playIcon) {
        m_playIcon->loadSVG(kTestSoundStopSvg);
        m_playIcon->setColorFromTheme("error");
    }
    m_cacheInvalidated = true; // Text changed, invalidate cache
    
    Nomad::Log::info("Test sound started! Flag set to TRUE");
}

void AudioSettingsDialog::stopTestSound() {
    if (!m_isPlayingTestSound) return;
    
    m_isPlayingTestSound = false;
    m_testSoundButton->setText("Test Sound");
    if (m_playIcon) {
        m_playIcon->loadSVG(kTestSoundPlaySvg);
        m_playIcon->setColorFromTheme("primary");
    }
    m_cacheInvalidated = true; // Text changed, invalidate cache
    Nomad::Log::info("Test sound stopped - Flag set to FALSE");
}


Nomad::Audio::ResamplingMode AudioSettingsDialog::getSelectedResamplingMode() const {
    if (m_resamplingDropdown) {
        return static_cast<Nomad::Audio::ResamplingMode>(m_resamplingDropdown->getSelectedValue());
    }
    return Nomad::Audio::ResamplingMode::Medium;
}

Nomad::Audio::DitheringMode AudioSettingsDialog::getSelectedDitheringMode() const {
    if (m_ditheringDropdown) {
        return static_cast<Nomad::Audio::DitheringMode>(m_ditheringDropdown->getSelectedValue());
    }
    return Nomad::Audio::DitheringMode::None;
}

} // namespace Nomad
