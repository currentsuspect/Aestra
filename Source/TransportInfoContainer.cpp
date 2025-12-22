// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file TransportInfoContainer.cpp
 * @brief Implementation of Transport Info Container components
 */

#include "TransportInfoContainer.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace Nomad {

// Define a constant for the text baseline adjustment factor, as text rendering
// APIs often place y at the baseline, not the top of the glyph.
// This value (0.8f) is critical for vertical centering the text based on the 
// engine's font rendering.
const float TEXT_BASELINE_COMPENSATION_FACTOR = 0.8f;

// ============================================================================
// BPM Display Component
// ============================================================================

BPMDisplay::BPMDisplay()
    : NomadUI::NUIComponent()
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
    m_upArrow = std::make_shared<NomadUI::NUIIcon>(upArrowSvg);
    m_upArrow->setIconSize(NomadUI::NUIIconSize::Small);
    m_upArrow->setColorFromTheme("textSecondary"); 	// #9a9aa3 - Inactive by default
    
    // Create down arrow icon (small triangle pointing down)
    const char* downArrowSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M7 10l5 5 5-5z"/>
        </svg>
    )";
    m_downArrow = std::make_shared<NomadUI::NUIIcon>(downArrowSvg);
    m_downArrow->setIconSize(NomadUI::NUIIconSize::Small);
    m_downArrow->setColorFromTheme("textSecondary"); 	// #9a9aa3 - Inactive by default
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

NomadUI::NUIRect BPMDisplay::getUpArrowBounds() const {
    NomadUI::NUIRect bounds = getBounds();
    float arrowSize = 16.0f;
    float spacing = 6.0f;
    
    // Position arrows at the right edge of the component, vertically stacked
    float x = bounds.x + bounds.width - arrowSize - 5.0f; // 5px from right edge
    float totalArrowHeight = arrowSize * 2 + spacing; // Both arrows + gap
    float y = bounds.y + (bounds.height - totalArrowHeight) / 2.0f; // Vertically centered as a group
    
    return NomadUI::NUIRect(x, y, arrowSize, arrowSize);
}

NomadUI::NUIRect BPMDisplay::getDownArrowBounds() const {
    NomadUI::NUIRect bounds = getBounds();
    float arrowSize = 16.0f;
    float spacing = 6.0f;
    // Position down arrow below up arrow
    NomadUI::NUIRect upBounds = getUpArrowBounds();
    return NomadUI::NUIRect(upBounds.x, upBounds.y + arrowSize + spacing, arrowSize, arrowSize);
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
    
    NomadUI::NUIComponent::onUpdate(deltaTime);
}

