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
#include <vector>
#include <utility>

namespace Aestra {

namespace {

constexpr float TRANSPORT_BUTTON_SIZE = 32.0f;
constexpr float TRANSPORT_BUTTON_SPACING = 8.0f;
constexpr float TRANSPORT_ISLAND_PADDING = 12.0f;
constexpr float TRANSPORT_ISLAND_HEIGHT = 48.0f;
// With the group shells gone, spacing carries the grouping: a tight gap holds a
// cluster together (transport buttons ↔ record-aid extras), a generous gap sets
// the transport / musical-state / view clusters apart as distinct surfaces.
constexpr float TRANSPORT_INTRA_GAP = 14.0f;
constexpr float TRANSPORT_SURFACE_GAP = 46.0f;
constexpr float TRANSPORT_INFO_WIDTH = 260.0f;

// Semantic labels. Universal transport actions (play/stop/record), the
// metronome and the piano roll read as icons on their own; every DAW-specific
// concept carries a word so nobody has to memorise a glyph. The vocabulary is
// deliberately terse. "LOOP REC" is never shortened to "LOOP" — that would
// collide with playback looping.
constexpr const char* TRANSPORT_LABEL_COUNT_IN = "COUNT";
constexpr const char* TRANSPORT_LABEL_WAIT = "WAIT";
constexpr const char* TRANSPORT_LABEL_LOOP_REC = "LOOP REC";
// The view switches carry no labels. Faders and a rack of channel strips are
// vocabulary any producer already owns, so the glyphs stand on their own and
// the workspace cluster stays visually light next to the labelled record aids.
// Their tooltips name them ("Mixer (F3)", "Arsenal (F6)", "Piano Roll (F7)");
// the view is called Arsenal everywhere else in the product, so nothing here
// invents a second name for it.

// 11px matches the renderer's small-text floor, so the size layout assumes is
// the size that actually renders.
constexpr float TRANSPORT_LABEL_FONT_SIZE = 11.0f;
constexpr float TRANSPORT_LABEL_ICON_SIZE = 14.0f;
constexpr float TRANSPORT_LABEL_ICON_GAP = 6.0f;
constexpr float TRANSPORT_LABEL_PAD_X = 9.0f;

// layoutComponents runs without a renderer, so label widths are estimated
// rather than measured. Estimate high: the label is left-aligned after the
// icon, so overshooting only adds trailing padding, while undershooting would
// clip the word — and a clipped label defeats the whole point.
inline float transportLabelTextWidth(const char* text) {
    float w = 0.0f;
    for (const char* p = text; *p != '\0'; ++p) {
        w += (*p == ' ') ? TRANSPORT_LABEL_FONT_SIZE * 0.34f : TRANSPORT_LABEL_FONT_SIZE * 0.66f;
    }
    return w;
}

// An icon+label control is as wide as its word needs; icon-only controls stay
// square. This is why the toolbar can no longer assume a uniform button width.
inline float transportLabeledWidth(const char* text) {
    return TRANSPORT_LABEL_PAD_X * 2.0f + TRANSPORT_LABEL_ICON_SIZE + TRANSPORT_LABEL_ICON_GAP
           + transportLabelTextWidth(text);
}

// Record-aid cluster: COUNT · WAIT · LOOP REC · metronome (icon only).
inline float transportExtrasWidth() {
    return transportLabeledWidth(TRANSPORT_LABEL_COUNT_IN) + TRANSPORT_BUTTON_SPACING
         + transportLabeledWidth(TRANSPORT_LABEL_WAIT) + TRANSPORT_BUTTON_SPACING
         + transportLabeledWidth(TRANSPORT_LABEL_LOOP_REC) + TRANSPORT_BUTTON_SPACING
         + TRANSPORT_BUTTON_SIZE;
}

// View cluster: mixer · arsenal · piano roll, all icon-only. No Timeline button
// — the title-bar Timeline tab already owns that workspace, and duplicate
// navigation is worse than an ambiguous icon.
inline float transportViewsWidth() {
    return TRANSPORT_BUTTON_SIZE * 3.0f + TRANSPORT_BUTTON_SPACING * 2.0f;
}

// Progressive collapse for narrow windows. The transport island packs four fixed
// groups (transport / extras / info / views). When the window can't hold them all
// it used to place them at full width anyway and clip the rightmost group off the
// edge. Instead, hide the secondary groups — the record-aid extras first
// (count-in / wait / loop-record / metronome), then the view switches — keeping
// the transport buttons and tempo/time readout always visible. The transport
// buttons and info readout never hide.
// Collapse must preserve MEANING, not merely save width: a control either
// appears with its label intact or it does not appear at all. Degrading
// "LOOP REC" to a bare glyph to squeeze it in would undo the whole point of
// labelling it.
struct TransportLayoutTier {
    bool showExtras = true;   // count-in / wait / loop-record / metronome
    bool showViews = true;    // mixer / sequencer / piano-roll
};

inline TransportLayoutTier transportTierFor(float availWidth) {
    const float bs = TRANSPORT_BUTTON_SIZE;
    const float sp = TRANSPORT_BUTTON_SPACING;
    const float intra = TRANSPORT_INTRA_GAP;
    const float surf = TRANSPORT_SURFACE_GAP;
    const float pad = TRANSPORT_ISLAND_PADDING;
    const float g1 = bs * 3.0f + sp * 2.0f;         // transport (3 icon-only buttons)
    const float extras = transportExtrasWidth();
    const float views = transportViewsWidth();
    const float info = TRANSPORT_INFO_WIDTH;
    const float margin = 20.0f;                     // mirrors the island width clamp
    // Mirrors layoutComponents' gap rhythm: extras tuck tight to transport (intra),
    // clusters set apart by the surface gap.
    const float full     = (g1 + intra + extras + surf + info + surf + views) + pad * 2.0f;
    const float noExtras = (g1 + surf + info + surf + views) + pad * 2.0f;
    if (availWidth >= full + margin)     return {true, true};
    if (availWidth >= noExtras + margin) return {false, true};
    return {false, false};
}

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

