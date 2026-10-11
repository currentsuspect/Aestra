// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file TransportInfoContainer.cpp
 * @brief Implementation of Transport Info Container components
 */

#include "TransportInfoContainer.h"
#include "TransportModuleStyle.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Aestra {

// Define a constant for the text baseline adjustment factor, as text rendering
// APIs often place y at the baseline, not the top of the glyph.
// This value (0.8f) is critical for vertical centering the text based on the 
// engine's font rendering.
const float TEXT_BASELINE_COMPENSATION_FACTOR = 0.8f;

// ============================================================================
// BPM Display Component
// ============================================================================

BPMDisplay::BPMDisplay()
    : AestraUI::NUIComponent()
    , m_currentBPM(120.0f)
    , m_targetBPM(120.0f)
    , m_displayBPM(120.0f)
    , m_upArrowHovered(false)
    , m_downArrowHovered(false)
    , m_upArrowPressed(false)
    , m_downArrowPressed(false)
    , m_isHovered(false)
    , m_pulseAnimation(0.0f)
    , m_holdTimer(0.0f)
    , m_holdDelay(0.0f)
{
    // Create up arrow icon (small triangle pointing up)
    const char* upArrowSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M7 14l5-5 5 5z"/>
        </svg>
    )";
    m_upArrow = std::make_shared<AestraUI::NUIIcon>(upArrowSvg);
    m_upArrow->setIconSize(AestraUI::NUIIconSize::Small);
    m_upArrow->setColorFromTheme("textSecondary"); 	// #9a9aa3 - Inactive by default
    
    // Create down arrow icon (small triangle pointing down)
    const char* downArrowSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M7 10l5 5 5-5z"/>
        </svg>
    )";
    m_downArrow = std::make_shared<AestraUI::NUIIcon>(downArrowSvg);
    m_downArrow->setIconSize(AestraUI::NUIIconSize::Small);
    m_downArrow->setColorFromTheme("textSecondary");
}

BPMDisplay::~BPMDisplay() {
    // Torn down mid-edit: the editor's callbacks capture `this`, and it can
    // outlive the display via deferred-removal parking. Nothing fires focus
    // loss during teardown today, so this is not a live crash; dropping the
    // callbacks makes that independent of a future change to component
    // destruction order. (Same shape as UIMixerFader.)
    if (m_editInput) {
        m_editInput->setOnReturnKey(nullptr);
        m_editInput->setOnEscapeKey(nullptr);
        m_editInput->setOnFocusLost(nullptr);
    }
}

void BPMDisplay::setBPM(float bpm) {
    m_targetBPM = std::max(20.0f, std::min(999.0f, bpm));
    m_currentBPM = m_targetBPM;
    m_displayBPM = m_targetBPM; // Also update display to prevent animation conflicts
}

void BPMDisplay::incrementBPM(float amount) {
    setBPM(m_currentBPM + amount);
    if (m_onBPMChange) {
        m_onBPMChange(m_currentBPM);
    }
}

void BPMDisplay::decrementBPM(float amount) {
    setBPM(m_currentBPM - amount);
    if (m_onBPMChange) {
        m_onBPMChange(m_currentBPM);
    }
}

std::string BPMDisplay::trimBPMValue(float bpm) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(bpm));
    std::string text(buf);
    text.erase(text.find_last_not_of('0') + 1, std::string::npos);
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text.empty() ? std::string("0") : text;
}

