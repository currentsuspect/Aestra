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

    // Count-In icon (3-2-1 dots style)
    const char* countInSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <text x="12" y="17" font-family="Arial" font-size="14" font-weight="900" text-anchor="middle">3</text>
            <circle cx="12" cy="5" r="1.5"/>
            <circle cx="7" cy="5" r="1.5"/>
            <circle cx="17" cy="5" r="1.5"/>
        </svg>
    )";
    m_countInIcon = std::make_shared<NomadUI::NUIIcon>(countInSvg);
    m_countInIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_countInIcon->setColorFromTheme("textSecondary");

    // Wait for Input icon (Pause bars + Play Triangle combo or Hourglass)
    // Let's use a "Signal" style (Keyboard key + Wave) or just a simple "Wait" hand?
    // User requested "Wait". Let's use a nice Clock/Hourglass or Key input style.
    // Going with "Keyboard Key with Input Arrow" style for interaction wait.
    const char* waitSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
             <path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-6h2v6zm0-8h-2V7h2v2z"/>
        </svg>
    )";
    // Use an actual Hourglass/Clock might be better for "Wait".
    const char* waitRealSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
             <path d="M6 2v6h.01L6 8.01 10 12l-4 4 .01.01H6V22h12v-5.99h-.01L18 16l-4-4 4-3.99-.01-.01H18V2H6zm10 14.5V20H8v-3.5l4-4 4 4z"/>
        </svg>
    )";
    m_waitIcon = std::make_shared<NomadUI::NUIIcon>(waitRealSvg);
    m_waitIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_waitIcon->setColorFromTheme("textSecondary");

    // Loop Record icon (Ouroboros / Cycle arrow with Dot)
    const char* loopRecordSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
             <path d="M12 4V1L8 5l4 4V6c3.31 0 6 2.69 6 6 0 1.01-.25 1.97-.7 2.8l1.46 1.46C19.54 15.03 20 13.57 20 12c0-4.42-3.58-8-8-8zm0 14c-3.31 0-6-2.69-6-6 0-1.01.25-1.97.7-2.8L5.24 7.74C4.46 8.97 4 10.43 4 12c0 4.42 3.58 8 8 8v3l4-4-4-4v3z"/>
             <circle cx="12" cy="12" r="3"/>
        </svg>
    )";
    m_loopRecordIcon = std::make_shared<NomadUI::NUIIcon>(loopRecordSvg);
    m_loopRecordIcon->setIconSize(NomadUI::NUIIconSize::Medium);
    m_loopRecordIcon->setColorFromTheme("textSecondary");

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
    m_playButton->setTooltip("Play/Pause (Space)");

    createBtn(m_stopButton, [this]() { stop(); });
    m_stopButton->setTooltip("Stop (Space)");

    createBtn(m_recordButton, [](){});
    m_recordButton->setTooltip("Record (R)");
    m_recordButton->setEnabled(false);
    
    // Metronome toggle button
    createBtn(m_metronomeButton, [this]() {
        m_metronomeActive = !m_metronomeActive;
        if (m_onMetronomeToggle) {
            m_onMetronomeToggle(m_metronomeActive);
        }
        setDirty(true);
    });
    m_metronomeButton->setTooltip("Metronome");

    // Transport Extras
    createBtn(m_countInButton, [this]() {
        m_countInActive = !m_countInActive;
        if (m_onCountInToggle) m_onCountInToggle(m_countInActive);
        setDirty(true);
    });
    m_countInButton->setTooltip("Count-In");
    
    createBtn(m_waitButton, [this]() {
        m_waitActive = !m_waitActive;
        if (m_onWaitToggle) m_onWaitToggle(m_waitActive);
        setDirty(true);
    });
    m_waitButton->setTooltip("Wait for Input");
    
    createBtn(m_loopRecordButton, [this]() {
        m_loopRecordActive = !m_loopRecordActive;
        if (m_onLoopRecordToggle) m_onLoopRecordToggle(m_loopRecordActive);
        setDirty(true);
    });
    m_loopRecordButton->setTooltip("Loop Record");

    // View Toggles
    auto createViewButton = [&](std::shared_ptr<NomadUI::NUIButton>& btn, std::function<void()> onClick) {
        createBtn(btn, onClick);
    };

    createViewButton(m_mixerButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::Mixer); });
    if(m_mixerButton) m_mixerButton->setTooltip("Mixer (F3)");

    createViewButton(m_sequencerButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::Sequencer); });
    if(m_sequencerButton) m_sequencerButton->setTooltip("Channel Rack (F6)");

    createViewButton(m_pianoRollButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::PianoRoll); });
    if(m_pianoRollButton) m_pianoRollButton->setTooltip("Piano Roll (F7)");

    createViewButton(m_playlistButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::Playlist); });
    if(m_playlistButton) m_playlistButton->setTooltip("Playlist (F5)");
    
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
    
    // Colors
    // "Frosted Glass" - Grey tint to distinguish from dark displays
    NomadUI::NUIColor glassBg = themeManager.getColor("textSecondary").withAlpha(0.15f); 
    NomadUI::NUIColor glassBorder = themeManager.getColor("glassBorder");
    NomadUI::NUIColor glassHover = themeManager.getColor("textSecondary").withAlpha(0.25f); // Brighter grey on hover
    NomadUI::NUIColor glassActive = themeManager.getColor("glassActive"); // Purple tint
    
    NomadUI::NUIColor iconGrey = themeManager.getColor("textSecondary");
    NomadUI::NUIColor iconPurple = themeManager.getColor("accentPrimary");
    NomadUI::NUIColor iconRed = themeManager.getColor("error");

    // Calculate button positions
    float padding = layout.panelMargin;
    float buttonSize = layout.transportButtonSize;
    float spacing = layout.transportButtonSpacing;
    float centerOffsetY = (bounds.height - buttonSize) / 2.0f;
    float x = padding;
    
    const float iconSize = 24.0f;
    float iconPadding = (buttonSize - iconSize) * 0.5f;
    if (iconPadding < 0.0f) iconPadding = 0.0f;

    // Helper to render universal Glass Box button
    auto renderGlassButton = [&](std::shared_ptr<NomadUI::NUIButton>& btn, std::shared_ptr<NomadUI::NUIIcon>& icon, bool isActive, bool isRecording = false) {
        if (!btn || !icon) return;

        NomadUI::NUIRect buttonRect = btn->getBounds(); // Use bounds set in layoutComponents
        bool isHovered = btn->isHovered() && btn->isEnabled();
        
        // Setup Colors
        NomadUI::NUIColor currentBg = glassBg;
        NomadUI::NUIColor currentBorder = glassBorder;
        NomadUI::NUIColor iconColor = iconGrey;
        
        // LOGIC: Glassy Look (Reverted per user request)
        // Active = Purple Tint Glass + Purple Icon
        // Inactive = Grey Tint Glass + Grey Icon
        
        if (isRecording) {
             // Recording Active: Red Tint Glass + Red Icon
             currentBg = iconRed.withAlpha(0.15f); // Red Glass
             currentBorder = iconRed.withAlpha(0.5f);
             iconColor = iconRed;
             if (isHovered) currentBg = iconRed.withAlpha(0.25f);
        } else if (isActive) {
             // Normal Active: Purple Haze
             currentBg = glassActive; 
             currentBorder = iconPurple.withAlpha(0.5f);
             iconColor = iconPurple;
        } else if (isHovered) {
             // Hover (Inactive): Brighter Grey Glass + Purple Icon
             currentBg = glassHover;
             currentBorder = iconPurple.withAlpha(0.3f);
             iconColor = iconPurple;
        }
        
        // Draw Button Background
        renderer.fillRoundedRect(buttonRect, 4.0f, currentBg);
        renderer.strokeRoundedRect(buttonRect, 4.0f, 1.0f, currentBorder);
        
        if (!btn->isEnabled()) {
            iconColor = iconColor.withAlpha(0.3f);
        }

        // Render Icon
        NomadUI::NUIRect iconRect = NUIAbsolute(buttonRect, iconPadding, iconPadding, iconSize, iconSize);
        icon->setBounds(iconRect);
        icon->setColor(iconColor);
        icon->onRender(renderer);
    };

    // --- Transport Controls (Left) ---

    // Play/Pause
    if (m_playButton) {
        bool isPlaying = (m_state == TransportState::Playing);
        auto currentIcon = isPlaying ? m_pauseIcon : m_playIcon;
        renderGlassButton(m_playButton, currentIcon, isPlaying);
    }

    // Stop
    renderGlassButton(m_stopButton, m_stopIcon, false);

    // Record (Special Red handling inside helper)
    renderGlassButton(m_recordButton, m_recordIcon, m_state == TransportState::Recording, true);

    // --- Transport Extras (Left of Metronome) ---
    renderGlassButton(m_countInButton, m_countInIcon, m_countInActive);
    renderGlassButton(m_waitButton, m_waitIcon, m_waitActive);
    renderGlassButton(m_loopRecordButton, m_loopRecordIcon, m_loopRecordActive);
    
    // --- Metronome (Left of Center) ---
    renderGlassButton(m_metronomeButton, m_metronomeIcon, m_metronomeActive);

    // --- View Toggles (Right) ---
    renderGlassButton(m_mixerButton, m_mixerIcon, m_mixerActive);
    renderGlassButton(m_sequencerButton, m_sequencerIcon, m_sequencerActive);
    renderGlassButton(m_pianoRollButton, m_pianoRollIcon, m_pianoRollActive);
    renderGlassButton(m_playlistButton, m_playlistIcon, m_playlistActive);
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
    
    // Metronome button: Positioned to the LEFT of BPM display
    // New Balance: [Count] [Wait] [Loop] [Metronome] ---> [BPM]
    
    // Start metronome at old position (Center - 180 - buttonSize)
    // Then stack others to the left of it.
    
    float metronomeRightGap = 180.0f; // Gap from center to Right edge of Metronome
    float metronomeX = centerX - metronomeRightGap - buttonSize;
    m_metronomeButton->setBounds(NUIAbsolute(bounds, metronomeX, centerOffsetY, buttonSize, buttonSize));
    
    // Stack Extras to the left of Metronome
    float currentX = metronomeX;
    
    // Loop Record
    currentX -= (buttonSize + spacing);
    m_loopRecordButton->setBounds(NUIAbsolute(bounds, currentX, centerOffsetY, buttonSize, buttonSize));
    
    // Wait
    currentX -= (buttonSize + spacing);
    m_waitButton->setBounds(NUIAbsolute(bounds, currentX, centerOffsetY, buttonSize, buttonSize));
    
    // Count In
    currentX -= (buttonSize + spacing);
    m_countInButton->setBounds(NUIAbsolute(bounds, currentX, centerOffsetY, buttonSize, buttonSize));

    
    // Calculate position for view toggles (Right side)
    float rightEdge = bounds.width;
    // View toggle buttons
    // Position to the right of center (BPM display) - centerX already declared above
    if (x > currentX - 20) { 
        // If main transport buttons encroach on extras, we might need adjustments.
        // But main transport is far left, so unlikely overlap.
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