    // Create modular info container FIRST so it's behind the buttons (Z-order)
    // This fixed the issue where InfoContainer blocked clicks to Transport buttons
    m_infoContainer = std::make_shared<TransportInfoContainer>();
    addChild(m_infoContainer);
    
    createButtons();

    m_musicalTypingLabel = std::make_shared<AestraUI::NUILabel>("KEYS C3");
    m_musicalTypingLabel->setFontSize(11.0f);
    m_musicalTypingLabel->setAlignment(AestraUI::NUILabel::Alignment::Center);
    m_musicalTypingLabel->setTextColor(AestraUI::NUIThemeManager::getInstance().getColor("accentPrimary"));
    m_musicalTypingLabel->setBackgroundVisible(true);
    m_musicalTypingLabel->setBackgroundColor(
        AestraUI::NUIThemeManager::getInstance().getColor("surfaceTertiary").withAlpha(0.72f));
    m_musicalTypingLabel->setBorderVisible(true);
    m_musicalTypingLabel->setBorderWidth(1.0f);
    m_musicalTypingLabel->setBorderColor(
        AestraUI::NUIThemeManager::getInstance().getColor("border").withAlpha(0.52f));
    m_musicalTypingLabel->setTooltip("Computer keys: Caps Lock toggles, Up/Down shifts octave");
    addChild(m_musicalTypingLabel);

    // Wire up BPM change callback from arrows
    if (m_infoContainer && m_infoContainer->getBPMDisplay()) {
        m_infoContainer->getBPMDisplay()->setOnBPMChange([this](float newBPM) {
            m_tempo = newBPM;
            if (m_onTempoChange) {
                m_onTempoChange(m_tempo);
            }
        });
    }