void BPMDisplay::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    
    // Get theme for rendering
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // Get colors
    NomadUI::NUIColor bgColor = themeManager.getColor("surfaceTertiary").withAlpha(0.5f); // Glass!
    NomadUI::NUIColor borderColor = themeManager.getColor("glassBorder");
    NomadUI::NUIColor accentColor = themeManager.getColor("accent");
    NomadUI::NUIColor textPrimary = themeManager.getColor("textPrimary");
    NomadUI::NUIColor textSecondary = themeManager.getColor("textSecondary");
    
    const float radius = themeManager.getRadius("m");
    
    // Hover glow effect - subtle accent border when hovered
    if (m_isHovered) {
        // Draw outer glow
        NomadUI::NUIRect glowBounds = bounds;
        glowBounds.x -= 1.0f;
        glowBounds.y -= 1.0f;
        glowBounds.width += 2.0f;
        glowBounds.height += 2.0f;
        NomadUI::NUIColor glowColor = accentColor;
        glowColor.a = 0.3f;
        renderer.strokeRoundedRect(glowBounds, radius + 1.0f, 2.0f, glowColor);
    }
    
    // Draw Dark Pill Background
    renderer.fillRoundedRect(bounds, radius, bgColor);
    
    // Border: accent when hovered, normal otherwise
    NomadUI::NUIColor currentBorder = m_isHovered ? accentColor : borderColor;
    if (m_isHovered) currentBorder.a = 0.6f;
    renderer.strokeRoundedRect(bounds, radius, 1.0f, currentBorder);
    
    // Pulse effect - flash accent color when BPM changes
    if (m_pulseAnimation > 0.0f) {
        NomadUI::NUIColor pulseColor = accentColor;
        pulseColor.a = m_pulseAnimation * 0.4f;
        renderer.fillRoundedRect(bounds, radius, pulseColor);
    }
    
    // Text color: pulse accent briefly when changing
    NomadUI::NUIColor textColor = textPrimary;
    if (m_pulseAnimation > 0.5f) {
        textColor = accentColor;
    }
    
    float fontSize = themeManager.getFontSize("l");
    
    // Calculate text position - offset left to make room for arrows
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << m_displayBPM << " BPM";
    std::string bpmText = ss.str();
    
    NomadUI::NUISize textSize = renderer.measureText(bpmText, fontSize);
    float textY = std::round(renderer.calculateTextY(bounds, fontSize));
    // Offset text left to make room for arrows on right
    float textX = std::round(bounds.x + (bounds.width - textSize.width - 20.0f) * 0.5f);
    
    // Draw text
    renderer.drawText(bpmText, NomadUI::NUIPoint(textX, textY), fontSize, textColor);
    
    // Draw arrow buttons
    NomadUI::NUIRect upBounds = getUpArrowBounds();
    NomadUI::NUIRect downBounds = getDownArrowBounds();
    
    // Up arrow color based on state
    NomadUI::NUIColor upColor = textSecondary;
    if (m_upArrowPressed) {
        upColor = accentColor;
    } else if (m_upArrowHovered) {
        upColor = textPrimary;
    }
    
    // Down arrow color based on state
    NomadUI::NUIColor downColor = textSecondary;
    if (m_downArrowPressed) {
        downColor = accentColor;
    } else if (m_downArrowHovered) {
        downColor = textPrimary;
    }
    
    // Draw up arrow using NUIIcon
    if (m_upArrow) {
        m_upArrow->setBounds(upBounds);
        m_upArrow->setColor(upColor);
        m_upArrow->onRender(renderer);
    }
    
    // Draw down arrow using NUIIcon
    if (m_downArrow) {
        m_downArrow->setBounds(downBounds);
        m_downArrow->setColor(downColor);
        m_downArrow->onRender(renderer);
    }
}

bool BPMDisplay::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    NomadUI::NUIRect bounds = getBounds();
    NomadUI::NUIRect upBounds = getUpArrowBounds();
    NomadUI::NUIRect downBounds = getDownArrowBounds();
    
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
        if (event.modifiers & NomadUI::NUIModifiers::Shift) {
            increment = 5.0f;
        } else if (event.modifiers & NomadUI::NUIModifiers::Ctrl) {
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
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
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
    if (event.released && event.button == NomadUI::NUIMouseButton::Left) {
        m_upArrowPressed = false;
        m_downArrowPressed = false;
    }
    
    // Capture hover changes for redraw
    if (wasHovered != m_isHovered || inUp || inDown) {
        return true; // Consume event to trigger redraw
    }
    
    return NomadUI::NUIComponent::onMouseEvent(event);
}

// ============================================================================
// Timer Display Component
// ============================================================================

TimerDisplay::TimerDisplay()
    : NomadUI::NUIComponent()
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
    ss << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setw(2) << secs << "."
        << std::setw(2) << millis;
    return ss.str();
}

void TimerDisplay::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    
    // Get theme for rendering
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // Draw Dark Pill Background (Glass)
    const float radius = themeManager.getRadius("m");
    auto glassColor = themeManager.getColor("surfaceTertiary").withAlpha(0.5f);
    renderer.fillRoundedRect(bounds, radius, glassColor);
    renderer.strokeRoundedRect(bounds, radius, 1.0f, themeManager.getColor("glassBorder"));
    
    // CRITICAL: Green when playing, white when stopped
    NomadUI::NUIColor textColor = m_isPlaying 
        ? themeManager.getColor("success") 	// Vibrant green when playing
        : themeManager.getColor("textPrimary"); 	 	// White when stopped
    
    float fontSize = themeManager.getFontSize("l");
    std::string timeText = formatTime(m_currentTime);
    
    // Perfectly center text in the pill
    NomadUI::NUISize textSize = renderer.measureText(timeText, fontSize);
    float textY = std::round(renderer.calculateTextY(bounds, fontSize));
    float textX = std::round(bounds.x + (bounds.width - textSize.width) * 0.5f);
    
    renderer.drawText(timeText, NomadUI::NUIPoint(textX, textY), fontSize, textColor);
}

// ============================================================================
// Time Signature Display Component
// ============================================================================

