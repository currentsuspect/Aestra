/**
 * @file AudioSettingsDialog.cpp
 * @brief Audio settings dialog for NOMAD DAW
 */

#include "AudioSettingsDialog.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"
#include "../NomadAudio/include/AudioDriverTypes.h"
#include "../NomadAudio/include/ASIODriverInfo.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace Nomad {

AudioSettingsDialog::AudioSettingsDialog(Audio::AudioDeviceManager* audioManager,
                                        std::shared_ptr<Audio::TrackManager> trackManager)
    : m_audioManager(audioManager)
    , m_trackManager(trackManager)
    , m_visible(false)
    , m_dialogBounds(0, 0, 520, 520)
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
{
    createUI();
    loadCurrentSettings();
}

void AudioSettingsDialog::createUI() {
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
        Nomad::Log::info("Driver dropdown changed: index=" + std::to_string(index) + 
                        ", value=" + std::to_string(value) + ", text=" + text);
        m_selectedDriverType = static_cast<Audio::AudioDriverType>(value);
        Nomad::Log::info("m_selectedDriverType now = " + std::to_string(static_cast<int>(m_selectedDriverType)));
    });
    addChild(m_driverDropdown);

    m_deviceDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_deviceDropdown->setPlaceholderText("Select Audio Device");
    m_deviceDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        m_selectedDeviceId = static_cast<uint32_t>(value);
    });
    addChild(m_deviceDropdown);

    m_sampleRateDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_sampleRateDropdown->setPlaceholderText("Select Sample Rate");
    m_sampleRateDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        m_selectedSampleRate = static_cast<uint32_t>(value);
    });
    addChild(m_sampleRateDropdown);

    m_bufferSizeDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_bufferSizeDropdown->setPlaceholderText("Select Buffer Size");
    m_bufferSizeDropdown->setOnSelectionChanged([this](int index, int value, const std::string& text) {
        m_selectedBufferSize = static_cast<uint32_t>(value);
    });
    addChild(m_bufferSizeDropdown);

    // Create buttons
    m_applyButton = std::make_shared<NomadUI::NUIButton>();
    m_applyButton->setText("Apply");
    m_applyButton->setStyle(NomadUI::NUIButton::Style::Secondary); // Secondary style
    m_applyButton->setOnClick([this]() {
        applySettings();
    });
    addChild(m_applyButton);
    
    m_cancelButton = std::make_shared<NomadUI::NUIButton>();
    m_cancelButton->setText("Cancel");
    m_cancelButton->setStyle(NomadUI::NUIButton::Style::Secondary); // Secondary style
    m_cancelButton->setOnClick([this]() {
        cancelSettings();
    });
    addChild(m_cancelButton);
    
    m_testSoundButton = std::make_shared<NomadUI::NUIButton>();
    m_testSoundButton->setText("Test Sound");
    m_testSoundButton->setStyle(NomadUI::NUIButton::Style::Secondary); // Secondary style
    m_testSoundButton->setOnClick([this]() {
        Nomad::Log::info("Test sound button clicked!");
        if (m_isPlayingTestSound) {
            Nomad::Log::info("Stopping test sound...");
            stopTestSound();
        } else {
            Nomad::Log::info("Starting test sound...");
            playTestSound();
        }
    });
    addChild(m_testSoundButton);
    
    // Create play icon for test button (SVG)
    const char* playSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M8 5v14l11-7z"/>
        </svg>
    )";
    m_playIcon = std::make_shared<NomadUI::NUIIcon>(playSvg);
    m_playIcon->setIconSize(NomadUI::NUIIconSize::Small);  // Small for compact button
    m_playIcon->setColorFromTheme("accentCyan");  // Match transport bar style
    
    // Load lists
    updateDriverList();
    updateDeviceList();
    updateSampleRateList();
    updateBufferSizeList();
    updateASIOInfo();
}

