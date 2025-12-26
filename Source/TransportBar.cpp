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

    // Piano Roll icon (MIDI Grid + Vertical Keys)
    const char* pianoRollSvg = R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <rect x="2" y="4" width="20" height="16" rx="2" stroke="currentColor" stroke-width="1.5"/>
            <line x1="7" y1="4" x2="7" y2="20" stroke="currentColor" stroke-width="1"/>
            <line x1="2" y1="8" x2="7" y2="8" stroke="currentColor" stroke-width="1"/>
            <line x1="2" y1="12" x2="7" y2="12" stroke="currentColor" stroke-width="1"/>
            <line x1="2" y1="16" x2="7" y2="16" stroke="currentColor" stroke-width="1"/>
            <rect x="10" y="6" width="6" height="3" rx="1" fill="currentColor"/>
            <rect x="15" y="10" width="4" height="3" rx="1" fill="currentColor"/>
            <rect x="9" y="14" width="8" height="3" rx="1" fill="currentColor"/>
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

    // Metronome icon (classic metronome shape)
    const char* metronomeSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M12 1.5L6 22h12L12 1.5zM11 8l1-3 1 3v6h-2V8z"/>
            <circle cx="12" cy="18" r="2"/>
        </svg>
    )";
    m_metronomeIcon = std::make_shared<NomadUI::NUIIcon>(metronomeSvg);
    m_metronomeIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_metronomeIcon->setColorFromTheme("textSecondary");

}

void TransportBar::createButtons() {
    // Play/Pause/Stop/Record...
    auto createBtn = [&](std::shared_ptr<NomadUI::NUIButton>& btn, std::function<void()> cb) {
        btn = std::make_shared<NomadUI::NUIButton>();
        btn->setText("");
        btn->setStyle(NomadUI::NUIButton::Style::Icon);
        btn->setSize(40, 40);
        btn->setBackgroundColor(NomadUI::NUIColor(0,0,0,0)); 
        btn->setOnClick(cb);
        addChild(btn);
    };

    createBtn(m_playButton, [this]() { togglePlayPause(); });
    createBtn(m_stopButton, [this]() { stop(); });
    createBtn(m_recordButton, [](){});
    m_recordButton->setEnabled(false);
    
    // Metronome toggle button
    createBtn(m_metronomeButton, [this]() {
        m_metronomeActive = !m_metronomeActive;
        if (m_onMetronomeToggle) {
            m_onMetronomeToggle(m_metronomeActive);
        }
        setDirty(true);
    });

    // View Toggles
    auto createViewButton = [&](std::shared_ptr<NomadUI::NUIButton>& btn, std::function<void()> onClick) {
        createBtn(btn, onClick);
    };

    createViewButton(m_mixerButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::Mixer); });
    createViewButton(m_sequencerButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::Sequencer); });
    createViewButton(m_pianoRollButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::PianoRoll); });
    createViewButton(m_playlistButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::Playlist); });
    
    // Add Dropdowns LAST to ensure Z-ordering

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

