// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
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
    // Play icon (Rounded Triangle) - Electric Purple
    const char* playSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M8 6.82v10.36c0 .79.87 1.27 1.54.84l8.14-5.18c.62-.39.62-1.29 0-1.69L9.54 5.98C8.87 5.55 8 6.03 8 6.82z"/>
        </svg>
    )";
    m_playIcon = std::make_shared<NomadUI::NUIIcon>(playSvg);
    m_playIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_playIcon->setColorFromTheme("primary");  // Use primary theme color
    
    // Pause icon (Thicker Bars)
    const char* pauseSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M8 19c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2s-2 .9-2 2v10c0 1.1.9 2 2 2zm6-12v10c0 1.1.9 2 2 2s2-.9 2-2V7c0-1.1-.9-2-2-2s-2 .9-2 2z"/>
        </svg>
    )";
    m_pauseIcon = std::make_shared<NomadUI::NUIIcon>(pauseSvg);
    m_pauseIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_pauseIcon->setColorFromTheme("primary");
    
    // Stop icon (Rounded Square)
    const char* stopSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M8 6h8c1.1 0 2 .9 2 2v8c0 1.1-.9 2-2 2H8c-1.1 0-2-.9-2-2V8c0-1.1.9-2 2-2z"/>
        </svg>
    )";
    m_stopIcon = std::make_shared<NomadUI::NUIIcon>(stopSvg);
    m_stopIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_stopIcon->setColorFromTheme("primary");
    
    // Record icon (Solid Circle) - Vibrant Red
    const char* recordSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <circle cx="12" cy="12" r="9"/>
        </svg>
    )";
    m_recordIcon = std::make_shared<NomadUI::NUIIcon>(recordSvg);
    m_recordIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_recordIcon->setColorFromTheme("error");  // #ff4d4d - Clear red for recording

    // Mixer icon (Stylized Sliders)
    const char* mixerSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M5 15h2v4H5v-4zm0-10h2v8H5V5zm6 12h2v2h-2v-2zm0-12h2v10h-2V5zm6 8h2v6h-2v-6zm0-8h2v6h-2V5z"/>
        </svg>
    )";
    m_mixerIcon = std::make_shared<NomadUI::NUIIcon>(mixerSvg);
    m_mixerIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_mixerIcon->setColorFromTheme("textSecondary");

    // Sequencer icon (Grid)
    const char* sequencerSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M4 4h4v4H4V4zm6 0h4v4h-4V4zm6 0h4v4h-4V4zM4 10h4v4H4v-4zm6 0h4v4h-4v-4zm6 0h4v4h-4v-4zM4 16h4v4H4v-4zm6 0h4v4h-4v-4zm6 0h4v4h-4v-4z"/>
        </svg>
    )";
    m_sequencerIcon = std::make_shared<NomadUI::NUIIcon>(sequencerSvg);
    m_sequencerIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_sequencerIcon->setColorFromTheme("textSecondary");

    // Piano Roll icon (keys)
    const char* pianoRollSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M20 2H4c-1.1 0-2 .9-2 2v16c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zm-5.5 17h-2.5v-7h2.5v7zm-4.5 0H7.5v-7h2.5v7zM20 19h-2.5v-7H20v7z"/>
        </svg>
    )";
    m_pianoRollIcon = std::make_shared<NomadUI::NUIIcon>(pianoRollSvg);
    m_pianoRollIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_pianoRollIcon->setColorFromTheme("textSecondary");

    // Playlist icon (tracks)
    const char* playlistSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M3 13h8v-2H3v2zm0 4h8v-2H3v2zm0-8h8V7H3v2zm10-6v18h8V3h-8zm6 16h-4V5h4v14z"/>
        </svg>
    )";
    m_playlistIcon = std::make_shared<NomadUI::NUIIcon>(playlistSvg);
    m_playlistIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_playlistIcon->setColorFromTheme("textSecondary");
}

