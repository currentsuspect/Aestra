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
    
    // Get theme for rendering
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    
    // Get colors
    AestraUI::NUIColor bgColor = themeManager.getColor("surfaceTertiary").withAlpha(0.5f); // Semi-glass
    AestraUI::NUIColor borderColor = themeManager.getColor("glassBorder");
    AestraUI::NUIColor accentColor = themeManager.getColor("accentPrimary"); // Aestra Purple
    AestraUI::NUIColor textPrimary = themeManager.getColor("textPrimary");
    AestraUI::NUIColor textSecondary = themeManager.getColor("textSecondary");
    
    const float radius = themeManager.getRadius("m");
    
    // Hover glow effect
    if (m_isHovered) {
        AestraUI::NUIRect glowBounds = bounds;
        glowBounds.x -= 1.0f;
        glowBounds.y -= 1.0f;
        glowBounds.width += 2.0f;
        glowBounds.height += 2.0f;
        AestraUI::NUIColor glowColor = accentColor;
        glowColor.a = 0.3f;
        renderer.strokeRoundedRect(glowBounds, radius + 1.0f, 2.0f, glowColor);
    }
    
    // Draw Dark Pill Background
    renderer.fillRoundedRect(bounds, radius, bgColor);
    
    // Border
    AestraUI::NUIColor currentBorder = m_isHovered ? accentColor : borderColor;
    if (m_isHovered) currentBorder.a = 0.6f;
    renderer.strokeRoundedRect(bounds, radius, 1.0f, currentBorder);
    
    // Pulse effect
    if (m_pulseAnimation > 0.0f) {
        AestraUI::NUIColor pulseColor = accentColor;
        pulseColor.a = m_pulseAnimation * 0.4f;
        renderer.fillRoundedRect(bounds, radius, pulseColor);
    }
    
    // Text color
    AestraUI::NUIColor textColor = textPrimary;
    if (m_pulseAnimation > 0.5f) {
        textColor = accentColor;
    }
    
    float fontSize = themeManager.getFontSize("l");
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << m_displayBPM << " BPM";
    std::string bpmText = ss.str();
    
    // === CENTERED CLUSTER LAYOUT ===
    AestraUI::NUISize textSize = renderer.measureText(bpmText, fontSize);
    
    float arrowSize = 12.0f;  // Balanced size (not too small, fits container)
    float arrowGap = 1.0f;    // Tight spacing between arrows
    float textArrowGap = 8.0f; // Comfortable gap between text and arrows
    float horizontalPadding = 6.0f; // Internal padding from edges
    
    float arrowBlockWidth = arrowSize;
    float arrowBlockHeight = (arrowSize * 2) + arrowGap;
    
    float totalContentWidth = textSize.width + textArrowGap + arrowBlockWidth;
    
    // Calculate available width with padding
    float availableWidth = bounds.width - (horizontalPadding * 2);
    
    // Center the entire cluster (Text + Arrows) within the padded area
    float startX = bounds.x + horizontalPadding + (availableWidth - totalContentWidth) * 0.5f;
    float textX = std::round(startX);
    float textY = std::round(renderer.calculateTextY(bounds, fontSize));
    
    float arrowX = std::round(startX + textSize.width + textArrowGap);
    float arrowY = bounds.y + (bounds.height - arrowBlockHeight) * 0.5f;
    
    // Update Cached Bounds for Hit Testing
    m_cachedUpArrowBounds = AestraUI::NUIRect(arrowX, arrowY, arrowSize, arrowSize);
    m_cachedDownArrowBounds = AestraUI::NUIRect(arrowX, arrowY + arrowSize + arrowGap, arrowSize, arrowSize);
    
    // Draw text
    renderer.drawText(bpmText, AestraUI::NUIPoint(textX, textY), fontSize, textColor);
    
    // Draw arrow buttons
    
    // Up arrow color
    AestraUI::NUIColor upColor = textSecondary;
    if (m_upArrowPressed) upColor = accentColor;
    else if (m_upArrowHovered) upColor = textPrimary;
    
    // Down arrow color
    AestraUI::NUIColor downColor = textSecondary;
    if (m_downArrowPressed) downColor = accentColor;
    else if (m_downArrowHovered) downColor = textPrimary;
    
    // Draw up arrow
    if (m_upArrow) {
        m_upArrow->setBounds(m_cachedUpArrowBounds);
        m_upArrow->setColor(upColor);
        m_upArrow->onRender(renderer);
    }
    
    // Draw down arrow
    if (m_downArrow) {
        m_downArrow->setBounds(m_cachedDownArrowBounds);
        m_downArrow->setColor(downColor);
        m_downArrow->onRender(renderer);
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
    ss << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setw(2) << secs << "."
        << std::setw(2) << millis;
    return ss.str();
}

