/**
 * @file TransportBar.cpp
 * @brief Transport bar implementation
 */

#include "TransportBar.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace Nomad {

TransportBar::TransportBar()
    : NomadUI::NUIComponent()
    , m_state(TransportState::Stopped)
    , m_tempo(120.0f)
    , m_position(0.0)
{
    createIcons();
    createButtons();
    
    // Create modular info container
    m_infoContainer = std::make_shared<TransportInfoContainer>();
    addChild(m_infoContainer);
    
    // Wire up BPM change callback from arrows
    if (m_infoContainer && m_infoContainer->getBPMDisplay()) {
        m_infoContainer->getBPMDisplay()->setOnBPMChange([this](float newBPM) {
            m_tempo = newBPM;
            if (m_onTempoChange) {
                m_onTempoChange(m_tempo);
            }
        });
    }
    
    updateButtonStates();
}

void TransportBar::createIcons() {
    // Play icon (triangle) - Liminal Dark v2.0
    const char* playSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M8 5v14l11-7z"/>
        </svg>
    )";
    m_playIcon = std::make_shared<NomadUI::NUIIcon>(playSvg);
    m_playIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_playIcon->setColorFromTheme("accentCyan");  // #00bcd4 - Playful but professional blue
    
    // Pause icon (two bars)
    const char* pauseSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M6 4h4v16H6V4zm8 0h4v16h-4V4z"/>
        </svg>
    )";
    m_pauseIcon = std::make_shared<NomadUI::NUIIcon>(pauseSvg);
    m_pauseIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_pauseIcon->setColorFromTheme("accentCyan");  // #00bcd4 - Same as play
    
    // Stop icon (square)
    const char* stopSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <rect x="6" y="6" width="12" height="12"/>
        </svg>
    )";
    m_stopIcon = std::make_shared<NomadUI::NUIIcon>(stopSvg);
    m_stopIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_stopIcon->setColorFromTheme("textSecondary");  // #9a9aa3 - Inactive labels
    
    // Record icon (circle) - Liminal Dark v2.0
    const char* recordSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <circle cx="12" cy="12" r="8"/>
        </svg>
    )";
    m_recordIcon = std::make_shared<NomadUI::NUIIcon>(recordSvg);
    m_recordIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_recordIcon->setColorFromTheme("error");  // #ff4d4d - Clear red for recording
}

void TransportBar::createButtons() {
    // Play/Pause button
    m_playButton = std::make_shared<NomadUI::NUIButton>();
    m_playButton->setText("");
    m_playButton->setStyle(NomadUI::NUIButton::Style::Icon);
    m_playButton->setSize(40, 40);
    // Use theme color instead of hardcoded color
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    m_playButton->setBackgroundColor(themeManager.getColor("surfaceTertiary")); // #28282d - same as track buttons
    m_playButton->setOnClick([this]() {
        togglePlayPause();
    });
    addChild(m_playButton);
    
    // Stop button
    m_stopButton = std::make_shared<NomadUI::NUIButton>();
    m_stopButton->setText("");
    m_stopButton->setStyle(NomadUI::NUIButton::Style::Icon);
    m_stopButton->setSize(40, 40);
    // Use theme color instead of hardcoded color
    m_stopButton->setBackgroundColor(themeManager.getColor("surfaceTertiary")); // #28282d - same as track buttons
    m_stopButton->setOnClick([this]() {
        stop();
    });
    addChild(m_stopButton);
    
    // Record button (for future use)
    m_recordButton = std::make_shared<NomadUI::NUIButton>();
    m_recordButton->setText("");
    m_recordButton->setStyle(NomadUI::NUIButton::Style::Icon);
    m_recordButton->setSize(40, 40);
    // Use theme color instead of hardcoded color
    m_recordButton->setBackgroundColor(themeManager.getColor("surfaceTertiary")); // #28282d - same as track buttons
    m_recordButton->setEnabled(false); // Disabled for now
    addChild(m_recordButton);
}

void TransportBar::play() {
    if (m_state != TransportState::Playing) {
        m_state = TransportState::Playing;
        updateButtonStates();
        if (m_onPlay) {
            m_onPlay();
        }
    }
}

void TransportBar::pause() {
    if (m_state == TransportState::Playing) {
        m_state = TransportState::Paused;
        updateButtonStates();
        if (m_onPause) {
            m_onPause();
        }
    }
}

void TransportBar::stop() {
    if (m_state != TransportState::Stopped) {
        m_state = TransportState::Stopped;
        m_position = 0.0;
        updateButtonStates();
        if (m_infoContainer) {
            m_infoContainer->getTimerDisplay()->setTime(m_position);
        }
        if (m_onStop) {
            m_onStop();
        }
    }
}

void TransportBar::togglePlayPause() {
    if (m_state == TransportState::Playing) {
        pause();
    } else {
        play();
    }
}

void TransportBar::setTempo(float bpm) {
    m_tempo = std::max(20.0f, std::min(999.0f, bpm));
    if (m_infoContainer) {
        m_infoContainer->getBPMDisplay()->setBPM(m_tempo);
    }
    if (m_onTempoChange) {
        m_onTempoChange(m_tempo);
    }
}

void TransportBar::setPosition(double seconds) {
    m_position = std::max(0.0, seconds);
    if (m_infoContainer) {
        m_infoContainer->getTimerDisplay()->setTime(m_position);
    }
}