void TransportBar::createButtons() {
    // Play/Pause button
    m_playButton = std::make_shared<NomadUI::NUIButton>();
    m_playButton->setText("");
    m_playButton->setStyle(NomadUI::NUIButton::Style::Icon);
    m_playButton->setSize(40, 40);
    // FLAT DESIGN: Removed background color for cleaner look
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    // Explicitly set transparent background to ensure no "black box"
    m_playButton->setBackgroundColor(NomadUI::NUIColor(0,0,0,0)); 
    // Hover/Press colors handled by theme now
    m_playButton->setOnClick([this]() {
        togglePlayPause();
    });
    addChild(m_playButton);
    
    // Stop button
    m_stopButton = std::make_shared<NomadUI::NUIButton>();
    m_stopButton->setText("");
    m_stopButton->setStyle(NomadUI::NUIButton::Style::Icon);
    m_stopButton->setSize(40, 40);
    // FLAT DESIGN: Removed background color
    m_stopButton->setBackgroundColor(NomadUI::NUIColor(0,0,0,0));
    // Hover/Press colors handled by theme now
    m_stopButton->setOnClick([this]() {
        stop();
    });
    addChild(m_stopButton);
    
    // Record button (for future use)
    m_recordButton = std::make_shared<NomadUI::NUIButton>();
    m_recordButton->setText("");
    m_recordButton->setStyle(NomadUI::NUIButton::Style::Icon);
    m_recordButton->setSize(40, 40);
    // FLAT DESIGN: Removed background color
    m_recordButton->setBackgroundColor(NomadUI::NUIColor(0,0,0,0));
    // Hover/Press colors handled by theme now
    m_recordButton->setEnabled(false); // Disabled for now
    addChild(m_recordButton);

    // Helper to create view toggle buttons
    auto createViewButton = [&](std::shared_ptr<NomadUI::NUIButton>& btn, std::function<void()> onClick) {
        btn = std::make_shared<NomadUI::NUIButton>();
        btn->setText("");
        btn->setStyle(NomadUI::NUIButton::Style::Icon);
        btn->setSize(40, 40);
        // FLAT DESIGN: Removed background color
        btn->setBackgroundColor(NomadUI::NUIColor(0,0,0,0));
        // Hover/Press colors handled by theme now
        btn->setOnClick(onClick);
        addChild(btn);
    };

    createViewButton(m_mixerButton, [this]() { if (m_onToggleMixer) m_onToggleMixer(); });
    createViewButton(m_sequencerButton, [this]() { if (m_onToggleSequencer) m_onToggleSequencer(); });
    createViewButton(m_pianoRollButton, [this]() { if (m_onTogglePianoRoll) m_onTogglePianoRoll(); });
    createViewButton(m_playlistButton, [this]() { if (m_onTogglePlaylist) m_onTogglePlaylist(); });
}

void TransportBar::play() {
    if (m_state != TransportState::Playing) {
        m_state = TransportState::Playing;
        updateButtonStates();
        
        // Update timer to show playing state (green color)
        if (m_infoContainer) {
            m_infoContainer->getTimerDisplay()->setPlaying(true);
        }
        
        if (m_onPlay) {
            m_onPlay();
        }
    }
}