    // Forward time-signature clicks to the app (this link was missing: the
    // display cycled 2/4..7/8 but nothing outside the transport ever heard).
    if (m_infoContainer && m_infoContainer->getTimeSignatureDisplay()) {
        m_infoContainer->getTimeSignatureDisplay()->setOnTimeSignatureChange([this](int beatsPerBar) {
            if (m_onTimeSignatureChange) {
                m_onTimeSignatureChange(beatsPerBar);
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
    // Arsenal — three channel strips, each a pad plus its lane. The old glyph
    // was a plain 3x3 grid, which is the universal "apps" icon and said nothing
    // about channels. Horizontal lanes also read differently from the mixer's
    // vertical faders, so the two view buttons don't blur together.
    const char* sequencerSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <rect x="3" y="5.2" width="4" height="3.8" rx="1.1"/>
            <rect x="8.8" y="5.2" width="12.2" height="3.8" rx="1.1"/>
            <rect x="3" y="10.6" width="4" height="3.8" rx="1.1"/>
            <rect x="8.8" y="10.6" width="9" height="3.8" rx="1.1"/>
            <rect x="3" y="16" width="4" height="3.8" rx="1.1"/>
            <rect x="8.8" y="16" width="10.8" height="3.8" rx="1.1"/>
        </svg>
    )";
    m_sequencerIcon = std::make_shared<AestraUI::NUIIcon>(sequencerSvg);
    m_sequencerIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_sequencerIcon->setColorFromTheme("textSecondary");

    // Piano Roll icon (MIDI Grid + Vertical Keys)
    // A literal piano keyboard. This control carries no label, so the glyph has
    // to be self-evident — the old version was an abstract pane with note
    // blocks, which at 16px read as a generic panel. Kept to three white keys
    // and two chunky black keys: a full octave turns into a barcode at this
    // size, and the black keys need to out-weigh the key dividers to register
    // as black keys rather than more lines.
    const char* pianoRollSvg = R"(
        <svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <path fill="currentColor" fill-rule="evenodd" d="M3 6 H21 V18 H3 Z M7.7 6 H10.3 V13 H7.7 Z M13.7 6 H16.3 V13 H13.7 Z M8.55 13 H9.45 V18 H8.55 Z M14.55 13 H15.45 V18 H14.55 Z"/>
        </svg>
    )";
    m_pianoRollIcon = std::make_shared<AestraUI::NUIIcon>(pianoRollSvg);
    m_pianoRollIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_pianoRollIcon->setColorFromTheme("textSecondary");

    // Metronome — solid trapezoid body with the pendulum arm and its weight
    // rising clear of it. The previous outline version stacked a full-height
    // trapezoid, a crossbar and a rod all in 1.8px strokes, which collapsed
    // into a smudge at 16px. Keeping the arm entirely outside the body means
    // it still reads when the whole glyph is one flat colour.
    const char* metronomeSvg = R"(
        <svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
            <path fill="currentColor" d="M9.6 12.2 H14.4 L19.8 20.6 H4.2 Z"/>
            <path d="M12.6 12.4 L17 3.4" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
            <circle cx="14.8" cy="7.9" r="1.7" fill="currentColor"/>
        </svg>
    )";
    m_metronomeIcon = std::make_shared<AestraUI::NUIIcon>(metronomeSvg);
    m_metronomeIcon->setIconSize(AestraUI::NUIIconSize::Medium);
    m_metronomeIcon->setColorFromTheme("textSecondary");

    // Count-In icon (3-2-1 dots style)
    // Three swelling beats — a count-in building to the downbeat. Shapes only:
    // the SVG renderer ignores <text>, so the previous version's "3" never drew
    // and all that survived was a row of dots stuck at the top of the viewBox.
    const char* countInSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <circle cx="5" cy="12" r="1.3"/>
            <circle cx="12" cy="12" r="1.9"/>
            <circle cx="19" cy="12" r="2.6"/>
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
    if(m_sequencerButton) m_sequencerButton->setTooltip("Arsenal (F6)");
    
    // Wire Record button
    if (m_recordButton) {
        m_recordButton->setOnToggle([this](bool armed) {
            Aestra::Log::info("Transport: Record Button Toggled: " + std::string(armed ? "ON" : "OFF"));
            if (m_onRecord) m_onRecord(armed);
        });
    }

    createViewButton(m_pianoRollButton, [this]() { if (m_onToggleView) m_onToggleView(Audio::ViewType::PianoRoll); });
    if(m_pianoRollButton) m_pianoRollButton->setTooltip("Piano Roll (F7)");

    // No Playlist button: it only toggled the same Timeline workspace the
    // title-bar Timeline tab already owns. Duplicate navigation is worse than
    // an ambiguous icon, so the control is gone rather than renamed. The
    // ViewType::Playlist route itself (F5, menus) is untouched.

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
        // Timeline has no toolbar control to light up — the title-bar tab owns it.
        case Audio::ViewType::Playlist: break;
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

void TransportBar::setMusicalTypingStatus(bool enabled, int octave) {
    if (!m_musicalTypingLabel) {
        return;
    }
    m_musicalTypingLabel->setText(enabled ? "KEYS C" + std::to_string(octave) : "KEYS OFF");
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    m_musicalTypingLabel->setTextColor(
        enabled ? theme.getColor("accentPrimary") : theme.getColor("textSecondary").withAlpha(0.62f));
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

    // Helper to render universal Glass Box button. Passing a label switches it
    // to the inline "tiny icon + micro-label" form used by DAW-specific
    // concepts; icon-only controls pass nullptr.
    auto renderGlassButton = [&](std::shared_ptr<AestraUI::NUIButton>& btn,
                                 std::shared_ptr<AestraUI::NUIIcon>& icon,
                                 bool isActive,
                                 bool isRecording = false,
                                 bool isPrimaryTransport = false,
                                 const char* label = nullptr) {
        if (!btn || !icon || !btn->isVisible()) return; // collapsed groups draw nothing

        AestraUI::NUIRect buttonRect = btn->getBounds(); // Use bounds set in layoutComponents
        bool isHovered = btn->isHovered() && btn->isEnabled();
        
        // Setup Colors
        AestraUI::NUIColor currentBg = glassBg;
        AestraUI::NUIColor currentBorder = glassBorder;
        AestraUI::NUIColor iconColor = iconGrey.withAlpha(0.55f);
        
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
            // No idle backing plate: primaries sit flush on the group shell
            // exactly like the time display (owner direction). State still
            // brings the tinted plate in on hover/active/record.
            if (isActive && !isRecording) {
                currentBg = themeManager.getColor("accentPrimary").withAlpha(0.22f);
                currentBorder = themeManager.getColor("accentPrimary").withAlpha(0.46f);
            }
            if (!isRecording) {
                iconColor = themeManager.getColor("textPrimary").withAlpha(isActive || isHovered ? 1.0f : 0.94f);
            }
        }

        // Draw Button Background
        if (isHovered || isActive || isRecording) {
            renderer.fillRoundedRect(buttonRect, themeManager.getRadius("m"), currentBg);
            if (currentBorder.a > 0.0f) {
                renderer.strokeRoundedRect(buttonRect, themeManager.getRadius("m"), 1.0f, currentBorder);
            }
        }
        
        if (!btn->isEnabled()) {
            iconColor = iconColor.withAlpha(0.3f);
        }

        if (label && label[0] != '\0') {
            // Inline form: tiny icon, then the word. The word carries the
            // meaning and the icon reinforces it, so the icon sits a step
            // dimmer than the label rather than competing with it.
            const AestraUI::NUIColor labelColor = iconColor;
            const AestraUI::NUIColor dimIconColor = iconColor.withAlpha(iconColor.a * 0.70f);

            const float iconY = buttonRect.y + (buttonRect.height - TRANSPORT_LABEL_ICON_SIZE) * 0.5f;
            AestraUI::NUIRect labelIconRect(buttonRect.x + TRANSPORT_LABEL_PAD_X, iconY,
                                            TRANSPORT_LABEL_ICON_SIZE, TRANSPORT_LABEL_ICON_SIZE);
            icon->setBounds(labelIconRect);
            icon->setColor(dimIconColor);
            icon->onRender(renderer);

            const float textX = labelIconRect.x + TRANSPORT_LABEL_ICON_SIZE + TRANSPORT_LABEL_ICON_GAP;
            renderer.drawText(label,
                              {textX, renderer.calculateTextY(buttonRect, TRANSPORT_LABEL_FONT_SIZE)},
                              TRANSPORT_LABEL_FONT_SIZE, labelColor);
            return;
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
        // No breathing shadow while playing (owner direction: flat active
        // state) — the accent plate from renderGlassButton is the indicator.
    }

    // Stop
    renderGlassButton(m_stopButton, m_stopIcon, false, false, true);

    // Use isToggled() for immediate visual feedback. 
    // 3rd arg (isActive): Controls pressed look. 4th arg (isRecording): Controls RED color.
    // We want RED only when toggled.
    renderGlassButton(m_recordButton, m_recordIcon, m_recordButton->isToggled(), m_recordButton->isToggled(), true);

    // --- Transport Extras (Left of Metronome) ---
    // Record aids are Aestra-specific concepts, so each carries its word.
    renderGlassButton(m_countInButton, m_countInIcon, m_countInActive, false, false, TRANSPORT_LABEL_COUNT_IN);
    renderGlassButton(m_waitButton, m_waitIcon, m_waitActive, false, false, TRANSPORT_LABEL_WAIT);
    renderGlassButton(m_loopRecordButton, m_loopRecordIcon, m_loopRecordActive, false, false, TRANSPORT_LABEL_LOOP_REC);

    // --- Metronome (Left of Center) ---
    // Icon only: a metronome is universally legible.
    renderGlassButton(m_metronomeButton, m_metronomeIcon, m_metronomeActive);

    // --- View Toggles (Right) ---
    // All icon-only: faders, a channel rack and a keyboard are vocabulary a
    // producer already has. Tooltips confirm rather than translate.
    renderGlassButton(m_mixerButton, m_mixerIcon, m_mixerActive);
    renderGlassButton(m_sequencerButton, m_sequencerIcon, m_sequencerActive);
    renderGlassButton(m_pianoRollButton, m_pianoRollIcon, m_pianoRollActive);
}

// =============================================================================
// SECTION: Layout
// =============================================================================

// ... (Previous code)

void TransportBar::setRightReservedWidth(float width) {
    if (m_rightReservedWidth == width) {
        return;
    }
    m_rightReservedWidth = width;
    layoutComponents();
}

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

    // --- Layout Logic: Center-Out Calculation ---
    // We calculate the required width first to center the island perfectly
    
    // Group 1: Transport (Play, Stop, Rec)
    float group1Width = (buttonSize * 3) + (spacing * 2);
    
    // Group 2: Extras (COUNT, WAIT, LOOP REC, metronome) — labelled controls
    // size to their word, so this is no longer 4 uniform squares.
    float group2Width = transportExtrasWidth();

    // Group 3: Info Display (Center)
    // Compact Info: 180px instead of 220px -> reduce to 160px for tighter packing?
    // Let's check TransportInfoContainer first, but for now allow 170.
    // Group 3: Info Display (Center)
    // Expanded Info: 220px to accommodate children
    float infoWidth = 260.0f;
    
    // Group 4: Views (MIX, RACK, piano roll) - 3 controls
    float group4Width = transportViewsWidth();

    // Which secondary groups fit? (hide extras first, then views — see helper)
    // The visualizers overlay the right of this row, so the island only owns the
    // width left of them.
    const float availWidth = std::max(0.0f, bounds.width - m_rightReservedWidth);
    const TransportLayoutTier tier = transportTierFor(availWidth);

    // Total Content Width — clusters set apart by a surface gap, extras tucked
    // tight to the transport buttons (one cluster). Order: transport, (extras),
    // info, (views).
    float totalContentWidth = group1Width
        + (tier.showExtras ? TRANSPORT_INTRA_GAP + group2Width : 0.0f)
        + TRANSPORT_SURFACE_GAP + infoWidth
        + (tier.showViews ? TRANSPORT_SURFACE_GAP + group4Width : 0.0f);
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

    // Stay centred on the window while there's room, but never slide under the
    // visualizers — with labelled controls the island is wide enough that a
    // plain centre would put the rightmost view button beneath the scope.
    if (m_rightReservedWidth > 0.0f) {
        const float rightLimit = bounds.width - m_rightReservedWidth;
        if (islandX + islandWidth > rightLimit) {
            islandX = std::round(std::max(0.0f, rightLimit - islandWidth));
        }
    }

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
    // Tight gap to the extras (same Transport cluster); if extras are collapsed,
    // it's a full surface gap onward to the musical-state cluster.
    xCursor += buttonSize + (tier.showExtras ? TRANSPORT_INTRA_GAP : TRANSPORT_SURFACE_GAP);

    // Group 2: Extras — hidden on narrow windows (see tier). Toggle visibility so
    // the buttons neither render nor take clicks when collapsed.
    // A null label means icon-only, so the control stays square; otherwise it is
    // as wide as its word. The hitbox is the full labelled width, so the word is
    // clickable, not just the glyph.
    const auto placeExtra = [&](const std::shared_ptr<AestraUI::NUIButton>& button, const char* label) {
        if (!button) return;
        button->setVisible(tier.showExtras);
        if (tier.showExtras) {
            const float w = label ? transportLabeledWidth(label) : buttonSize;
            button->setBounds(NUIAbsolute(bounds, xCursor, centerOffsetY, w, buttonSize));
            xCursor += w + spacing;
        }
    };
    placeExtra(m_countInButton, TRANSPORT_LABEL_COUNT_IN);
    placeExtra(m_waitButton, TRANSPORT_LABEL_WAIT);
    placeExtra(m_loopRecordButton, TRANSPORT_LABEL_LOOP_REC);
    placeExtra(m_metronomeButton, nullptr);
    if (tier.showExtras) {
        // Last extra added a trailing button spacing; swap it for the surface gap
        // that sets the Transport cluster apart from the musical-state cluster.
        xCursor += -spacing + TRANSPORT_SURFACE_GAP;
    }

    // Group 3: Info Container (always shown)
    if (m_infoContainer) {
        m_infoContainer->setBounds(NUIAbsolute(bounds, xCursor, islandY, infoWidth, islandHeight));
    }
    xCursor += infoWidth;

    // Group 4: Views — hidden first on the narrowest windows.
    const auto placeView = [&](const std::shared_ptr<AestraUI::NUIButton>& button, const char* label, bool advance) {
        if (!button) return;
        button->setVisible(tier.showViews);
        if (tier.showViews) {
            const float w = label ? transportLabeledWidth(label) : buttonSize;
            button->setBounds(NUIAbsolute(bounds, xCursor, centerOffsetY, w, buttonSize));
            if (advance) xCursor += w + spacing;
        }
    };
    if (tier.showViews) xCursor += TRANSPORT_SURFACE_GAP; // surface gap before the view cluster
    placeView(m_mixerButton, nullptr, true);
    placeView(m_sequencerButton, nullptr, true);
    placeView(m_pianoRollButton, nullptr, false);

    if (m_musicalTypingLabel) {
        constexpr float statusWidth = 82.0f;
        constexpr float statusHeight = 24.0f;
        const float statusX = islandX + islandWidth + 10.0f;
        const bool hasRoom = statusX + statusWidth <= bounds.width - 8.0f - m_rightReservedWidth;
        m_musicalTypingLabel->setVisible(hasRoom);
        if (hasRoom) {
            m_musicalTypingLabel->setBounds(NUIAbsolute(
                bounds, statusX, islandY + (islandHeight - statusHeight) * 0.5f, statusWidth, statusHeight));
        }
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
    float group1Width = (buttonSize * 3) + (spacing * 2);
    float group2Width = transportExtrasWidth();
    float infoWidth = TRANSPORT_INFO_WIDTH;
    float group4Width = transportViewsWidth();

    // Must mirror layoutComponents: same available width, same collapse tier.
    const float availWidth = std::max(0.0f, bounds.width - m_rightReservedWidth);
    const TransportLayoutTier tier = transportTierFor(availWidth);

    float totalContentWidth = group1Width
        + (tier.showExtras ? TRANSPORT_INTRA_GAP + group2Width : 0.0f)
        + TRANSPORT_SURFACE_GAP + infoWidth
        + (tier.showViews ? TRANSPORT_SURFACE_GAP + group4Width : 0.0f);
    float islandPadding = TRANSPORT_ISLAND_PADDING;
    float islandWidth = totalContentWidth + (islandPadding * 2.0f);
    
    if (islandWidth > bounds.width - 20.0f) islandWidth = bounds.width - 20.0f;
    
    const float islandHeight = std::min(TRANSPORT_ISLAND_HEIGHT, bounds.height);
    const float visualCenterBiasY = -1.0f;
    float islandX = std::round((bounds.width - islandWidth) * 0.5f);
    float islandY = std::round((bounds.height - islandHeight) * 0.5f + visualCenterBiasY);

    // Mirrors layoutComponents: keep the island clear of the visualizers.
    if (m_rightReservedWidth > 0.0f) {
        const float rightLimit = bounds.width - m_rightReservedWidth;
        if (islandX + islandWidth > rightLimit) {
            islandX = std::round(std::max(0.0f, rightLimit - islandWidth));
        }
    }

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
    
    // No per-group shells or separators: the toolbar reads as one purpose-built
    // instrument, with the transport / musical-state / workspace surfaces set
    // apart by spacing (in layoutComponents) rather than boxes and borders.
    (void)islandRect;

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
        // Labelled controls: the word on the button already says what it is, so
        // the tooltip confirms and adds the shortcut instead of translating.
        {m_countInButton, "Count-In"},
        {m_waitButton, "Wait for Input"},
        {m_loopRecordButton, "Loop Record"},
        {m_mixerButton, "Mixer (F3)"},
        {m_sequencerButton, "Arsenal (F6)"},
        {m_pianoRollButton, "Piano Roll (F7)"}
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