void BPMDisplay::openBPMEditor() {
    if (m_editInput) {
        return;
    }
    AestraUI::NUIRect bounds = getBounds();
    auto input = std::make_shared<AestraUI::NUITextInput>(trimBPMValue(m_currentBPM));
    input->setInputType(AestraUI::NUITextInput::InputType::Number);
    input->setJustification(AestraUI::NUITextInput::Justification::Center);
    // NUITextInput draws at the theme "m" size and needs line-height room: a
    // 19 px strip clips the glyphs out entirely (same trap as the fader
    // readout — see UIMixerFader). Size to the font and bottom-align over the
    // value so the digits stay where they were; the transient field may cover
    // the micro label above, same tradeoff the fader makes.
    const float valueBottom = bounds.bottom();
    const float editH =
        std::ceil(AestraUI::NUIThemeManager::getInstance().getFontSize("m") * 1.9f) + 4.0f;
    input->setBounds(AestraUI::NUIRect(bounds.x, valueBottom - editH, bounds.width, editH));
    // Same field chrome as the mixer fader's inline editor: themed text on
    // an input background with a purple focused border, so the open editor
    // reads as focused instead of a blank box.
    {
        auto& themeManager = AestraUI::NUIThemeManager::getInstance();
        input->setTextColor(themeManager.getColor("textPrimary"));
        input->setBackgroundColor(themeManager.getColor("inputBgDefault"));
        input->setBorderColor(themeManager.getColor("inputBorderFocus"));
        input->setBorderWidth(1.0f);
        input->setBorderRadius(3.0f);
        input->setFocusedBorderColor(themeManager.getColor("accentPrimary"));
        input->setPadding(4.0f);
    }
    input->setOnReturnKey([this]() { commitBPMEdit(); });
    input->setOnEscapeKey([this]() { cancelBPMEdit(); });
    // Clicking away applies what's typed (same convention as the mixer
    // fader's inline editor). commitBPMEdit is null-guarded, so the removal
    // below retriggering focus loss is harmless.
    input->setOnFocusLost([this]() { commitBPMEdit(); });
    addChild(input);
    input->setFocused(true);
    input->selectAll();
    m_editInput = std::move(input);
    setDirty(true);
}

void BPMDisplay::commitBPMEdit() {
    if (!m_editInput) {
        return;
    }
    std::string text = m_editInput->getText();
    closeBPMEditor();
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return; // Empty: reject, keep the current value.
    }
    const auto last = text.find_last_not_of(" \t");
    char* end = nullptr;
    const double value = std::strtod(text.c_str() + first, &end);
    if (end != text.c_str() + last + 1 || !std::isfinite(value)) {
        return; // Not a plain number: reject, keep the current value.
    }
    // Same path as the arrows: existing 20..999 limits constrain.
    setBPM(static_cast<float>(value));
    if (m_onBPMChange) {
        m_onBPMChange(m_currentBPM);
    }
    m_pulseAnimation = 1.0f;
}

void BPMDisplay::cancelBPMEdit() {
    closeBPMEditor();
}

void BPMDisplay::closeBPMEditor() {
    if (!m_editInput) {
        return;
    }
    removeChild(m_editInput);
    m_editInput.reset();
    setDirty(true);
}

// The step arrows own the display's right-hand column: up in the top half,
// down in the bottom. onRender draws them in exactly these rects and keeps the
// value and its suffix out of the column, so every hit zone is visible.
AestraUI::NUIRect BPMDisplay::getUpArrowBounds() const {
    const AestraUI::NUIRect bounds = getBounds();
    const float half = bounds.height * 0.5f;
    return AestraUI::NUIRect(bounds.right() - kArrowColumnWidth, bounds.y, kArrowColumnWidth, half - 1.0f);
}

AestraUI::NUIRect BPMDisplay::getDownArrowBounds() const {
    const AestraUI::NUIRect bounds = getBounds();
    const float half = bounds.height * 0.5f;
    return AestraUI::NUIRect(bounds.right() - kArrowColumnWidth, bounds.y + half + 1.0f, kArrowColumnWidth,
                             half - 1.0f);
}

void BPMDisplay::onUpdate(double deltaTime) {
    // Smooth scrolling animation towards target BPM
    const float animSpeed = 8.0f; // Faster animation for responsiveness
    float diff = m_targetBPM - m_displayBPM;
    
    if (std::abs(diff) > 0.01f) {
        m_displayBPM += diff * animSpeed * static_cast<float>(deltaTime);
    } else {
        m_displayBPM = m_targetBPM;
    }
    
    // Decay pulse animation
    if (m_pulseAnimation > 0.0f) {
        m_pulseAnimation -= static_cast<float>(deltaTime) * 4.0f; // Fast decay
        if (m_pulseAnimation < 0.0f) m_pulseAnimation = 0.0f;
    }
    
    // Hold-to-repeat: continuously adjust BPM when arrow is held
    if (m_upArrowPressed || m_downArrowPressed) {
        m_holdDelay -= static_cast<float>(deltaTime);
        if (m_holdDelay <= 0.0f) {
            m_holdTimer += static_cast<float>(deltaTime);
            // After 0.3s initial delay, repeat every 50ms
            if (m_holdTimer >= 0.05f) {
                m_holdTimer = 0.0f;
                if (m_upArrowPressed) {
                    incrementBPM(1.0f);
                } else {
                    decrementBPM(1.0f);
                }
            }
        }
    }
    
    AestraUI::NUIComponent::onUpdate(deltaTime);
}