void TimerDisplay::onRender(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    
    // Get theme for rendering
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    
    // Draw Dark Pill Background (Glass)
    const float radius = themeManager.getRadius("m");
    auto glassColor = themeManager.getColor("surfaceTertiary").withAlpha(0.5f);
    renderer.fillRoundedRect(bounds, radius, glassColor);
    renderer.strokeRoundedRect(bounds, radius, 1.0f, themeManager.getColor("glassBorder"));
    
    // CRITICAL: Green when playing, white when stopped
    AestraUI::NUIColor textColor = m_isPlaying 
        ? themeManager.getColor("accentPrimary") 	// Vibrant purple when playing
        : themeManager.getColor("textPrimary"); 	 	// White when stopped
    
    float fontSize = themeManager.getFontSize("l");
    std::string timeText = formatTime(m_currentTime);
    
    // Perfectly center text in the pill
    AestraUI::NUISize textSize = renderer.measureText(timeText, fontSize);
    float textY = std::round(renderer.calculateTextY(bounds, fontSize));
    float textX = std::round(bounds.x + (bounds.width - textSize.width) * 0.5f);
    
    renderer.drawText(timeText, AestraUI::NUIPoint(textX, textY), fontSize, textColor);
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
    
    // Consistent Glass styling
    const float radius = themeManager.getRadius("m");
    AestraUI::NUIColor bgColor = themeManager.getColor("surfaceTertiary").withAlpha(0.5f);
    AestraUI::NUIColor borderColor = themeManager.getColor("glassBorder");
    AestraUI::NUIColor accentColor = themeManager.getColor("accentPrimary");
    
    // Hover glow
    if (m_isHovered) {
        AestraUI::NUIRect glowBounds = bounds;
        glowBounds.x -= 1.0f;
        glowBounds.y -= 1.0f;
        glowBounds.width += 2.0f;
        glowBounds.height += 2.0f;
        AestraUI::NUIColor glowColor = accentColor;
        glowColor.a = 0.3f; // Glow opacity
        renderer.strokeRoundedRect(glowBounds, radius + 1.0f, 2.0f, glowColor);
    }
    
    // Background
    renderer.fillRoundedRect(bounds, radius, bgColor);
    
    // Border
    AestraUI::NUIColor currentBorder = m_isHovered ? accentColor : borderColor;
    if (m_isHovered) currentBorder.a = 0.6f;
    renderer.strokeRoundedRect(bounds, radius, 1.0f, currentBorder);
    
    // Text
    AestraUI::NUIColor textColor = m_isHovered ? accentColor : themeManager.getColor("textPrimary");
    
    float fontSize = themeManager.getFontSize("l");
    std::string text = getDisplayText();
    
    AestraUI::NUISize textSize = renderer.measureText(text, fontSize);
    float textY = std::round(renderer.calculateTextY(bounds, fontSize));
    float textX = std::round(bounds.x + (bounds.width - textSize.width) * 0.5f);
    
    renderer.drawText(text, AestraUI::NUIPoint(textX, textY), fontSize, textColor);
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
    
    // Get layout dimensions from theme
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Timer on the left - position using ABSOLUTE coordinates (bounds.x + offset)
    float timerWidth = 120.0f;
    float timerHeight = 30.0f;
    float timerOffsetX = layout.transportButtonSize * 3 + layout.transportButtonSpacing * 4 + layout.panelMargin;
    float timerOffsetY = (bounds.height - timerHeight) / 2.0f; // Vertically centered in container
    
    // Use absolute positioning (add bounds.x and bounds.y) - SNAP TO INTEGERS
    m_timerDisplay->setBounds(AestraUI::NUIRect(
        std::floor(bounds.x + timerOffsetX), 
        std::floor(bounds.y + timerOffsetY), 
        std::round(timerWidth), 
        std::round(timerHeight)
    ));
    
    // BPM in the center (horizontally centered in transport bar) - also ABSOLUTE
    float bpmWidth = 120.0f;  // Increased to fit content with padding
    float bpmHeight = 30.0f;
    float bpmOffsetX = (bounds.width - bpmWidth) / 2.0f;
    float bpmOffsetY = (bounds.height - bpmHeight) / 2.0f;
    
    m_bpmDisplay->setBounds(AestraUI::NUIRect(
        std::floor(bounds.x + bpmOffsetX), 
        std::floor(bounds.y + bpmOffsetY), 
        std::round(bpmWidth), 
        std::round(bpmHeight)
    ));
    
    // Time signature to the left of BPM display (between metronome button and BPM)
    float timeSigWidth = 50.0f;
    float timeSigHeight = 30.0f;
    float timeSigOffsetX = bpmOffsetX - timeSigWidth - 10.0f;
    float timeSigOffsetY = (bounds.height - timeSigHeight) / 2.0f;
    
    m_timeSignatureDisplay->setBounds(AestraUI::NUIRect(
        std::floor(bounds.x + timeSigOffsetX), 
        std::floor(bounds.y + timeSigOffsetY), 
        std::round(timeSigWidth), 
        std::round(timeSigHeight)
    ));
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
