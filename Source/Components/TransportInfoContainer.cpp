// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file TransportInfoContainer.cpp
 * @brief Implementation of Transport Info Container components
 */

#include "TransportInfoContainer.h"
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
    input->setBounds(AestraUI::NUIRect(bounds.x, bounds.y + 9.0f, bounds.width, 19.0f));
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

AestraUI::NUIRect BPMDisplay::getUpArrowBounds() const {
    if (m_cachedUpArrowBounds.width > 0) return m_cachedUpArrowBounds;
    // Fallback if not rendered yet
    AestraUI::NUIRect bounds = getBounds();
    return AestraUI::NUIRect(bounds.x + bounds.width - 20, bounds.y, 12, 12);
}

AestraUI::NUIRect BPMDisplay::getDownArrowBounds() const {
    if (m_cachedDownArrowBounds.width > 0) return m_cachedDownArrowBounds;
    AestraUI::NUIRect bounds = getBounds();
    return AestraUI::NUIRect(bounds.x + bounds.width - 20, bounds.y + 15, 12, 12);
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
    // Small "BPM" label above the value
    renderer.drawTextCentered("BPM", {bounds.x, bounds.y, bounds.width, 10.0f},
                                themeManager.getFontSize("micro"),
                                themeManager.getColor("textSecondary").withAlpha(0.75f));
    // While the inline editor is open it draws the value itself; drawing the
    // label underneath would double-print it.
    if (!m_editInput) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << m_displayBPM;
        AestraUI::NUIColor bpmColor =
            m_isHovered ? themeManager.getColor("accentPrimary") : themeManager.getColor("textPrimary");
        renderer.drawTextCentered(ss.str(), {bounds.x, bounds.y + 9.0f, bounds.width, 19.0f},
                                  themeManager.getFontSize("l"), bpmColor.withAlpha(0.95f));
    }
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
            AestraUI::NUIComponent::showRemoteTooltip("BPM - scroll to adjust", event.position, this);
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

std::string TimerDisplay::formatMusical(double beats, int beatsPerBar) {
    const int bpb = std::max(1, beatsPerBar);
    const double clamped = std::max(0.0, beats);
    const double barIndex = std::floor(clamped / bpb);
    const double beatInBar = clamped - barIndex * bpb;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%d:%.2f", static_cast<int>(barIndex) + 1, beatInBar + 1.0);
    return std::string(buf);
}

void TimerDisplay::onRender(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    // Time is transport information, not a standalone button. A near-invisible
    // tonal well keeps it aligned with BPM/time signature without a third box.
    renderer.fillRoundedRect(bounds, themeManager.getRadius("s"),
                             themeManager.getColor("surfaceRaised").withAlpha(0.045f));
    std::string timeText = (m_displayMode == DisplayMode::Musical)
                                 ? formatMusical(m_positionBeats, m_beatsPerBar)
                                 : formatTime(m_currentTime);
    renderer.drawTextCentered(timeText, {bounds.x, bounds.y + 4.0f, bounds.width, 20.0f},
                              themeManager.getFontSize("xl"), themeManager.getColor("textPrimary").withAlpha(0.95f));
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
        AestraUI::NUIComponent::showRemoteTooltip("Playback time — click to toggle bars/beats", event.position, this);
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
    AestraUI::NUIColor textColor = m_isHovered ? themeManager.getColor("accentPrimary") : themeManager.getColor("textPrimary");
    std::string text = getDisplayText();
    renderer.drawTextCentered(text, {bounds.x, bounds.y + 6.0f, bounds.width, 18.0f},
                              themeManager.getFontSize("xl"), textColor.withAlpha(0.95f));
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
            AestraUI::NUIComponent::showRemoteTooltip("Time signature - click to cycle", event.position, this);
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
    AestraUI::NUIRect bounds = getBounds();
    
    // Expanded Layout: [TimeSig 50px] [BPM 90px] [Timer 80px] = ~220px
    
    float padding = 4.0f; // Increased padding
    float contentHeight = 28.0f;
    float yPos = bounds.y + (bounds.height - contentHeight) / 2.0f;
    
    float currentX = bounds.x;
    
    // 1. Time Signature (Left)
    float timeSigWidth = 50.0f; // Increased
    if (m_timeSignatureDisplay) {
        m_timeSignatureDisplay->setBounds(AestraUI::NUIRect(std::floor(currentX), std::floor(yPos), timeSigWidth, contentHeight));
    }
    currentX += timeSigWidth + padding;
    
    // 2. BPM (Center/Next)
    float bpmWidth = 90.0f; // Increased from 70
    if (m_bpmDisplay) {
        m_bpmDisplay->setBounds(AestraUI::NUIRect(std::floor(currentX), std::floor(yPos), bpmWidth, contentHeight));
    }
    currentX += bpmWidth + padding;

    // 3. Timer (Right)
    float timerWidth = 76.0f; // Increased from 65
    if (m_timerDisplay) {
        m_timerDisplay->setBounds(AestraUI::NUIRect(std::floor(currentX), std::floor(yPos), timerWidth, contentHeight));
    }
}

void TransportInfoContainer::onRender(AestraUI::NUIRenderer& renderer) {
    // No background rendering - just render children
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