void BPMDisplay::onRender(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    // The TEMPO module label names this reading, so the unit rides as a quiet
    // suffix after the value instead of a micro label stacked above it.
    // While the inline editor is open it draws the value itself; drawing the
    // label underneath would double-print it.
    if (!m_editInput) {
        constexpr float kValueSize = 18.0f;
        constexpr float kUnitSize = 9.0f;
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << m_displayBPM;
        const std::string value = ss.str();
        AestraUI::NUIColor bpmColor =
            m_isHovered ? themeManager.getColor("secondary") : themeManager.getColor("textPrimary");
        const float valueY = renderer.calculateTextY(bounds, kValueSize);
        renderer.drawText(value, {bounds.x, valueY}, kValueSize, bpmColor);
        const float valueW = renderer.measureText(value, kValueSize).width;
        const AestraUI::NUIRect unitRect(bounds.x + valueW + 4.0f, bounds.y + 3.0f, 30.0f, bounds.height);
        // The suffix is dropped rather than drawn into the arrow column.
        const float arrowLeft = bounds.right() - kArrowColumnWidth;
        if (unitRect.x + renderer.measureText("BPM", kUnitSize).width <= arrowLeft - 2.0f) {
            renderer.drawText("BPM", {unitRect.x, renderer.calculateTextY(unitRect, kUnitSize)}, kUnitSize,
                              themeManager.getColor("textMuted"));
        }

        const auto chevron = [&](const AestraUI::NUIRect& r, bool up, bool hot) {
            const float cx = r.x + r.width * 0.5f;
            const float cy = r.y + r.height * 0.5f;
            const float dy = up ? 1.5f : -1.5f;
            const AestraUI::NUIColor ink =
                hot ? themeManager.getColor("textPrimary") : themeManager.getColor("textMuted");
            const AestraUI::NUIPoint pts[3] = {{cx - 3.5f, cy + dy}, {cx, cy - dy}, {cx + 3.5f, cy + dy}};
            renderer.drawPolyline(pts, 3, 1.4f, ink);
        };
        chevron(getUpArrowBounds(), true, m_upArrowHovered || m_upArrowPressed);
        chevron(getDownArrowBounds(), false, m_downArrowHovered || m_downArrowPressed);
    }
    // The inline editor is a child: without this it exists, takes focus and
    // suppresses the label, but never paints (invisible-field defect).
    renderChildren(renderer);
}