void TransportBar::updateButtonStates() {
    // Update play button text based on state (using text for now)
    if (m_state == TransportState::Playing) {
        m_playButton->setText("||"); // Pause symbol
    } else {
        m_playButton->setText("▶"); // Play symbol
    }
    
    // Update stop button
    m_stopButton->setText("■"); // Stop symbol
    m_stopButton->setEnabled(m_state != TransportState::Stopped);
    
    // Update record button
    m_recordButton->setText("●"); // Record symbol
}

void TransportBar::renderButtonIcons(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();

    // Get layout dimensions from theme
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    // Calculate button positions (same as layoutComponents)
    float padding = layout.panelMargin;
    float buttonSize = layout.transportButtonSize;
    float spacing = layout.transportButtonSpacing;
    float centerOffsetY = (bounds.height - buttonSize) / 2.0f;
    float x = padding;

    // Play/Pause button icon
    if (m_playButton && m_playIcon && m_pauseIcon) {
        NomadUI::NUIRect buttonRect = NUIAbsolute(bounds, x, centerOffsetY, buttonSize, buttonSize);
        auto icon = (m_state == TransportState::Playing) ? m_pauseIcon : m_playIcon;
        if (icon) {
            float iconPadding = 8.0f; // Could be made configurable
            float iconSize = 24.0f;
            NomadUI::NUIRect iconRect = NUIAbsolute(buttonRect, iconPadding, iconPadding, iconSize, iconSize);
            icon->setBounds(iconRect);
            icon->onRender(renderer);
        }
    }
    x += buttonSize + spacing;

    // Stop button icon
    if (m_stopButton && m_stopIcon) {
        NomadUI::NUIRect buttonRect = NUIAbsolute(bounds, x, centerOffsetY, buttonSize, buttonSize);
        float iconPadding = 8.0f;
        float iconSize = 24.0f;
        NomadUI::NUIRect iconRect = NUIAbsolute(buttonRect, iconPadding, iconPadding, iconSize, iconSize);
        m_stopIcon->setBounds(iconRect);
        m_stopIcon->onRender(renderer);
    }
    x += buttonSize + spacing;

    // Record button icon
    if (m_recordButton && m_recordIcon) {
        NomadUI::NUIRect buttonRect = NUIAbsolute(bounds, x, centerOffsetY, buttonSize, buttonSize);
        float iconPadding = 8.0f;
        float iconSize = 24.0f;
        NomadUI::NUIRect iconRect = NUIAbsolute(buttonRect, iconPadding, iconPadding, iconSize, iconSize);
        m_recordIcon->setBounds(iconRect);
        m_recordIcon->onRender(renderer);
    }
}

void TransportBar::layoutComponents() {
    NomadUI::NUIRect bounds = getBounds();

    // Get layout dimensions from theme
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    // Use configurable dimensions
    float padding = layout.panelMargin;
    float buttonSize = layout.transportButtonSize;
    float spacing = layout.transportButtonSpacing;

    // Center vertically offset
    float centerOffsetY = (bounds.height - buttonSize) / 2.0f;

    // Layout buttons from left using utility helpers
    float x = padding;

    // Play button - using NUIAbsolute helper for cleaner code
    m_playButton->setBounds(NUIAbsolute(bounds, x, centerOffsetY, buttonSize, buttonSize));
    x += buttonSize + spacing;

    // Stop button
    m_stopButton->setBounds(NUIAbsolute(bounds, x, centerOffsetY, buttonSize, buttonSize));
    x += buttonSize + spacing;

    // Record button
    m_recordButton->setBounds(NUIAbsolute(bounds, x, centerOffsetY, buttonSize, buttonSize));

    // Info container (timer + BPM) - full transport bar dimensions
    if (m_infoContainer) {
        m_infoContainer->setBounds(NUIAbsolute(bounds, 0, 0, bounds.width, bounds.height));
    }
}

void TransportBar::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    
    // Get Liminal Dark v2.0 theme colors
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    NomadUI::NUIColor bgColor = themeManager.getColor("backgroundPrimary");  // #19191c - Same black as title bar
    NomadUI::NUIColor borderColor = themeManager.getColor("border");           // #2e2e35 - Subtle separation lines
    NomadUI::NUIColor accentCyan = themeManager.getColor("accentCyan");        // #00bcd4 - Accent cyan
    NomadUI::NUIColor accentMagenta = themeManager.getColor("accentMagenta");  // #ff4081 - Accent magenta
    
    // Solid background (no gradient) - same black as title bar
    renderer.fillRect(bounds, bgColor);
    
    // Enhanced border with subtle glow
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x, bounds.y),
        NomadUI::NUIPoint(bounds.x + bounds.width, bounds.y),
        1.0f,
        borderColor.withAlpha(0.6f)
    );
    
    // Add subtle inner highlight
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x, bounds.y + 1),
        NomadUI::NUIPoint(bounds.x + bounds.width, bounds.y + 1),
        1.0f,
        NomadUI::NUIColor::white().withAlpha(0.05f)
    );
    
    // Render children (buttons and labels)
    renderChildren(renderer);
    
    // Render custom icons on top of buttons
    renderButtonIcons(renderer);
}

void TransportBar::onResize(int width, int height) {
    // Don't reset bounds here - parent has already set the correct position
    // Just update the size while preserving x,y position
    NomadUI::NUIRect currentBounds = getBounds();
    setBounds(NomadUI::NUIRect(currentBounds.x, currentBounds.y, width, height));
    layoutComponents();
    NomadUI::NUIComponent::onResize(width, height);
}

bool TransportBar::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    // Let children handle mouse events first
    return NomadUI::NUIComponent::onMouseEvent(event);
}

} // namespace Nomad
