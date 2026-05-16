// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file TransportBar.cpp
 * @brief Transport bar implementation
 */

#include "TransportBar.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraCore/include/AestraLog.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <chrono>

namespace Aestra {

namespace {

constexpr float TRANSPORT_BUTTON_SIZE = 32.0f;
constexpr float TRANSPORT_BUTTON_SPACING = 8.0f;
constexpr float TRANSPORT_GROUP_SPACING = 24.0f;
constexpr float TRANSPORT_ISLAND_PADDING = 12.0f;
constexpr float TRANSPORT_ISLAND_HEIGHT = 48.0f;

} // namespace

// =============================================================================
// SECTION: Construction & Setup
// =============================================================================

TransportBar::TransportBar()
    : AestraUI::NUIComponent()
    , m_state(TransportState::Stopped)
    , m_tempo(120.0f)
    , m_position(0.0)
{
    createIcons();
    createIcons();
    
    // Create modular info container FIRST so it's behind the buttons (Z-order)
    // This fixed the issue where InfoContainer blocked clicks to Transport buttons
    m_infoContainer = std::make_shared<TransportInfoContainer>();
    addChild(m_infoContainer);
    
    createButtons();

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
    m_playIcon = std::make_shared<AestraUI::NUIIcon>(playSvg);
    m_playIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_playIcon->setColorFromTheme("primary");  // Use primary theme color
    
    // Pause icon (Thicker Bars)
    const char* pauseSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M8 19c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2s-2 .9-2 2v10c0 1.1.9 2 2 2zm6-12v10c0 1.1.9 2 2 2s2-.9 2-2V7c0-1.1-.9-2-2-2s-2 .9-2 2z"/>
        </svg>
    )";
    m_pauseIcon = std::make_shared<AestraUI::NUIIcon>(pauseSvg);
    m_pauseIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_pauseIcon->setColorFromTheme("primary");
    
    // Stop icon (Rounded Square)
    const char* stopSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M8 6h8c1.1 0 2 .9 2 2v8c0 1.1-.9 2-2 2H8c-1.1 0-2-.9-2-2V8c0-1.1.9-2 2-2z"/>
        </svg>
    )";
    m_stopIcon = std::make_shared<AestraUI::NUIIcon>(stopSvg);
    m_stopIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_stopIcon->setColorFromTheme("primary");
    
    // Record icon (Solid Circle) - Vibrant Red
    const char* recordSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <circle cx="12" cy="12" r="9"/>
        </svg>
    )";
    m_recordIcon = std::make_shared<AestraUI::NUIIcon>(recordSvg);
    m_recordIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_recordIcon->setColorFromTheme("error");  // #ff4d4d - Clear red for recording

    // Mixer icon (Stylized Sliders)
    const char* mixerSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M5 15h2v4H5v-4zm0-10h2v8H5V5zm6 12h2v2h-2v-2zm0-12h2v10h-2V5zm6 8h2v6h-2v-6zm0-8h2v6h-2V5z"/>
        </svg>
    )";
    m_mixerIcon = std::make_shared<AestraUI::NUIIcon>(mixerSvg);
    m_mixerIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_mixerIcon->setColorFromTheme("textSecondary");

    // Sequencer icon (Grid)
    const char* sequencerSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M4 4h4v4H4V4zm6 0h4v4h-4V4zm6 0h4v4h-4V4zM4 10h4v4H4v-4zm6 0h4v4h-4v-4zm6 0h4v4h-4v-4zM4 16h4v4H4v-4zm6 0h4v4h-4v-4zm6 0h4v4h-4v-4z"/>
        </svg>
    )";
    m_sequencerIcon = std::make_shared<AestraUI::NUIIcon>(sequencerSvg);
    m_sequencerIcon->setIconSize(AestraUI::NUIIconSize::Medium);
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
    m_pianoRollIcon = std::make_shared<AestraUI::NUIIcon>(pianoRollSvg);
    m_pianoRollIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_pianoRollIcon->setColorFromTheme("textSecondary");

    // Playlist icon (tracks)
    const char* playlistSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M3 13h8v-2H3v2zm0 4h8v-2H3v2zm0-8h8V7H3v2zm10-6v18h8V3h-8zm6 16h-4V5h4v14z"/>
        </svg>
    )";
    m_playlistIcon = std::make_shared<AestraUI::NUIIcon>(playlistSvg);
    m_playlistIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_playlistIcon->setColorFromTheme("textSecondary");

    // Metronome icon (classic metronome shape)
    const char* metronomeSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M12 1.5L6 22h12L12 1.5zM11 8l1-3 1 3v6h-2V8z"/>
            <circle cx="12" cy="18" r="2"/>
        </svg>
    )";
    m_metronomeIcon = std::make_shared<AestraUI::NUIIcon>(metronomeSvg);
    m_metronomeIcon->setIconSize(AestraUI::NUIIconSize::Medium);
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
    m_countInIcon = std::make_shared<AestraUI::NUIIcon>(countInSvg);
    m_countInIcon->setIconSize(AestraUI::NUIIconSize::Medium);
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
    m_waitIcon = std::make_shared<AestraUI::NUIIcon>(waitRealSvg);
    m_waitIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_waitIcon->setColorFromTheme("textSecondary");

    // Loop Record icon (Ouroboros / Cycle arrow with Dot)
    const char* loopRecordSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
             <path d="M12 4V1L8 5l4 4V6c3.31 0 6 2.69 6 6 0 1.01-.25 1.97-.7 2.8l1.46 1.46C19.54 15.03 20 13.57 20 12c0-4.42-3.58-8-8-8zm0 14c-3.31 0-6-2.69-6-6 0-1.01.25-1.97.7-2.8L5.24 7.74C4.46 8.97 4 10.43 4 12c0 4.42 3.58 8 8 8v3l4-4-4-4v3z"/>
             <circle cx="12" cy="12" r="3"/>
        </svg>
    )";
    m_loopRecordIcon = std::make_shared<AestraUI::NUIIcon>(loopRecordSvg);
    m_loopRecordIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_loopRecordIcon->setColorFromTheme("textSecondary");

}