bool BPMDisplay::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    AestraUI::NUIRect bounds = getBounds();
    AestraUI::NUIRect upBounds = getUpArrowBounds();
    AestraUI::NUIRect downBounds = getDownArrowBounds();
    
    bool inBounds = bounds.contains(event.position);
    bool inUp = upBounds.contains(event.position);
    bool inDown = downBounds.contains(event.position);
    
    // Track hover state for visual feedback
    bool wasHovered = m_isHovered;
    m_isHovered = inBounds;
    m_upArrowHovered = inUp;
    m_downArrowHovered = inDown;
    
    // Handle mouse wheel for fine adjustment (anywhere on BPM display)
    if (event.wheelDelta != 0.0f && inBounds) {
        // A wheel turn elsewhere commits nothing: an open edit is abandoned
        // first so the wheel never fights typed text.
        cancelBPMEdit();
        // Modifier keys: Shift = 5x faster, Ctrl = 0.1x for fine control
        float increment = 1.0f;
        if (event.modifiers & AestraUI::NUIModifiers::Shift) {
            increment = 5.0f;
        } else if (event.modifiers & AestraUI::NUIModifiers::Ctrl) {
            increment = 0.1f;
        }
        
        if (event.wheelDelta > 0) {
            incrementBPM(increment);
        } else {
            decrementBPM(increment);
        }
        m_pulseAnimation = 1.0f; // Trigger pulse
        return true;
    }
    
    // Handle mouse button for arrow clicks
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        if (inUp || inDown) {
            // Arrow input abandons an open edit (nothing typed is applied).
            cancelBPMEdit();
        } else if (inBounds) {
            // Double-tap/click the value opens it for direct editing. The
            // native doubleClick flag is never set by the bridge, so use the
            // same manual window as the panel title bars. Epoch-initialized:
            // the first click can never count as a double.
            const auto now = std::chrono::steady_clock::now();
            const auto elapsedMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastValueClickTime).count();
            m_lastValueClickTime = now;
            if (elapsedMs < 350) {
                openBPMEditor();
                return true;
            }
        }
        if (inUp) {
            m_upArrowPressed = true;
            m_holdDelay = 0.3f;  // 300ms before repeat starts
            m_holdTimer = 0.0f;
            incrementBPM(1.0f);
            m_pulseAnimation = 1.0f;
            return true;
        }
        if (inDown) {
            m_downArrowPressed = true;
            m_holdDelay = 0.3f;
            m_holdTimer = 0.0f;
            decrementBPM(1.0f);
            m_pulseAnimation = 1.0f;
            return true;
        }
    }
    
    // Handle mouse button release
    if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
        m_upArrowPressed = false;
        m_downArrowPressed = false;
    }
    
    // Capture hover changes for redraw
    if (wasHovered != m_isHovered || inUp || inDown) {
        if (inBounds) {
            AestraUI::NUIComponent::showRemoteTooltip("BPM - scroll to adjust", getBounds(), this);
        } else {
            AestraUI::NUIComponent::hideRemoteTooltip(this);
        }
        return true; // Consume event to trigger redraw
    }
    
    return AestraUI::NUIComponent::onMouseEvent(event);
}

// ============================================================================
// Timer Display Component
// ============================================================================

TimerDisplay::TimerDisplay()
    : AestraUI::NUIComponent()
    , m_currentTime(0.0)
    , m_isPlaying(false)
{
}

void TimerDisplay::setTime(double seconds) {
    m_currentTime = std::max(0.0, seconds);
}

void TimerDisplay::setMusicalPosition(double beats, int beatsPerBar) {
    m_positionBeats = std::max(0.0, beats);
    m_beatsPerBar = std::max(1, beatsPerBar);
}

void TimerDisplay::toggleDisplayMode() {
    m_displayMode = (m_displayMode == DisplayMode::Time) ? DisplayMode::Musical : DisplayMode::Time;
    setDirty(true);
}

std::string TimerDisplay::formatTime(double seconds) {
    int totalSeconds = static_cast<int>(seconds);
    int minutes = totalSeconds / 60;
    int secs = totalSeconds % 60;
    int millis = static_cast<int>((seconds - totalSeconds) * 100);

    std::stringstream ss;
    ss << minutes << ":" << std::setfill('0')
        << std::setw(2) << secs << "."
        << std::setw(2) << millis;
    return ss.str();
}

std::string TimerDisplay::formatBarBeatSixteenth(double beats, int beatsPerBar) {
    const int bpb = std::max(1, beatsPerBar);
    // A hair of tolerance so an exact downbeat computed as 3.9999999 reads as
    // the next bar, not as the last sixteenth of this one.
    const double clamped = std::max(0.0, beats) + 1e-7;
    const long long sixteenths = static_cast<long long>(std::floor(clamped * 4.0));
    const long long perBar = static_cast<long long>(bpb) * 4;
    const long long bar = sixteenths / perBar;
    const long long inBar = sixteenths % perBar;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld.%lld.%lld", bar + 1, inBar / 4 + 1, inBar % 4 + 1);
    return std::string(buf);
}

std::string TimerDisplay::formatMusical(double beats, int beatsPerBar) {
    const int bpb = std::max(1, beatsPerBar);
    const double clamped = std::max(0.0, beats);
    // Round to centibeats BEFORE splitting bar/beat: a value like 3.995 in
    // 4/4 must carry into 2:1.00, not print an invalid 1:5.00.
    const double rounded = std::round(clamped * 100.0) / 100.0;
    const double barIndex = std::floor(rounded / bpb);
    const double beatInBar = rounded - barIndex * bpb;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%d:%.2f", static_cast<int>(barIndex) + 1, beatInBar + 1.0);
    return std::string(buf);
}

