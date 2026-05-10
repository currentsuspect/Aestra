// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file TransportInfoContainer.cpp
 * @brief Implementation of Transport Info Container components
 */

#include "TransportInfoContainer.h"
#include <sstream>
#include <iomanip>
#include <cmath>

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
                                9.0f, themeManager.getColor("textSecondary").withAlpha(0.58f));
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << m_displayBPM;
    renderer.drawTextCentered(ss.str(), {bounds.x, bounds.y + 9.0f, bounds.width, 19.0f},
                              13.0f, themeManager.getColor("textPrimary").withAlpha(0.95f));
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

std::string TimerDisplay::formatTime(double seconds) const {
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

void TimerDisplay::onRender(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    // Subtle readout background panel
    renderer.fillRoundedRect(bounds, 4.0f, themeManager.getColor("backgroundSecondary").withAlpha(0.38f));
    std::string timeText = formatTime(m_currentTime);
    renderer.drawTextCentered(timeText, {bounds.x, bounds.y + 4.0f, bounds.width, 20.0f},
                              15.0f, themeManager.getColor("textPrimary").withAlpha(0.95f));
}

bool TimerDisplay::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (getBounds().contains(event.position)) {
        AestraUI::NUIComponent::showRemoteTooltip("Playback time", event.position, this);
        return false;
    }
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
                              15.0f, textColor.withAlpha(0.95f));
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