void TransportBar::createButtons() {
    // Play/Pause/Stop/Record...
    // Play/Pause/Stop/Record...
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    auto createBtn = [&](std::shared_ptr<AestraUI::NUIButton>& btn, std::function<void()> cb) {
        btn = std::make_shared<AestraUI::NUIButton>();
        btn->setText("");
        btn->setStyle(AestraUI::NUIButton::Style::Icon);
        btn->setSize(32, 32);
        
        btn->setBackgroundColor(AestraUI::NUIColor::transparent());
        btn->setHoverColor(theme.getColor("primary").withAlpha(0.06f));
        btn->setPressedColor(theme.getColor("primary").withAlpha(0.12f));
        btn->setBorderEnabled(false);
        btn->setCornerRadius(6.0f);
        btn->setGlowEnabled(false);
        
        btn->setOnClick(cb);
        addChild(btn);
    };

    createBtn(m_playButton, [this]() { togglePlayPause(); });
    m_playButton->setTooltip("Play/Pause");

    createBtn(m_stopButton, [this]() { stop(); });
    m_stopButton->setTooltip("Stop (Space)");

    createBtn(m_recordButton, [](){});
    m_recordButton->setTooltip("Record (R)");
    m_recordButton->setToggleable(true); // Enable toggle behavior so setOnToggle works
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
    auto createViewButton = [&](std::shared_ptr<AestraUI::NUIButton>& btn, std::function<void()> onClick) {
        createBtn(btn, onClick);
    };

    createViewButton(m_mixerButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::Mixer); });
    if(m_mixerButton) m_mixerButton->setTooltip("Mixer (F3)");

    createViewButton(m_sequencerButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::Sequencer); });
    if(m_sequencerButton) m_sequencerButton->setTooltip("Channel Rack (F6)");
    
    // Wire Record button
    if (m_recordButton) {
        m_recordButton->setOnToggle([this](bool armed) {
            Aestra::Log::info("Transport: Record Button Toggled: " + std::string(armed ? "ON" : "OFF"));
            if (m_onRecord) m_onRecord(armed);
        });
    }

    createViewButton(m_pianoRollButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::PianoRoll); });
    if(m_pianoRollButton) m_pianoRollButton->setTooltip("Piano Roll (F7)");

    createViewButton(m_playlistButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::Playlist); });
    if(m_playlistButton) m_playlistButton->setTooltip("Playlist (F5)");
    
    // Add Dropdowns LAST to ensure Z-ordering

}