void TimerDisplay::onRender(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    // The large reading is the one you use while playing; the other rides
    // beside it, small, so both are always on screen. Clicking swaps them.
    constexpr float kPrimarySize = 21.0f;
    constexpr float kSecondarySize = 11.0f;
    const bool musical = (m_displayMode == DisplayMode::Musical);
    const std::string primary =
        musical ? formatBarBeatSixteenth(m_positionBeats, m_beatsPerBar) : formatTime(m_currentTime);
    const std::string secondary =
        musical ? formatTime(m_currentTime) : formatBarBeatSixteenth(m_positionBeats, m_beatsPerBar);

    const AestraUI::NUIColor ink = themeManager.getColor("textPrimary");
    const AestraUI::NUIColor separatorInk = themeManager.getColor("textDisabled");
    const float y = renderer.calculateTextY(bounds, kPrimarySize);
    float x = bounds.x;
    // Separators are drawn a step dimmer so the numbers read as numbers.
    std::string run;
    const auto flush = [&]() {
        if (run.empty()) return;
        renderer.drawText(run, {x, y}, kPrimarySize, ink);
        x += renderer.measureText(run, kPrimarySize).width;
        run.clear();
    };
    for (const char c : primary) {
        if (c == '.' || c == ':') {
            flush();
            const std::string sep(1, c);
            renderer.drawText(sep, {x, y}, kPrimarySize, separatorInk);
            x += renderer.measureText(sep, kPrimarySize).width;
        } else {
            run.push_back(c);
        }
    }
    flush();

    const AestraUI::NUIRect secondaryRect(x + 9.0f, bounds.y + 3.0f, bounds.right() - x - 9.0f, bounds.height);
    if (renderer.measureText(secondary, kSecondarySize).width <= secondaryRect.width) {
        renderer.drawText(secondary, {secondaryRect.x, renderer.calculateTextY(secondaryRect, kSecondarySize)},
                          kSecondarySize, themeManager.getColor("textSecondary"));
    }
}

bool TimerDisplay::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (getBounds().contains(event.position)) {
        if (event.pressed && event.button == AestraUI::NUIMouseButton::Left && !event.synthetic) {
            m_tapArmed = true;
            return true;
        }
        if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
            // A synthetic release (focus loss, post-capture re-resolution) is a
            // stop, not an accept: disarm without toggling.
            if (m_tapArmed && !event.synthetic) {
                toggleDisplayMode();
            }
            m_tapArmed = false;
            return true;
        }
        AestraUI::NUIComponent::showRemoteTooltip("Position — click to swap bars and clock time", getBounds(), this);
        return false;
    }
    m_tapArmed = false;
    AestraUI::NUIComponent::hideRemoteTooltip(this);
    return false;
}

// ============================================================================
// Time Signature Display Component
// ============================================================================

TimeSignatureDisplay::TimeSignatureDisplay()
    : AestraUI::NUIComponent()
    , m_beatsPerBar(4)
    , m_isHovered(false)
{
}

void TimeSignatureDisplay::cycleNext() {
    // Common time signatures: 2/4, 3/4, 4/4, 5/4, 6/8, 7/8
    static const int signatures[] = {2, 3, 4, 5, 6, 7};
    static const int numSignatures = 6;
    
    int currentIndex = 0;
    for (int i = 0; i < numSignatures; ++i) {
        if (signatures[i] == m_beatsPerBar) {
            currentIndex = i;
            break;
        }
    }
    
    m_beatsPerBar = signatures[(currentIndex + 1) % numSignatures];
    
    if (m_onTimeSignatureChange) {
        m_onTimeSignatureChange(m_beatsPerBar);
    }
    
    setDirty(true);
}

std::string TimeSignatureDisplay::getDisplayText() const {
    // Format as "X/4" or "X/8" depending on beats
    int denominator = (m_beatsPerBar == 6 || m_beatsPerBar == 7) ? 8 : 4;
    return std::to_string(m_beatsPerBar) + "/" + std::to_string(denominator);
}