void AudioSettingsDialog::show() {
    m_visible = true;
    setVisible(true);
    loadCurrentSettings();
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
    
    // Render background overlay
    renderBackground(renderer);
    
    // Render dialog
    renderDialog(renderer);
    
    // Render all children (buttons)
    NomadUI::NUIComponent::onRender(renderer);
    
    // Render play icon on Test Sound button (SVG)
    if (m_testSoundButton && m_playIcon) {
        auto bounds = m_testSoundButton->getBounds();
        
        // Position icon on left side of button with padding
        float iconPadding = 10.0f;
        float iconX = bounds.x + iconPadding;
        float iconY = bounds.y + (bounds.height - m_playIcon->getSize().height) / 2.0f;
        
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
}

void AudioSettingsDialog::onResize(int width, int height) {
    // Center dialog
    m_dialogBounds.x = (width - m_dialogBounds.width) / 2;
    m_dialogBounds.y = (height - m_dialogBounds.height) / 2;
    
        layoutComponents();
}

void AudioSettingsDialog::onUpdate(double deltaTime) {
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
        }
        setDirty(true);
    }
    
    // Call parent to update children
    NomadUI::NUIComponent::onUpdate(deltaTime);
}

bool AudioSettingsDialog::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    if (!m_visible) return false;
    
    // Track hover state for close button
    bool wasHovered = m_closeButtonHovered;
    m_closeButtonHovered = m_closeButtonBounds.contains(event.position.x, event.position.y);
    
    // Redraw if hover state changed
    if (wasHovered != m_closeButtonHovered) {
        setDirty(true);
    }
    
    // Check if click is on close button
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        if (m_closeButtonHovered) {
            hide();
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
    
    // Let children handle events (buttons will handle their own clicks)
    return NomadUI::NUIComponent::onMouseEvent(event);
}

bool AudioSettingsDialog::onKeyEvent(const NomadUI::NUIKeyEvent& event) {
    if (!m_visible) return false;
    
    if (event.pressed) {
        if (event.keyCode == NomadUI::NUIKeyCode::Escape) {
            hide();
            return true;
        }
    }
    
    return NomadUI::NUIComponent::onKeyEvent(event);
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
    m_asioDrivers = m_audioManager->getASIODrivers();
    for (const auto& asioDriver : m_asioDrivers) {
        std::string name = "ASIO: " + asioDriver.name + " [Not Yet Implemented]";
        m_driverDropdown->addItem(name, static_cast<int>(Audio::AudioDriverType::ASIO_EXTERNAL));
        m_driverDropdown->setItemEnabled(itemIndex, false);  // Disable ASIO items
        itemIndex++;
    }
}

void AudioSettingsDialog::updateASIOInfo() {
    if (!m_audioManager) return;
    
    m_asioDrivers = m_audioManager->getASIODrivers();
    
    if (!m_asioDrivers.empty()) {
        std::string infoText = "ASIO: ";
        for (size_t i = 0; i < m_asioDrivers.size(); ++i) {
            infoText += m_asioDrivers[i].name;
            if (i < m_asioDrivers.size() - 1) {
                infoText += ", ";
            }
        }
        m_asioInfoLabel->setText(infoText);
    } else {
        m_asioInfoLabel->setText("No ASIO drivers detected");
    }
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
            
            // Restore dropdown to show actual value
            m_selectedBufferSize = m_originalBufferSize;
            updateBufferSizeList();
        }
    }
    
    // DON'T close dialog on Apply - let user close manually
    // hide(); // REMOVED
    
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
    
    // Restore original settings
    m_selectedDriverType = m_originalDriverType;
    m_selectedDeviceId = m_originalDeviceId;
    m_selectedSampleRate = m_originalSampleRate;
    m_selectedBufferSize = m_originalBufferSize;
    
    // Close dialog
    hide();
    
    // Notify callback
    if (m_onCancel) {
        m_onCancel();
    }
}