// =============================================================================
// SECTION: Transport Controls
// =============================================================================

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
    // Always call the callback - even when already stopped
    // This enables "hard stop" (double-stop) to kill ring-outs
    bool wasAlreadyStopped = (m_state == TransportState::Stopped);
    
    if (!wasAlreadyStopped) {
        m_state = TransportState::Stopped;
        m_position = 0.0;
        updateButtonStates();
        
        if (m_infoContainer) {
            m_infoContainer->getTimerDisplay()->setTime(m_position);
            // Update timer to show stopped state (white color)
            m_infoContainer->getTimerDisplay()->setPlaying(false);
        }
        
        // Ensure Record button is untoggled visually when stopping
        if (m_recordButton) {
            m_recordButton->setToggled(false);
        }
    }
    
    // Always call callback (hard-stop when already stopped)
    if (m_onStop) {
        m_onStop(wasAlreadyStopped);
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

void TransportBar::syncTransportState(bool playing, bool paused, bool recordArmed) {
    TransportState newState = TransportState::Stopped;
    if (playing) {
        newState = TransportState::Playing;
    } else if (paused) {
        newState = TransportState::Paused;
    }

    if (m_state != newState) {
        m_state = newState;
        updateButtonStates();
    }

    if (m_infoContainer) {
        m_infoContainer->getTimerDisplay()->setPlaying(newState == TransportState::Playing);
    }

    if (m_recordButton && m_recordButton->isToggled() != recordArmed) {
        m_recordButton->setToggled(recordArmed);
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
        // [FIX] Always enabled - allows hard-stop (double-stop) to kill ring-outs
        m_stopButton->setEnabled(true);
    }

    if (m_recordButton) {
        m_recordButton->setText("");
        // Enable record button now that backend is implemented
        m_recordButton->setEnabled(true);
    }
}

// =============================================================================
// SECTION: Rendering
// =============================================================================

void TransportBar::renderButtonIcons(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();

    // Get layout dimensions from theme
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Colors
    AestraUI::NUIColor glassBg = AestraUI::NUIColor::transparent();
    AestraUI::NUIColor glassBorder = AestraUI::NUIColor::transparent();
    AestraUI::NUIColor glassHover = themeManager.getColor("surfaceRaised");
    AestraUI::NUIColor glassActive = themeManager.getColor("accentPrimary").withAlpha(0.15f);
    
    AestraUI::NUIColor iconGrey = themeManager.getColor("textSecondary");
    AestraUI::NUIColor iconPurple = themeManager.getColor("accentPrimary");
    AestraUI::NUIColor iconRed = themeManager.getColor("error");

    // Calculate button positions
    float padding = layout.panelMargin;
    float buttonSize = layout.transportButtonSize;
    float spacing = layout.transportButtonSpacing;
    float centerOffsetY = (bounds.height - buttonSize) / 2.0f;
    float x = padding;
    
    const float iconSize = 18.0f; // Reduced from 24 to 18 for better padding in 28px button

    // Helper to render universal Glass Box button
    auto renderGlassButton = [&](std::shared_ptr<AestraUI::NUIButton>& btn,
                                 std::shared_ptr<AestraUI::NUIIcon>& icon,
                                 bool isActive,
                                 bool isRecording = false,
                                 bool isPrimaryTransport = false) {
        if (!btn || !icon) return;

        AestraUI::NUIRect buttonRect = btn->getBounds(); // Use bounds set in layoutComponents
        bool isHovered = btn->isHovered() && btn->isEnabled();
        
        // Setup Colors
        AestraUI::NUIColor currentBg = glassBg;
        AestraUI::NUIColor currentBorder = glassBorder;
        AestraUI::NUIColor iconColor = iconGrey.withAlpha(0.35f);
        
        // LOGIC: Glassy Look (Reverted per user request)
        // Active = Purple Tint Glass + Purple Icon
        // Inactive = Grey Tint Glass + Grey Icon
        
        if (isRecording) {
             currentBg = iconRed.withAlpha(0.16f);
             currentBorder = iconRed.withAlpha(0.24f);
             iconColor = iconRed;
             if (isHovered) currentBg = iconRed.withAlpha(0.22f);
        } else if (isActive) {
             currentBg = themeManager.getColor("accentPrimary").withAlpha(0.18f);
             currentBorder = themeManager.getColor("accentPrimary").withAlpha(0.34f);
             iconColor = themeManager.getColor("textPrimary");
         } else if (isHovered) {
             currentBg = glassHover.withAlpha(0.82f);
             currentBorder = themeManager.getColor("border").withAlpha(0.30f);
             iconColor = themeManager.getColor("textPrimary").withAlpha(0.76f);
         }

        if (isPrimaryTransport) {
            if (!isRecording && !isActive && !isHovered) {
                currentBg = themeManager.getColor("buttonBgHover").withAlpha(0.64f);
                currentBorder = themeManager.getColor("border").withAlpha(0.38f);
            } else if (isActive && !isRecording) {
                currentBg = themeManager.getColor("accentPrimary").withAlpha(0.22f);
                currentBorder = themeManager.getColor("accentPrimary").withAlpha(0.46f);
            }
            if (!isRecording) {
                iconColor = themeManager.getColor("textPrimary").withAlpha(isActive || isHovered ? 1.0f : 0.94f);
            }
        }
        
        // Draw Button Background
        if (isHovered || isActive || isRecording || isPrimaryTransport) {
            renderer.fillRoundedRect(buttonRect, 6.0f, currentBg);
            if (currentBorder.a > 0.0f) {
                renderer.strokeRoundedRect(buttonRect, 6.0f, 1.0f, currentBorder);
            }
        }
        
        if (!btn->isEnabled()) {
            iconColor = iconColor.withAlpha(0.3f);
        }

        // Render Icon
        const float localIconSize = isPrimaryTransport ? 18.0f : 16.0f;
        float localPadding = (buttonRect.width - localIconSize) * 0.5f;
        if (localPadding < 2.0f) localPadding = 2.0f;
        AestraUI::NUIRect iconRect = NUIAbsolute(buttonRect, localPadding, localPadding, localIconSize, localIconSize);
        icon->setBounds(iconRect);
        icon->setColor(iconColor);
        icon->onRender(renderer);
    };

    // --- Transport Controls (Left) ---

    // Play/Pause
    if (m_playButton) {
        bool isPlaying = (m_state == TransportState::Playing);
        auto currentIcon = isPlaying ? m_pauseIcon : m_playIcon;
        renderGlassButton(m_playButton, currentIcon, isPlaying, false, true);

        // Breathing glow when playing — subtle pulse so user can see transport is active
        if (isPlaying) {
            auto now = std::chrono::steady_clock::now();
            float timeSec = std::chrono::duration<float>(now.time_since_epoch()).count();
            float pulse = (std::sin(timeSec * 3.0f) * 0.5f + 0.5f); // 0..1 oscillation ~0.5Hz
            AestraUI::NUIRect playRect = m_playButton->getBounds();
            renderer.drawShadow(
                {playRect.x, playRect.y, playRect.width, playRect.height},
                4.0f, 1.0f, 6.0f,
                themeManager.getColor("accentPrimary").withAlpha(0.08f + pulse * 0.06f)
            );
        }
    }

    // Stop
    renderGlassButton(m_stopButton, m_stopIcon, false, false, true);

    // Use isToggled() for immediate visual feedback. 
    // 3rd arg (isActive): Controls pressed look. 4th arg (isRecording): Controls RED color.
    // We want RED only when toggled.
    renderGlassButton(m_recordButton, m_recordIcon, m_recordButton->isToggled(), m_recordButton->isToggled(), true);

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

// =============================================================================
// SECTION: Layout
// =============================================================================

// ... (Previous code)

void TransportBar::layoutComponents() {
    AestraUI::NUIRect bounds = getBounds();

    // Get layout dimensions from theme
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    // Use configurable dimensions - OVERRIDE for Compact Mode
    float buttonSize = TRANSPORT_BUTTON_SIZE;
    const float primaryButtonScale = 1.0f;
    const float primaryButtonSize = buttonSize * primaryButtonScale;
    float spacing = TRANSPORT_BUTTON_SPACING;
    float groupSpacing = TRANSPORT_GROUP_SPACING;

    // --- Layout Logic: Center-Out Calculation ---
    // We calculate the required width first to center the island perfectly
    
    // Group 1: Transport (Play, Stop, Rec)
    float group1Width = (buttonSize * 3) + (spacing * 2);
    
    // Group 2: Extras (Count, Wait, Loop, Metronome)
    float group2Width = (buttonSize * 4) + (spacing * 3);
    
    // Group 3: Info Display (Center)
    // Compact Info: 180px instead of 220px -> reduce to 160px for tighter packing?
    // Let's check TransportInfoContainer first, but for now allow 170.
    // Group 3: Info Display (Center)
    // Expanded Info: 220px to accommodate children
    float infoWidth = 260.0f;
    
    // Group 4: Views (Mixer, Seq, Piano, Playlist) - 4 buttons
    float group4Width = (buttonSize * 4) + (spacing * 3);

    // Total Content Width
    float totalContentWidth = group1Width + groupSpacing + group2Width + groupSpacing + infoWidth + groupSpacing + group4Width;
    float islandPadding = TRANSPORT_ISLAND_PADDING;
    float islandWidth = totalContentWidth + (islandPadding * 2.0f);
    
    // Clamp to window width
    if (islandWidth > bounds.width - 20.0f) {
        islandWidth = bounds.width - 20.0f;
    }

    const float islandHeight = std::min(TRANSPORT_ISLAND_HEIGHT, bounds.height);
    const float visualCenterBiasY = 0.0f;
    float islandX = std::round((bounds.width - islandWidth) * 0.5f);
    float islandY = std::round((bounds.height - islandHeight) * 0.5f + visualCenterBiasY);
    
    // Check min width/fallback
    if (bounds.width < islandWidth) {
        islandX = 0;
        islandWidth = bounds.width;
    }

    float centerOffsetY = std::round(islandY + (islandHeight - buttonSize) * 0.5f);

    // --- Placement ---
    float xCursor = islandX + islandPadding;

    // Group 1: Transport
    const auto placePrimaryButton = [&](const std::shared_ptr<AestraUI::NUIButton>& button, float x) {
        if (!button) return;
        const float grow = 0.5f * (primaryButtonSize - buttonSize);
        button->setBounds(NUIAbsolute(bounds, x - grow, centerOffsetY - grow, primaryButtonSize, primaryButtonSize));
    };

    placePrimaryButton(m_playButton, xCursor);
    xCursor += buttonSize + spacing;
    
    placePrimaryButton(m_stopButton, xCursor);
    xCursor += buttonSize + spacing;
    
    placePrimaryButton(m_recordButton, xCursor);
    xCursor += buttonSize + groupSpacing; // GAP

    // Group 2: Extras
    m_countInButton->setBounds(NUIAbsolute(bounds, xCursor, centerOffsetY, buttonSize, buttonSize));
    xCursor += buttonSize + spacing;
    
    m_waitButton->setBounds(NUIAbsolute(bounds, xCursor, centerOffsetY, buttonSize, buttonSize));
    xCursor += buttonSize + spacing;
    
    m_loopRecordButton->setBounds(NUIAbsolute(bounds, xCursor, centerOffsetY, buttonSize, buttonSize));
    xCursor += buttonSize + spacing;
    
    m_metronomeButton->setBounds(NUIAbsolute(bounds, xCursor, centerOffsetY, buttonSize, buttonSize));
    xCursor += buttonSize + groupSpacing; // GAP to Info

    // Group 3: Info Container
    if (m_infoContainer) {
        m_infoContainer->setBounds(NUIAbsolute(bounds, xCursor, islandY, infoWidth, islandHeight));
    }
    xCursor += infoWidth + groupSpacing; // GAP

    // Group 4: Views
    if (m_mixerButton) {
        m_mixerButton->setBounds(NUIAbsolute(bounds, xCursor, centerOffsetY, buttonSize, buttonSize));
        xCursor += buttonSize + spacing;
    }
    if (m_sequencerButton) {
        m_sequencerButton->setBounds(NUIAbsolute(bounds, xCursor, centerOffsetY, buttonSize, buttonSize));
        xCursor += buttonSize + spacing;
    }
    if (m_pianoRollButton) {
        m_pianoRollButton->setBounds(NUIAbsolute(bounds, xCursor, centerOffsetY, buttonSize, buttonSize));
        xCursor += buttonSize + spacing;
    }
    if (m_playlistButton) {
        m_playlistButton->setBounds(NUIAbsolute(bounds, xCursor, centerOffsetY, buttonSize, buttonSize));
        // xCursor += buttonSize + spacing;
    }

    // Pass dimensions to Render via Theme or member not possible easily here without state.
    // We relying on onRender duplicating the math or us storing it?
    // Let's update onRender to match these hardcoded compaction values.
}

void TransportBar::onRender(AestraUI::NUIRenderer& renderer) {
    AESTRA_ZONE("Transport_Render");
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();

    // 1. Clear background (Void/Transparent)
    // renderer.fillRect(bounds, themeManager.getColor("backgroundPrimary")); // REMOVE: Occludes FileBrowser if Z-Ordered on top

    // 2. Re-Calculate Island Geometry (Match layoutComponents logic)
    // 2. Re-Calculate Island Geometry (Match layoutComponents logic)
    // Compact Values (Relaxed per user request: "space would have done the trick")
    float buttonSize = TRANSPORT_BUTTON_SIZE;
    float spacing = TRANSPORT_BUTTON_SPACING;
    float groupSpacing = TRANSPORT_GROUP_SPACING;
    float group1Width = (buttonSize * 3) + (spacing * 2);
    float group2Width = (buttonSize * 4) + (spacing * 3);
    float infoWidth = 260.0f;
    float group4Width = (buttonSize * 4) + (spacing * 3);

    float totalContentWidth = group1Width + groupSpacing + group2Width + groupSpacing + infoWidth + groupSpacing + group4Width;
    float islandPadding = TRANSPORT_ISLAND_PADDING;
    float islandWidth = totalContentWidth + (islandPadding * 2.0f);
    
    if (islandWidth > bounds.width - 20.0f) islandWidth = bounds.width - 20.0f;
    
    const float islandHeight = std::min(TRANSPORT_ISLAND_HEIGHT, bounds.height);
    const float visualCenterBiasY = -1.0f;
    float islandX = std::round((bounds.width - islandWidth) * 0.5f);
    float islandY = std::round((bounds.height - islandHeight) * 0.5f + visualCenterBiasY);
    
    if (bounds.width < islandWidth) {
        islandX = 0;
        islandWidth = bounds.width;
    }

    AestraUI::NUIRect islandRect(bounds.x + islandX, bounds.y + islandY, islandWidth, islandHeight);

    renderer.fillRect(bounds, themeManager.getColor("backgroundSecondary"));
    renderer.drawLine({bounds.x, bounds.bottom() - 1.0f},
                      {bounds.right(), bounds.bottom() - 1.0f},
                      1.0f,
                      themeManager.getColor("border").withAlpha(0.52f));
    
    const float leftEdge = islandRect.x + islandPadding;
    const float topInset = 6.0f;
    const float bottomInset = 6.0f;
    const float available = islandRect.height - topInset - bottomInset;
    const float groupH = std::max(0.0f, std::min(36.0f, available));
    const float groupY = islandRect.y + topInset;
    const auto groupBg = themeManager.getColor("surfaceTertiary").withAlpha(0.56f);
    const auto groupBorder = themeManager.getColor("border").withAlpha(0.52f);
    const auto drawGroup = [&](float x, float w) {
        if (w <= 0.0f) {
            return;
        }
        AestraUI::NUIRect groupRect(std::round(x), std::round(groupY), std::round(w), groupH);
        renderer.fillRoundedRect(groupRect, 6.0f, groupBg);
        renderer.strokeRoundedRect(groupRect, 6.0f, 1.0f, groupBorder);
    };

    const float g1X = leftEdge - 6.0f;
    const float g2X = leftEdge + group1Width + groupSpacing - 6.0f;
    const float infoX = leftEdge + group1Width + groupSpacing + group2Width + groupSpacing - 6.0f;
    const float g4X = leftEdge + group1Width + groupSpacing + group2Width + groupSpacing + infoWidth + groupSpacing - 6.0f;
    drawGroup(g1X, group1Width + 12.0f);
    drawGroup(g2X, group2Width + 12.0f);
    drawGroup(infoX, infoWidth + 12.0f);
    drawGroup(g4X, group4Width + 12.0f);

    const float sep1X = leftEdge + group1Width + (groupSpacing * 0.5f);
    const float sep2X = sep1X + groupSpacing * 0.5f + group2Width + (groupSpacing * 0.5f);
    const float sep3X = sep2X + groupSpacing * 0.5f + infoWidth + (groupSpacing * 0.5f);
    const float sepTop = islandRect.y + 8.0f;
    const float sepBottom = islandRect.bottom() - 8.0f;
    const auto sepColor = themeManager.getColor("borderSubtle").withAlpha(0.58f);
    renderer.drawLine({sep1X, sepTop}, {sep1X, sepBottom}, 1.0f, sepColor);
    renderer.drawLine({sep2X, sepTop}, {sep2X, sepBottom}, 1.0f, sepColor);
    renderer.drawLine({sep3X, sepTop}, {sep3X, sepBottom}, 1.0f, sepColor);
    
    renderChildren(renderer);
    renderButtonIcons(renderer);
}

void TransportBar::onResize(int width, int height) {
    // Don't reset bounds here - parent has already set the correct position
    // Just update the size while preserving x,y position
    AestraUI::NUIRect currentBounds = getBounds();
    setBounds(AestraUI::NUIRect(currentBounds.x, currentBounds.y, width, height));
    layoutComponents();
    AestraUI::NUIComponent::onResize(width, height);
}

bool TransportBar::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    // Standard event dispatch to children (respects Z-order: Buttons are on Top)
    const bool handled = AestraUI::NUIComponent::onMouseEvent(event);
    if (!getBounds().contains(event.position)) {
        return handled;
    }

    const auto updateTooltipForButton = [&](const std::shared_ptr<AestraUI::NUIButton>& button,
                                            const char* text) -> bool {
        return button && button->isVisible() && button->getBounds().contains(event.position)
            && button->isEnabled() && text && text[0] != '\0';
    };

    const std::pair<std::shared_ptr<AestraUI::NUIButton>, const char*> tooltipButtons[] = {
        {m_playButton, "Play / Pause"},
        {m_stopButton, "Stop"},
        {m_recordButton, "Record"},
        {m_metronomeButton, "Metronome"},
        {m_countInButton, "Count-In"},
        {m_waitButton, "Wait for Input"},
        {m_loopRecordButton, "Loop Record"},
        {m_mixerButton, "Mixer"},
        {m_sequencerButton, "Arsenal"},
        {m_pianoRollButton, "Piano Roll"},
        {m_playlistButton, "Timeline"}
    };

    for (const auto& [button, text] : tooltipButtons) {
        if (updateTooltipForButton(button, text)) {
            AestraUI::NUIComponent::showRemoteTooltip(text, event.position, this);
            return handled;
        }
    }

    AestraUI::NUIComponent::hideRemoteTooltip(this);
    return handled;
}

} // namespace Aestra