TimeSignatureDisplay::TimeSignatureDisplay()
    : NomadUI::NUIComponent()
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

void TimeSignatureDisplay::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    NomadUI::NUIColor textColor = themeManager.getColor("textPrimary");
    NomadUI::NUIColor accentCyan = themeManager.getColor("accentCyan");
    NomadUI::NUIColor bgColor = themeManager.getColor("backgroundSecondary");
    
    // Subtle background when hovered
    if (m_isHovered) {
        renderer.fillRoundedRect(bounds, 4.0f, bgColor.withAlpha(0.5f));
    }
    
    // Draw time signature text - centered
    std::string text = getDisplayText();
    float fontSize = 14.0f;
    NomadUI::NUISize textSize = renderer.measureText(text, fontSize);
    float textX = std::round(bounds.x + (bounds.width - textSize.width) * 0.5f);
    float textY = std::round(renderer.calculateTextY(bounds, fontSize));
    
    NomadUI::NUIColor displayColor = m_isHovered ? accentCyan : textColor;
    renderer.drawText(text, NomadUI::NUIPoint(textX, textY), fontSize, displayColor);
}

bool TimeSignatureDisplay::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    NomadUI::NUIRect bounds = getBounds();
    bool inside = bounds.contains(event.position);
    
    // Track hover state
    bool wasHovered = m_isHovered;
    m_isHovered = inside;
    
    // Handle click to cycle time signature
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        if (inside) {
            cycleNext();
            return true;
        }
    }
    
    // Capture hover changes for redraw
    if (wasHovered != m_isHovered) {
        setDirty(true);
        return true;
    }
    
    return false;
}

// ============================================================================
// Transport Info Container
// ============================================================================

TransportInfoContainer::TransportInfoContainer()
    : NomadUI::NUIComponent()
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
    NomadUI::NUIRect bounds = getBounds();
    
    // Get layout dimensions from theme
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Timer on the left - position using ABSOLUTE coordinates (bounds.x + offset)
    float timerWidth = 120.0f;
    float timerHeight = 30.0f;
    float timerOffsetX = layout.transportButtonSize * 3 + layout.transportButtonSpacing * 4 + layout.panelMargin;
    float timerOffsetY = (bounds.height - timerHeight) / 2.0f; // Vertically centered in container
    
    // Use absolute positioning (add bounds.x and bounds.y)
    m_timerDisplay->setBounds(NomadUI::NUIRect(bounds.x + timerOffsetX, bounds.y + timerOffsetY, timerWidth, timerHeight));
    
    // BPM in the center (horizontally centered in transport bar) - also ABSOLUTE
    float bpmWidth = 100.0f;
    float bpmHeight = 30.0f; 	// Increased from 24 to match timer height for better vertical centering
    float bpmOffsetX = (bounds.width - bpmWidth) / 2.0f;
    float bpmOffsetY = (bounds.height - bpmHeight) / 2.0f; // Vertically centered in container
    
    // Use absolute positioning (add bounds.x and bounds.y)
    m_bpmDisplay->setBounds(NomadUI::NUIRect(bounds.x + bpmOffsetX, bounds.y + bpmOffsetY, bpmWidth, bpmHeight));
    
    // Time signature to the left of BPM display (between metronome button and BPM)
    float timeSigWidth = 40.0f;
    float timeSigHeight = 24.0f;
    float timeSigOffsetX = bpmOffsetX - timeSigWidth - 10.0f;  // 10px gap from BPM
    float timeSigOffsetY = (bounds.height - timeSigHeight) / 2.0f;
    
    m_timeSignatureDisplay->setBounds(NomadUI::NUIRect(bounds.x + timeSigOffsetX, bounds.y + timeSigOffsetY, timeSigWidth, timeSigHeight));
}

void TransportInfoContainer::onRender(NomadUI::NUIRenderer& renderer) {
    // No background rendering - just render children
    renderChildren(renderer);
}

void TransportInfoContainer::onResize(int width, int height) {
    NomadUI::NUIRect currentBounds = getBounds();
    setBounds(NomadUI::NUIRect(currentBounds.x, currentBounds.y, width, height));
    layoutComponents();
    NomadUI::NUIComponent::onResize(width, height);
}

bool TransportInfoContainer::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
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
    
    return NomadUI::NUIComponent::onMouseEvent(event);
}

} // namespace Nomad