void AudioSettingsDialog::layoutComponents() {
    if (!m_visible) return;
    
    float padding = 24.0f;
    float labelWidth = 120.0f;
    float dropdownWidth = 320.0f;
    float dropdownHeight = 36.0f;
    float buttonWidth = 110.0f;
    float buttonHeight = 38.0f;
    float buttonSpacing = 12.0f;
    float verticalSpacing = 20.0f;
    float sectionSpacing = 28.0f;
    
    // Start position for components (below title bar)
    float startY = m_dialogBounds.y + 70.0f;
    float labelX = m_dialogBounds.x + padding;
    float dropdownX = labelX + labelWidth + 16.0f;
    
    // Driver selector
    m_driverLabel->setBounds(NomadUI::NUIRect(labelX, startY, labelWidth, dropdownHeight));
    m_driverDropdown->setBounds(NomadUI::NUIRect(dropdownX, startY, dropdownWidth, dropdownHeight));
    
    // Device selector
    startY += dropdownHeight + verticalSpacing;
    m_deviceLabel->setBounds(NomadUI::NUIRect(labelX, startY, labelWidth, dropdownHeight));
    m_deviceDropdown->setBounds(NomadUI::NUIRect(dropdownX, startY, dropdownWidth, dropdownHeight));
    
    // Sample rate selector
    startY += dropdownHeight + sectionSpacing;
    m_sampleRateLabel->setBounds(NomadUI::NUIRect(labelX, startY, labelWidth, dropdownHeight));
    m_sampleRateDropdown->setBounds(NomadUI::NUIRect(dropdownX, startY, dropdownWidth, dropdownHeight));
    
    // Buffer size selector
    startY += dropdownHeight + verticalSpacing;
    m_bufferSizeLabel->setBounds(NomadUI::NUIRect(labelX, startY, labelWidth, dropdownHeight));
    m_bufferSizeDropdown->setBounds(NomadUI::NUIRect(dropdownX, startY, dropdownWidth, dropdownHeight));
    
    // ASIO info label (informational text)
    startY += dropdownHeight + sectionSpacing + 5.0f;
    float asioInfoWidth = dropdownWidth + labelWidth + 16.0f;
    m_asioInfoLabel->setBounds(NomadUI::NUIRect(labelX, startY, asioInfoWidth, 28.0f));
    
    // Test sound button (centered, compact with icon)
    startY += 50.0f + verticalSpacing;
    float testButtonWidth = 120.0f; // More compact
    float testButtonHeight = 36.0f; // Slightly smaller
    float testButtonX = m_dialogBounds.x + (m_dialogBounds.width - testButtonWidth) / 2;
    m_testSoundButton->setBounds(NomadUI::NUIRect(testButtonX, startY, testButtonWidth, testButtonHeight));
    
    // Position buttons at bottom right
    float buttonY = m_dialogBounds.y + m_dialogBounds.height - buttonHeight - padding;
    float buttonX = m_dialogBounds.x + m_dialogBounds.width - (buttonWidth * 2 + buttonSpacing) - padding;
    
    // Apply button
    m_applyButton->setBounds(NomadUI::NUIRect(buttonX, buttonY, buttonWidth, buttonHeight));
    
    // Cancel button
    m_cancelButton->setBounds(NomadUI::NUIRect(buttonX + buttonWidth + buttonSpacing, buttonY, buttonWidth, buttonHeight));
}

void AudioSettingsDialog::renderBackground(NomadUI::NUIRenderer& renderer) {
    // Render semi-transparent overlay
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    NomadUI::NUIColor overlayColor = themeManager.getColor("backgroundPrimary");
    overlayColor = overlayColor.withAlpha(0.8f);
    
    NomadUI::NUIRect overlay(0, 0, 2000, 2000); // Large overlay
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
    
    // Title bar area - INSIDE the dialog bounds with proper inset
    NomadUI::NUIRect titleBar(m_dialogBounds.x + 3, m_dialogBounds.y + 3, m_dialogBounds.width - 6, 50);
    renderer.fillRoundedRect(titleBar, 9, bgColor.lightened(0.05f));
    
    // Title text (lowered a bit)
    NomadUI::NUIColor textColor = themeManager.getColor("textPrimary");
    float titleY = titleBar.y + 22; // Relative to titleBar
    float titleX = titleBar.x + 22;
    renderer.drawText("Audio Settings", NomadUI::NUIPoint(titleX, titleY), 20, textColor);
    
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
    renderer.drawText("Configure your audio hardware and performance", 
                     NomadUI::NUIPoint(titleX + 2, titleY + 24), 11, subtitleColor);
    
    // Error message (if any) - displayed below subtitle with fade animation
    if (m_errorMessageAlpha > 0.0f && !m_errorMessage.empty()) {
        NomadUI::NUIColor errorColor = NomadUI::NUIColor(1.0f, 0.3f, 0.2f, m_errorMessageAlpha); // Red with fade
        float errorY = titleY + 50; // Below subtitle
        renderer.drawText(m_errorMessage, 
                         NomadUI::NUIPoint(titleX + 2, errorY), 12, errorColor);
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
    
    Nomad::Log::info("Test sound started! Flag set to TRUE");
}

void AudioSettingsDialog::stopTestSound() {
    if (!m_isPlayingTestSound) return;
    
    m_isPlayingTestSound = false;
    m_testSoundButton->setText("Test Sound");
    Nomad::Log::info("Test sound stopped - Flag set to FALSE");
}

} // namespace Nomad