void TransportBar::setViewToggled(Audio::ViewType view, bool active) {
    switch (view) {
        case Audio::ViewType::Mixer: m_mixerActive = active; break;
        case Audio::ViewType::Sequencer: m_sequencerActive = active; break;
        case Audio::ViewType::PianoRoll: m_pianoRollActive = active; break;
        case Audio::ViewType::Playlist: m_playlistActive = active; break;
    }
    setDirty(true);
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
        
        // Draw True Glass highlight on hover
        if (m_playButton->isHovered()) {
             renderer.fillRoundedRect(buttonRect, 4.0f, themeManager.getColor("glassHover"));
             renderer.strokeRoundedRect(buttonRect, 4.0f, 1.0f, themeManager.getColor("glassBorder"));
        }

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

        // Draw True Glass highlight on hover
        if (m_stopButton->isHovered() && m_stopButton->isEnabled()) {
             renderer.fillRoundedRect(buttonRect, 4.0f, themeManager.getColor("glassHover"));
             renderer.strokeRoundedRect(buttonRect, 4.0f, 1.0f, themeManager.getColor("glassBorder"));
        }

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

    // Tools

    // Calculate position for view toggles and metronome
    // Move to the right of the center (BPM display)
    float centerX = bounds.width / 2.0f;
    
    // Metronome button icon - positioned to the LEFT of BPM display
    float metronomeX = centerX - 120.0f - buttonSize;
    if (m_metronomeButton && m_metronomeIcon) {
        NomadUI::NUIRect buttonRect = NUIAbsolute(bounds, metronomeX, centerOffsetY, buttonSize, buttonSize);
        
        if (m_metronomeActive) {
            // Active: cyan highlight
            renderer.fillRoundedRect(buttonRect, 4.0f, themeManager.getColor("glassActive"));
            renderer.strokeRoundedRect(buttonRect, 4.0f, 1.0f, themeManager.getColor("primary").withAlpha(0.4f));
            m_metronomeIcon->setColorFromTheme("primary");
        } else if (m_metronomeButton->isHovered()) {
            renderer.fillRoundedRect(buttonRect, 4.0f, themeManager.getColor("glassHover"));
            renderer.strokeRoundedRect(buttonRect, 4.0f, 1.0f, themeManager.getColor("glassBorder"));
            m_metronomeIcon->setColorFromTheme("textSecondary");
        } else {
            m_metronomeIcon->setColor(themeManager.getColor("textSecondary").withAlpha(0.5f));
        }
        
        NomadUI::NUIRect iconRect = NUIAbsolute(buttonRect, iconPadding, iconPadding, iconSize, iconSize);
        m_metronomeIcon->setBounds(iconRect);
        m_metronomeIcon->onRender(renderer);
    }
    
    float viewButtonsX = centerX + 120.0f; // Offset from center to avoid BPM
    
    // Helper to render view icons
    auto renderViewIcon = [&](std::shared_ptr<NomadUI::NUIButton>& btn, std::shared_ptr<NomadUI::NUIIcon>& icon, bool isActive) {
        if (btn && icon) {
            NomadUI::NUIRect buttonRect = NUIAbsolute(bounds, viewButtonsX, centerOffsetY, buttonSize, buttonSize);
            
            if (isActive) {
                // Background highlight for active state: Luminous Glass
                renderer.fillRoundedRect(buttonRect, 4.0f, themeManager.getColor("glassActive"));
                renderer.strokeRoundedRect(buttonRect, 4.0f, 1.0f, themeManager.getColor("primary").withAlpha(0.4f));
                icon->setColorFromTheme("primary"); 
            } else if (btn->isHovered()) {
                // Hover state: True Glass (neutral)
                renderer.fillRoundedRect(buttonRect, 4.0f, themeManager.getColor("glassHover"));
                renderer.strokeRoundedRect(buttonRect, 4.0f, 1.0f, themeManager.getColor("glassBorder"));
                icon->setColorFromTheme("primary"); 
            } else {
                icon->setColorFromTheme("textSecondary"); // Dimmer default for non-active
            }
            
            NomadUI::NUIRect iconRect = NUIAbsolute(buttonRect, iconPadding, iconPadding, iconSize, iconSize);
            icon->setBounds(iconRect);
            icon->onRender(renderer);
            
            viewButtonsX += buttonSize + spacing;
        }
    };

    renderViewIcon(m_mixerButton, m_mixerIcon, m_mixerActive);
    renderViewIcon(m_sequencerButton, m_sequencerIcon, m_sequencerActive);
    renderViewIcon(m_pianoRollButton, m_pianoRollIcon, m_pianoRollActive);
    renderViewIcon(m_playlistButton, m_playlistIcon, m_playlistActive);
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
    x += buttonSize + layout.panelMargin; // Extra margin after transport

    // Center of the transport bar (BPM display area)
    float centerX = bounds.width / 2.0f;
    
    // Metronome button - positioned to the LEFT of BPM display (mirroring mixer position on right)
    float metronomeX = centerX - 120.0f - buttonSize;  // Same offset as mixer from center, minus button width
    m_metronomeButton->setBounds(NUIAbsolute(bounds, metronomeX, centerOffsetY, buttonSize, buttonSize));

    
    // Calculate position for view toggles (Right side)
    float rightEdge = bounds.width;
    // View toggle buttons
    // Position to the right of center (BPM display) - centerX already declared above
    if (x > centerX - 100) { 
        // If our tools encroach on center, shift center or rely on manual absolute
    }
    
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
    
    // REMOVED: Vertical separator was slicing through Arsenal/Timeline buttons
    // auto& layout = themeManager.getLayoutDimensions();
    // float fileBrowserWidth = layout.fileBrowserWidth;
    // renderer.drawLine(
    //     NomadUI::NUIPoint(bounds.x + fileBrowserWidth, bounds.y),
    //     NomadUI::NUIPoint(bounds.x + fileBrowserWidth, bounds.y + bounds.height),
    //     1.0f,
    //     borderColor.withAlpha(0.8f)
    // );
    
    // Add horizontal divider at bottom to separate transport from track area
    // REMOVED: Causing gap/double-border with playlist view
    /*
    renderer.drawLine(
        NomadUI::NUIPoint(bounds.x, bounds.y + bounds.height - 1),
        NomadUI::NUIPoint(bounds.x + bounds.width, bounds.y + bounds.height - 1),
        1.0f,
        borderColor.withAlpha(0.8f)
    );
    */
    
    // Render children (buttons and labels)
    renderChildren(renderer);
    
    // Render custom icons on top of buttons
    renderButtonIcons(renderer);

    // Popups Last (Render Z-Top)

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
    // Explicitly forward to info container first (for BPM scroll, arrow clicks)
    if (m_infoContainer && m_infoContainer->getBounds().contains(event.position)) {
        if (m_infoContainer->onMouseEvent(event)) {
            return true;
        }
    }
    
    // Let other children handle mouse events
    return NomadUI::NUIComponent::onMouseEvent(event);
}

} // namespace Nomad