void TransportBar::pause() {
    if (m_state == TransportState::Playing) {
        m_state = TransportState::Paused;
        updateButtonStates();
        
        // Update timer to show stopped state (white color)
        if (m_infoContainer) {
            m_infoContainer->getTimerDisplay()->setPlaying(false);
        }
        
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
            // Update timer to show stopped state (white color)
            m_infoContainer->getTimerDisplay()->setPlaying(false);
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
    // Clear textual fallbacks (we render SVG icons instead)
    if (m_playButton) {
        m_playButton->setText("");
        m_playButton->setEnabled(true);
    }

    if (m_stopButton) {
        m_stopButton->setText("");
        m_stopButton->setEnabled(m_state != TransportState::Stopped);
    }

    if (m_recordButton) {
        m_recordButton->setText("");
        // Keep record disabled until recording is implemented
        m_recordButton->setEnabled(false);
    }
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
    
    const float iconSize = 24.0f;
    float iconPadding = (buttonSize - iconSize) * 0.5f;
    if (iconPadding < 0.0f) iconPadding = 0.0f;

    // Play/Pause button icon
    if (m_playButton && m_playIcon && m_pauseIcon) {
        NomadUI::NUIRect buttonRect = NUIAbsolute(bounds, x, centerOffsetY, buttonSize, buttonSize);
        auto icon = (m_state == TransportState::Playing) ? m_pauseIcon : m_playIcon;
        if (icon) {
            // CRITICAL: Green when playing, grey on hover, purple otherwise
            if (m_state == TransportState::Playing) {
                // Bright green when actively playing
                icon->setColor(themeManager.getColor("success"));
            } else if (m_playButton->isHovered()) {
                icon->setColor(themeManager.getColor("textSecondary"));
            } else {
                icon->setColor(themeManager.getColor("primary"));
            }
            
            NomadUI::NUIRect iconRect = NUIAbsolute(buttonRect, iconPadding, iconPadding, iconSize, iconSize);
            icon->setBounds(iconRect);
            icon->onRender(renderer);
        }
    }
    x += buttonSize + spacing;

    // Stop button icon
    if (m_stopButton && m_stopIcon) {
        NomadUI::NUIRect buttonRect = NUIAbsolute(bounds, x, centerOffsetY, buttonSize, buttonSize);

        if (!m_stopButton->isEnabled()) {
            m_stopIcon->setColor(themeManager.getColor("textSecondary").withAlpha(0.35f));
        } else if (m_stopButton->isHovered()) {
            m_stopIcon->setColor(themeManager.getColor("textSecondary"));
        } else {
            m_stopIcon->setColor(themeManager.getColor("primary"));
        }
        
        NomadUI::NUIRect iconRect = NUIAbsolute(buttonRect, iconPadding, iconPadding, iconSize, iconSize);
        m_stopIcon->setBounds(iconRect);
        m_stopIcon->onRender(renderer);
    }
    x += buttonSize + spacing;

    // Record button icon (always red, no hover change)
    if (m_recordButton && m_recordIcon) {
        NomadUI::NUIRect buttonRect = NUIAbsolute(bounds, x, centerOffsetY, buttonSize, buttonSize);

        if (!m_recordButton->isEnabled()) {
            m_recordIcon->setColor(themeManager.getColor("textSecondary").withAlpha(0.35f));
        } else {
            m_recordIcon->setColorFromTheme("error");  // #ff4d4d - Red
        }
        
        NomadUI::NUIRect iconRect = NUIAbsolute(buttonRect, iconPadding, iconPadding, iconSize, iconSize);
        m_recordIcon->setBounds(iconRect);
        m_recordIcon->onRender(renderer);
    }
    x += buttonSize + spacing;

    // Calculate position for view toggles
    // Move to the right of the center (BPM display)
    float centerX = bounds.width / 2.0f;
    float viewButtonsX = centerX + 120.0f; // Offset from center to avoid BPM
    
    // Helper to render view icons
    auto renderViewIcon = [&](std::shared_ptr<NomadUI::NUIButton>& btn, std::shared_ptr<NomadUI::NUIIcon>& icon) {
        if (btn && icon) {
            NomadUI::NUIRect buttonRect = NUIAbsolute(bounds, viewButtonsX, centerOffsetY, buttonSize, buttonSize);
            
            if (btn->isHovered()) {
                icon->setColorFromTheme("textPrimary"); // White on hover
            } else {
                icon->setColorFromTheme("accent"); // Purple default
            }
            
            NomadUI::NUIRect iconRect = NUIAbsolute(buttonRect, iconPadding, iconPadding, iconSize, iconSize);
            icon->setBounds(iconRect);
            icon->onRender(renderer);
            
            viewButtonsX += buttonSize + spacing;
        }
    };

    renderViewIcon(m_mixerButton, m_mixerIcon);
    renderViewIcon(m_sequencerButton, m_sequencerIcon);
    renderViewIcon(m_pianoRollButton, m_pianoRollIcon);
    renderViewIcon(m_playlistButton, m_playlistIcon);
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
    
    // Calculate position for view toggles (Right side)
    float rightEdge = bounds.width;
    // View toggle buttons
    // Move to the right of the center (BPM display)
    float centerX = bounds.width / 2.0f;
    float viewButtonsX = centerX + 120.0f; // Offset from center to avoid BPM

    if (m_mixerButton) {
        m_mixerButton->setBounds(NUIAbsolute(bounds, viewButtonsX, centerOffsetY, buttonSize, buttonSize));
        viewButtonsX += buttonSize + spacing;
    }
    if (m_sequencerButton) {
        m_sequencerButton->setBounds(NUIAbsolute(bounds, viewButtonsX, centerOffsetY, buttonSize, buttonSize));
        viewButtonsX += buttonSize + spacing;
    }
    if (m_pianoRollButton) {
        m_pianoRollButton->setBounds(NUIAbsolute(bounds, viewButtonsX, centerOffsetY, buttonSize, buttonSize));
        viewButtonsX += buttonSize + spacing;
    }
    if (m_playlistButton) {
        m_playlistButton->setBounds(NUIAbsolute(bounds, viewButtonsX, centerOffsetY, buttonSize, buttonSize));
        viewButtonsX += buttonSize + spacing;
    }

    // Info container (timer + BPM) - spans full transport bar
    // Use NUIAbsolute with 0,0 offset to position at transport bar's absolute position
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
    
    // Add vertical separator between file browser and track area
    auto& layout = themeManager.getLayoutDimensions();
    float fileBrowserWidth = layout.fileBrowserWidth;
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x + fileBrowserWidth, bounds.y),
        NomadUI::NUIPoint(bounds.x + fileBrowserWidth, bounds.y + bounds.height),
        1.0f,
        borderColor.withAlpha(0.8f)
    );
    
    // Add horizontal divider at bottom to separate transport from track area
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x, bounds.y + bounds.height - 1),
        NomadUI::NUIPoint(bounds.x + bounds.width, bounds.y + bounds.height - 1),
        1.0f,
        borderColor.withAlpha(0.8f)
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