void TimeSignatureDisplay::onRender(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    AestraUI::NUIColor textColor = m_isHovered ? themeManager.getColor("secondary") : themeManager.getColor("textSecondary");
    std::string text = getDisplayText();
    constexpr float kMeterSize = 13.0f;
    renderer.drawText(text, {bounds.x, renderer.calculateTextY(bounds, kMeterSize)}, kMeterSize, textColor);
}


bool TimeSignatureDisplay::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    AestraUI::NUIRect bounds = getBounds();
    bool inside = bounds.contains(event.position);
    
    // Track hover state
    bool wasHovered = m_isHovered;
    m_isHovered = inside;
    
    // Handle click to cycle time signature
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        if (inside) {
            cycleNext();
            return true;
        }
    }
    
    // Capture hover changes for redraw
    if (wasHovered != m_isHovered) {
        if (inside) {
            AestraUI::NUIComponent::showRemoteTooltip("Time signature - click to cycle", getBounds(), this);
        } else {
            AestraUI::NUIComponent::hideRemoteTooltip(this);
        }
        setDirty(true);
        return true;
    }
    
    return false;
}

// ============================================================================
// Transport Info Container
// ============================================================================

TransportInfoContainer::TransportInfoContainer()
    : AestraUI::NUIComponent()
{
    m_timerDisplay = std::make_shared<TimerDisplay>();
    addChild(m_timerDisplay);
    
    m_bpmDisplay = std::make_shared<BPMDisplay>();
    addChild(m_bpmDisplay);
    
    m_timeSignatureDisplay = std::make_shared<TimeSignatureDisplay>();
    addChild(m_timeSignatureDisplay);
    
    layoutComponents();
}

void TransportInfoContainer::layoutComponents() {
    namespace TM = TransportModule;
    const AestraUI::NUIRect bounds = getBounds();

    // POSITION | TEMPO. Each display owns only its module's content line; the
    // caps labels above are painted by this container.
    const float posX = std::floor(bounds.x + TM::kPadX);
    if (m_timerDisplay) {
        m_timerDisplay->setBounds(TM::contentRect(posX, kPositionModuleWidth - TM::kPadX * 1.5f, bounds.y));
    }

    const float tempoX = std::floor(bounds.x + kPositionModuleWidth + TM::kPadX);
    constexpr float kBPMWidth = 88.0f;
    if (m_bpmDisplay) {
        m_bpmDisplay->setBounds(TM::contentRect(tempoX, kBPMWidth, bounds.y));
    }
    if (m_timeSignatureDisplay) {
        m_timeSignatureDisplay->setBounds(TM::contentRect(tempoX + kBPMWidth + 6.0f, 32.0f, bounds.y));
    }
}

void TransportInfoContainer::onRender(AestraUI::NUIRenderer& renderer) {
    namespace TM = TransportModule;
    const AestraUI::NUIRect bounds = getBounds();
    TM::drawLabel(renderer, "POSITION", bounds.x + TM::kPadX, bounds.y);
    TM::drawDivider(renderer, bounds.x + kPositionModuleWidth, bounds.y, bounds.height);
    TM::drawLabel(renderer, "TEMPO", bounds.x + kPositionModuleWidth + TM::kPadX, bounds.y);
    renderChildren(renderer);
}

void TransportInfoContainer::onResize(int width, int height) {
    AestraUI::NUIRect currentBounds = getBounds();
    setBounds(AestraUI::NUIRect(currentBounds.x, currentBounds.y, width, height));
    layoutComponents();
    AestraUI::NUIComponent::onResize(width, height);
}

bool TransportInfoContainer::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    // Forward mouse events to children
    if (m_timeSignatureDisplay && m_timeSignatureDisplay->getBounds().contains(event.position)) {
        if (m_timeSignatureDisplay->onMouseEvent(event)) {
            return true;
        }
    }
    
    if (m_bpmDisplay && m_bpmDisplay->getBounds().contains(event.position)) {
        if (m_bpmDisplay->onMouseEvent(event)) {
            return true;
        }
    }
    
    if (m_timerDisplay && m_timerDisplay->getBounds().contains(event.position)) {
        if (m_timerDisplay->onMouseEvent(event)) {
            return true;
        }
    }
    
    return AestraUI::NUIComponent::onMouseEvent(event);
}

} // namespace Aestra
