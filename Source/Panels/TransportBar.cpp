// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file TransportBar.cpp
 * @brief Transport bar implementation
 */

#include <string>
#include <cstdio>
#include <algorithm>
#include "TransportBar.h"
#include "../Components/TransportModuleStyle.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraCore/include/AestraLog.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <vector>
#include <utility>

namespace Aestra {

namespace {

namespace TM = TransportModule;

// Primary transport buttons: real controls with a plate, not bare glyphs.
constexpr float TRANSPORT_BUTTON_W = 34.0f;
constexpr float TRANSPORT_BUTTON_H = 30.0f;
constexpr float TRANSPORT_BUTTON_GAP = 6.0f;
constexpr float TRANSPORT_EDGE_PAD = 10.0f;
constexpr float TRANSPORT_PRIMARY_ICON = 18.0f;

// Record aids are lamp chips: a lamp that shows what's on, then the word. The
// vocabulary is deliberately terse. "Loop record" is never shortened to
// "Loop": that would collide with playback looping.
constexpr const char* TRANSPORT_LABEL_COUNT_IN = "Count-in";
constexpr const char* TRANSPORT_LABEL_WAIT = "Wait";
constexpr const char* TRANSPORT_LABEL_LOOP_REC = "Loop record";
constexpr float CHIP_H = 22.0f;
constexpr float CHIP_FONT = 11.0f;
constexpr float CHIP_PAD_X = 8.0f;
constexpr float CHIP_LAMP = 6.0f;
constexpr float CHIP_LAMP_GAP = 6.0f;
constexpr float CHIP_GAP = 4.0f;
constexpr float CHIP_ICON = 13.0f;
// The metronome chip carries its pendulum instead of a word: a metronome is
// universally legible, and the swinging arm is its own state readout.
constexpr float CHIP_METRONOME_W = CHIP_PAD_X * 2.0f + CHIP_LAMP + CHIP_LAMP_GAP + CHIP_ICON;

// Panel switches (mixer / Arsenal / piano roll): icon keys with tooltips.
constexpr float PANEL_BTN_W = 28.0f;
constexpr float PANEL_BTN_H = 24.0f;
constexpr float PANEL_GAP = 4.0f;
constexpr float PANEL_ICON = 15.0f;

constexpr float KEYS_MODULE_W = 72.0f;
constexpr float KEYS_VALUE_SIZE = 13.0f;

// layoutComponents runs without a renderer, so chip label widths are
// estimated rather than measured. Estimate high: the word is left-aligned
// after the lamp, so overshooting only adds trailing padding, while
// undershooting would clip the word, and a clipped label defeats the point.
inline float chipTextWidth(const char* text) {
    float w = 0.0f;
    for (const char* p = text; *p != '\0'; ++p) {
        w += (*p == ' ') ? CHIP_FONT * 0.30f : CHIP_FONT * 0.60f;
    }
    return w;
}

inline float chipWidth(const char* label) {
    return CHIP_PAD_X * 2.0f + CHIP_LAMP + CHIP_LAMP_GAP + chipTextWidth(label);
}

inline float transportModuleWidth() {
    return TRANSPORT_EDGE_PAD + TRANSPORT_BUTTON_W * 3.0f + TRANSPORT_BUTTON_GAP * 2.0f + TM::kPadX;
}

inline float recordModuleWidth() {
    return TM::kPadX * 2.0f + chipWidth(TRANSPORT_LABEL_COUNT_IN) + chipWidth(TRANSPORT_LABEL_WAIT)
         + chipWidth(TRANSPORT_LABEL_LOOP_REC) + CHIP_METRONOME_W + CHIP_GAP * 3.0f;
}

inline float panelsModuleWidth() {
    return TM::kPadX * 2.0f + PANEL_BTN_W * 3.0f + PANEL_GAP * 2.0f;
}

// Progressive collapse. The row holds modules in a fixed order; when the
// window can't hold them all, optional modules hide whole: keys first, then
// the panel switches, then the record aids. (The output scope and meter are
// shed before and between these by AestraContent, using requiredWidth.)
// Collapse must preserve MEANING, not merely save width: a control appears
// with its word intact or not at all. Transport, position and tempo never hide.
struct TransportLayoutTier {
    bool showRecord = true;
    bool showPanels = true;
    bool showKeys = true;
};

inline float tierWidth(const TransportLayoutTier& t) {
    return transportModuleWidth() + TransportInfoContainer::kPreferredWidth
         + (t.showRecord ? recordModuleWidth() : 0.0f)
         + (t.showPanels ? panelsModuleWidth() : 0.0f)
         + (t.showKeys ? KEYS_MODULE_W : 0.0f);
}

inline TransportLayoutTier transportTierFor(float availWidth) {
    const TransportLayoutTier ladder[] = {
        {true, true, true},
        {true, true, false},
        {true, false, false},
        {false, false, false},
    };
    for (const auto& tier : ladder) {
        if (availWidth >= tierWidth(tier)) return tier;
    }
    return ladder[3];
}

} // namespace

// =============================================================================
// SECTION: Construction & Setup
// =============================================================================


namespace {

// Metronome pendulum. The arm is knocked out of the solid body (SVG mask) so it
// reads over the trapezoid in one flat colour, and pivots at the base.
constexpr int kMetronomePoseCount = 9;
constexpr float kMetronomeSwingDegrees = 28.0f;
constexpr int kMetronomeRestPose = 6; // a gentle right lean when idle

float metronomePoseAngle(int pose) {
    const float step = (2.0f * kMetronomeSwingDegrees) / static_cast<float>(kMetronomePoseCount - 1);
    return -kMetronomeSwingDegrees + step * static_cast<float>(pose);
}

std::string metronomeSvgForAngle(float angleDegrees) {
    char rot[48];
    std::snprintf(rot, sizeof(rot), "rotate(%.1f 12 18.6)", angleDegrees);
    const std::string r(rot);
    return std::string(R"SVG(<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">)SVG") +
           R"SVG(<defs><mask id="arm"><rect width="24" height="24" fill="#fff"/>)SVG" +
           R"SVG(<g transform=")SVG" + r + R"SVG(">)SVG" +
           R"SVG(<path d="M12 18.6 V3.4" stroke="#000" stroke-width="3" stroke-linecap="round"/>)SVG" +
           R"SVG(<rect x="9.7" y="6.9" width="4.6" height="4" rx="1" fill="#000"/></g></mask></defs>)SVG" +
           R"SVG(<g mask="url(#arm)"><path fill="currentColor" d="M9.1 4.2 H14.9 L19.4 19.4 H4.6 Z"/>)SVG" +
           R"SVG(<rect x="3.8" y="19.2" width="16.4" height="2.2" rx="1.1" fill="currentColor"/></g>)SVG" +
           R"SVG(<g transform=")SVG" + r + R"SVG(">)SVG" +
           R"SVG(<path d="M12 18.6 V3.4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>)SVG" +
           R"SVG(<rect x="10.4" y="7.6" width="3.2" height="2.6" rx="0.6" fill="currentColor"/></g></svg>)SVG";
}

/** Pose for a transport beat position. The click sounds on each beat, so the
 *  arm reaches a side exactly then, alternating sides, and crosses centre
 *  half-way between clicks. */
int metronomePoseForBeat(double beat) {
    constexpr double kPi = 3.14159265358979323846;
    const double swing = std::cos(kPi * beat); // +1 on even beats, -1 on odd beats
    const long pose = std::lround((swing + 1.0) * 0.5 * static_cast<double>(kMetronomePoseCount - 1));
    return static_cast<int>(std::clamp(pose, 0L, static_cast<long>(kMetronomePoseCount - 1)));
}

} // namespace

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
            // Arrow edits bypass setTempo: resync the musical face here too,
            // before anyone downstream reads the clock.
            syncClockDisplays();
            if (m_onTempoChange) {
                m_onTempoChange(m_tempo);
            }
        });
    }

    // Forward time-signature clicks to the app (this link was missing: the
    // display cycled 2/4..7/8 but nothing outside the transport ever heard).
    if (m_infoContainer && m_infoContainer->getTimeSignatureDisplay()) {
        m_infoContainer->getTimeSignatureDisplay()->setOnTimeSignatureChange([this](int beatsPerBar) {
            // A new meter re-divides the same position: refresh the paused
            // musical face before forwarding.
            syncClockDisplays();
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
    // Shrunk from r=9 (18px, ~4× the visual area of Play/Stop) to a 12px
    // footprint that matches the Stop square, so the transport controls read
    // as one intentionally-weighted set rather than a dominant red dot.
    const char* recordSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <circle cx="12" cy="12" r="6"/>
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

    // Arsenal step sequencer icon.
    //
    // The previous glyph was three "channel strips" — a pad plus a long lane bar
    // per row — which reads as a list or a set of sliders, not as steps (spec 2
    // §10). A step sequencer is steps: equal cells in a grid, some lit, some not.
    //
    // Drawn as ONE solid card with the unlit steps punched out of it
    // (fill-rule="evenodd"), per the icon house rules: loose cells read as
    // confetti at 16px, whereas a solid silhouette with holes keeps its outline.
    // The holes are a deliberately uneven 4x3 pattern so it reads as a rhythm
    // rather than as a checkerboard or an "apps" grid, and no two holes touch, so
    // none of them cancel back to fill.
    const char* sequencerSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <rect x="2.4" y="6.0" width="3.6" height="5.1" rx="1.2"/>
            <rect x="7.6" y="6.0" width="3.6" height="5.1" rx="1.2"/>
            <rect x="12.8" y="6.0" width="3.6" height="5.1" rx="1.2"/>
            <rect x="18.0" y="6.0" width="3.6" height="5.1" rx="1.2"/>
            <rect x="2.4" y="12.9" width="3.6" height="5.1" rx="1.2"/>
            <rect x="7.6" y="12.9" width="3.6" height="5.1" rx="1.2"/>
            <rect x="12.8" y="12.9" width="3.6" height="5.1" rx="1.2"/>
            <rect x="18.0" y="12.9" width="3.6" height="5.1" rx="1.2"/>        </svg>
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

    // Metronome — one pendulum pose per NUIIcon (the icon cannot rotate), built
    // once. At rest the arm leans gently right; updateMetronomePose() swings it
    // while the metronome is on and the transport plays.
    m_metronomePoses.clear();
    for (int pose = 0; pose < kMetronomePoseCount; ++pose) {
        auto icon = std::make_shared<AestraUI::NUIIcon>(metronomeSvgForAngle(metronomePoseAngle(pose)));
        icon->setIconSize(AestraUI::NUIIconSize::Medium);
        icon->setColorFromTheme("textSecondary");
        m_metronomePoses.push_back(std::move(icon));
    }
    m_metronomePose = kMetronomeRestPose;
    m_metronomeIcon = m_metronomePoses[static_cast<size_t>(kMetronomeRestPose)];

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
        btn->setSize(TRANSPORT_BUTTON_W, TRANSPORT_BUTTON_H);
        
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
        updateMetronomePose();
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
    m_countInButton->setTooltip("Count in");
    
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
        // No local position reset: a single stop returns to the cue, which
        // only the model knows. The per-frame setPosition() sync from
        // TrackManager updates both clock faces; zeroing here flashed 0.
        updateButtonStates();

        if (m_infoContainer) {
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
    updateMetronomePose();
    if (m_infoContainer) {
        m_infoContainer->getBPMDisplay()->setBPM(m_tempo);
    }
    // Beats derive from the tempo: refresh the musical face while paused.
    syncClockDisplays();
    if (m_onTempoChange) {
        m_onTempoChange(m_tempo);
    }
}

void TransportBar::setPosition(double seconds) {
    m_position = std::max(0.0, seconds);
    syncClockDisplays();
    updateMetronomePose();
}

void TransportBar::syncClockDisplays() {
    if (!m_infoContainer || !m_infoContainer->getTimerDisplay()) {
        return;
    }
    m_infoContainer->getTimerDisplay()->setTime(m_position);
    // Musical clock mode needs beats + meter alongside seconds; the
    // position itself is untouched, only its representation changes.
    const int beatsPerBar = m_infoContainer->getTimeSignatureDisplay()
                                ? m_infoContainer->getTimeSignatureDisplay()->getBeatsPerBar()
                                : 4;
    const double beats = m_position * std::max(0.0f, m_tempo) / 60.0;
    m_infoContainer->getTimerDisplay()->setMusicalPosition(beats, beatsPerBar);
}

void TransportBar::updateMetronomePose() {
    if (m_metronomePoses.empty()) {
        return;
    }
    int pose = kMetronomeRestPose;
    if (m_metronomeActive && m_state == TransportState::Playing && m_tempo > 0.0f) {
        const double beat = m_position * static_cast<double>(m_tempo) / 60.0;
        pose = metronomePoseForBeat(beat);
    }
    if (pose != m_metronomePose) {
        m_metronomePose = pose;
        m_metronomeIcon = m_metronomePoses[static_cast<size_t>(pose)];
        setDirty(true);
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

    updateMetronomePose();

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
    // The KEYS module paints the state itself; the legacy pill stays hidden.
    m_musicalTypingLabel->setVisible(false);
    m_keysEnabled = enabled;
    m_keysOctave = octave;
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
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const AestraUI::NUIColor control = theme.getColor("surfaceTertiary");
    const AestraUI::NUIColor raised = theme.getColor("surfaceRaised");
    const AestraUI::NUIColor border = theme.getColor("border");
    const AestraUI::NUIColor borderStrong = theme.getColor("borderStrong");
    const AestraUI::NUIColor ink = theme.getColor("textPrimary");
    const AestraUI::NUIColor inkQuiet = theme.getColor("textSecondary");
    const AestraUI::NUIColor primary = theme.getColor("primary");
    const AestraUI::NUIColor recordRed = theme.getColor("error");
    const AestraUI::NUIColor lampOn = theme.getColor("warning");
    const AestraUI::NUIColor lampOff = theme.getColor("textDisabled");

    const auto drawIcon = [&](const std::shared_ptr<AestraUI::NUIIcon>& icon, const AestraUI::NUIRect& area,
                              float size, const AestraUI::NUIColor& color) {
        if (!icon) return;
        icon->setBounds(AestraUI::NUIRect(std::round(area.x + (area.width - size) * 0.5f),
                                          std::round(area.y + (area.height - size) * 0.5f), size, size));
        icon->setColor(color);
        icon->onRender(renderer);
    };

    // Play, stop, record: plated keys. Play fills violet while it plays; record
    // fills red while armed. Nothing else in the row is filled, so state reads
    // at a glance.
    const auto primaryKey = [&](const std::shared_ptr<AestraUI::NUIButton>& btn,
                                const std::shared_ptr<AestraUI::NUIIcon>& icon, bool active, bool isRecord) {
        if (!btn || !btn->isVisible()) return;
        const AestraUI::NUIRect rect = btn->getBounds();
        const bool hovered = btn->isHovered() && btn->isEnabled();
        AestraUI::NUIColor fill = hovered ? raised : control;
        AestraUI::NUIColor stroke = hovered ? borderStrong : border;
        AestraUI::NUIColor glyph = isRecord ? recordRed : (hovered ? ink : inkQuiet);
        if (active) {
            fill = isRecord ? recordRed : primary;
            stroke = AestraUI::NUIColor::transparent();
            glyph = isRecord ? theme.getColor("backgroundPrimary") : AestraUI::NUIColor::white();
        }
        if (!btn->isEnabled()) glyph = glyph.withAlpha(0.3f);
        renderer.fillRoundedRect(rect, 5.0f, fill);
        if (stroke.a > 0.0f) renderer.strokeRoundedRect(rect, 5.0f, 1.0f, stroke);
        drawIcon(icon, rect, TRANSPORT_PRIMARY_ICON, glyph);
    };

    const bool isPlaying = (m_state == TransportState::Playing);
    primaryKey(m_playButton, isPlaying ? m_pauseIcon : m_playIcon, isPlaying, false);
    primaryKey(m_stopButton, m_stopIcon, false, false);
    if (m_recordButton) {
        primaryKey(m_recordButton, m_recordIcon, m_recordButton->isToggled(), true);
    }

    // Record aids: lamp chips. The lamp is the state; the word is the meaning.
    const auto lampChip = [&](const std::shared_ptr<AestraUI::NUIButton>& btn, bool on, const char* label,
                              const std::shared_ptr<AestraUI::NUIIcon>& icon) {
        if (!btn || !btn->isVisible()) return;
        const AestraUI::NUIRect rect = btn->getBounds();
        const bool hovered = btn->isHovered() && btn->isEnabled();
        if (on || hovered) {
            renderer.fillRoundedRect(rect, 4.0f, on ? control : raised.withAlpha(0.6f));
        }
        renderer.strokeRoundedRect(rect, 4.0f, 1.0f, on || hovered ? borderStrong : border);

        const AestraUI::NUIPoint lampCenter(rect.x + CHIP_PAD_X + CHIP_LAMP * 0.5f, rect.y + rect.height * 0.5f);
        if (on) renderer.fillCircle(lampCenter, CHIP_LAMP, lampOn.withAlpha(0.22f));
        renderer.fillCircle(lampCenter, CHIP_LAMP * 0.5f, on ? lampOn : lampOff);

        const float contentX = rect.x + CHIP_PAD_X + CHIP_LAMP + CHIP_LAMP_GAP;
        const AestraUI::NUIColor text = (on || hovered) ? ink : inkQuiet;
        if (label) {
            renderer.drawText(label, {contentX, renderer.calculateTextY(rect, CHIP_FONT)}, CHIP_FONT, text);
        } else {
            drawIcon(icon, AestraUI::NUIRect(contentX, rect.y, CHIP_ICON, rect.height), CHIP_ICON, text);
        }
    };
    lampChip(m_countInButton, m_countInActive, TRANSPORT_LABEL_COUNT_IN, nullptr);
    lampChip(m_waitButton, m_waitActive, TRANSPORT_LABEL_WAIT, nullptr);
    lampChip(m_loopRecordButton, m_loopRecordActive, TRANSPORT_LABEL_LOOP_REC, nullptr);
    lampChip(m_metronomeButton, m_metronomeActive, nullptr, m_metronomeIcon);

    // Panel switches: an open panel gets a raised key with a violet underline.
    const auto panelKey = [&](const std::shared_ptr<AestraUI::NUIButton>& btn,
                              const std::shared_ptr<AestraUI::NUIIcon>& icon, bool open) {
        if (!btn || !btn->isVisible()) return;
        const AestraUI::NUIRect rect = btn->getBounds();
        const bool hovered = btn->isHovered() && btn->isEnabled();
        if (open || hovered) renderer.fillRoundedRect(rect, 4.0f, open ? raised : control);
        if (open) {
            renderer.fillRect(AestraUI::NUIRect(rect.x + 6.0f, rect.bottom() - 2.0f, rect.width - 12.0f, 2.0f),
                              primary);
        }
        drawIcon(icon, rect, PANEL_ICON, (open || hovered) ? ink : inkQuiet);
    };
    panelKey(m_mixerButton, m_mixerIcon, m_mixerActive);
    panelKey(m_sequencerButton, m_sequencerIcon, m_sequencerActive);
    panelKey(m_pianoRollButton, m_pianoRollIcon, m_pianoRollActive);
}

// =============================================================================
// SECTION: Layout
// =============================================================================

// ... (Previous code)

float TransportBar::requiredWidth(bool record, bool panels, bool keys) {
    return tierWidth(TransportLayoutTier{record, panels, keys});
}

void TransportBar::setRightReservedWidth(float width) {
    if (m_rightReservedWidth == width) {
        return;
    }
    m_rightReservedWidth = width;
    layoutComponents();
}

void TransportBar::layoutComponents() {
    const AestraUI::NUIRect bounds = getBounds();
    m_moduleMarks.clear();
    m_dividers.clear();

    // The output visualizers overlay the right of this row; the modules only
    // own the width left of them.
    const float availWidth = std::max(0.0f, bounds.width - m_rightReservedWidth);
    const TransportLayoutTier tier = transportTierFor(availWidth);

    // ── Transport: play · stop · record, centred on the row ──
    float x = TRANSPORT_EDGE_PAD;
    const float buttonY = std::round((bounds.height - TRANSPORT_BUTTON_H) * 0.5f);
    for (const auto* button : {&m_playButton, &m_stopButton, &m_recordButton}) {
        if (*button) {
            (*button)->setBounds(NUIAbsolute(bounds, x, buttonY, TRANSPORT_BUTTON_W, TRANSPORT_BUTTON_H));
        }
        x += TRANSPORT_BUTTON_W + TRANSPORT_BUTTON_GAP;
    }
    x = transportModuleWidth();
    m_dividers.push_back(x);

    // ── Position | Tempo ──
    if (m_infoContainer) {
        m_infoContainer->setBounds(
            NUIAbsolute(bounds, x, 0.0f, TransportInfoContainer::kPreferredWidth, bounds.height));
    }
    x += TransportInfoContainer::kPreferredWidth;
    m_dividers.push_back(x);

    // ── Record aids ──
    const float chipY = TM::kContentTop + std::round((TM::kContentHeight - CHIP_H) * 0.5f);
    const auto placeChip = [&](const std::shared_ptr<AestraUI::NUIButton>& button, float width, float& cx) {
        if (!button) return;
        button->setVisible(tier.showRecord);
        if (!tier.showRecord) return;
        button->setBounds(NUIAbsolute(bounds, cx, chipY, width, CHIP_H));
        cx += width + CHIP_GAP;
    };
    {
        float cx = x + TM::kPadX;
        placeChip(m_countInButton, chipWidth(TRANSPORT_LABEL_COUNT_IN), cx);
        placeChip(m_waitButton, chipWidth(TRANSPORT_LABEL_WAIT), cx);
        placeChip(m_loopRecordButton, chipWidth(TRANSPORT_LABEL_LOOP_REC), cx);
        placeChip(m_metronomeButton, CHIP_METRONOME_W, cx);
    }
    if (tier.showRecord) {
        m_moduleMarks.push_back({"RECORD", x, recordModuleWidth()});
        x += recordModuleWidth();
        m_dividers.push_back(x);
    }

    // ── Panels ──
    const float panelY = TM::kContentTop + std::round((TM::kContentHeight - PANEL_BTN_H) * 0.5f);
    {
        float px = x + TM::kPadX;
        for (const auto* button : {&m_mixerButton, &m_sequencerButton, &m_pianoRollButton}) {
            if (!*button) continue;
            (*button)->setVisible(tier.showPanels);
            if (tier.showPanels) {
                (*button)->setBounds(NUIAbsolute(bounds, px, panelY, PANEL_BTN_W, PANEL_BTN_H));
                px += PANEL_BTN_W + PANEL_GAP;
            }
        }
    }
    if (tier.showPanels) {
        m_moduleMarks.push_back({"PANELS", x, panelsModuleWidth()});
        x += panelsModuleWidth();
        m_dividers.push_back(x);
    }

    // ── Keys ──
    m_showKeys = tier.showKeys;
    if (tier.showKeys) {
        m_moduleMarks.push_back({"KEYS", x, KEYS_MODULE_W});
        m_keysValueRect = TM::contentRect(x + TM::kPadX, KEYS_MODULE_W - TM::kPadX * 2.0f, 0.0f);
        x += KEYS_MODULE_W;
        m_dividers.push_back(x);
    }

    if (m_musicalTypingLabel) {
        m_musicalTypingLabel->setVisible(false);
    }
}

void TransportBar::onRender(AestraUI::NUIRenderer& renderer) {
    AESTRA_ZONE("Transport_Render");
    const AestraUI::NUIRect bounds = getBounds();
    auto& theme = AestraUI::NUIThemeManager::getInstance();

    renderer.fillRect(bounds, theme.getColor("backgroundSecondary"));
    renderer.drawLine({bounds.x, bounds.bottom() - 1.0f}, {bounds.right(), bounds.bottom() - 1.0f}, 1.0f,
                      theme.getColor("border"));

    // One row of labelled modules, set apart by hairlines.
    for (const float dx : m_dividers) {
        TM::drawDivider(renderer, bounds.x + dx, bounds.y, bounds.height);
    }
    for (const auto& mark : m_moduleMarks) {
        TM::drawLabel(renderer, mark.label, bounds.x + mark.x + TM::kPadX, bounds.y);
    }

    if (m_showKeys) {
        const AestraUI::NUIRect r(bounds.x + m_keysValueRect.x, bounds.y + m_keysValueRect.y,
                                  m_keysValueRect.width, m_keysValueRect.height);
        const std::string value = m_keysEnabled ? "C" + std::to_string(m_keysOctave) : "Off";
        renderer.drawText(value, {r.x, renderer.calculateTextY(r, KEYS_VALUE_SIZE)}, KEYS_VALUE_SIZE,
                          m_keysEnabled ? theme.getColor("secondary") : theme.getColor("textMuted"));
    }

    // The output scope and meter are siblings drawn over the reserved region;
    // the bar names that region like any other module.
    if (m_rightReservedWidth > 0.0f) {
        const float outX = bounds.right() - m_rightReservedWidth;
        TM::drawDivider(renderer, outX, bounds.y, bounds.height);
        TM::drawLabel(renderer, "OUTPUT", outX + TM::kPadX, bounds.y);
    }

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
        {m_countInButton, "Count in"},
        {m_waitButton, "Wait for Input"},
        {m_loopRecordButton, "Loop Record"},
        {m_mixerButton, "Mixer (F3)"},
        {m_sequencerButton, "Arsenal (F6)"},
        {m_pianoRollButton, "Piano Roll (F7)"}
    };

    for (const auto& [button, text] : tooltipButtons) {
        if (updateTooltipForButton(button, text)) {
            AestraUI::NUIComponent::showRemoteTooltip(text, event.position, this);
            // Hand tool on clickable transport controls (consistent with the
            // grab-affordance sweep across draggable surfaces).
            if (m_platformBridge) m_platformBridge->setCursorStyle(AestraUI::NUICursorStyle::Hand);
            return handled;
        }
    }

    if (m_platformBridge) m_platformBridge->setCursorStyle(AestraUI::NUICursorStyle::Arrow);
    AestraUI::NUIComponent::hideRemoteTooltip(this);
    return handled;
}

void TransportBar::onMouseLeave() {
    // Release the hand cursor when the pointer leaves the bar, so it cannot
    // linger app-wide (the window-manager bridge override otherwise keeps the
    // last style until another component claims it).
    if (m_platformBridge) {
        m_platformBridge->setCursorStyle(AestraUI::NUICursorStyle::Arrow);
    }
    AestraUI::NUIComponent::hideRemoteTooltip(this);
    AestraUI::NUIComponent::onMouseLeave();
}

} // namespace Aestra
