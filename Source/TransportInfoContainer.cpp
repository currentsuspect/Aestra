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
    const float animSpeed = 5.0f; // Adjust for faster/slower animation
    float diff = m_targetBPM - m_displayBPM;
    
    if (std::abs(diff) > 0.01f) {
        m_displayBPM += diff * animSpeed * static_cast<float>(deltaTime);
    } else {
        m_displayBPM = m_targetBPM;
    }
    
    NomadUI::NUIComponent::onUpdate(deltaTime);
}

void BPMDisplay::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    
    // Get theme for text rendering
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    NomadUI::NUIColor textColor = themeManager.getColor("textPrimary");
    float fontSize = themeManager.getFontSize("l"); // use larger theme size
    
    // Calculate text position with vertical centering (like dropdown does it)
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << m_displayBPM << " BPM";
    std::string bpmText = ss.str();
    
    // ALIGNMENT: use font metrics (drawText expects top-left Y)
    NomadUI::NUISize textSize = renderer.measureText(bpmText, fontSize);
    float textY = std::round(renderer.calculateTextY(bounds, fontSize));
    float textX = std::round(bounds.x + (bounds.width - textSize.width) * 0.5f);
    
    // Draw text directly (not using label)
    renderer.drawText(bpmText, NomadUI::NUIPoint(textX, textY), fontSize, textColor);
    
    // Arrow affordances removed: BPM is adjusted via mouse wheel.
}

bool BPMDisplay::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    // Handle mouse wheel for fine adjustment (anywhere on BPM display)
    if (event.wheelDelta != 0.0f) {
        NomadUI::NUIRect bounds = getBounds();
        if (bounds.contains(event.position)) {
            if (event.wheelDelta > 0) {
                incrementBPM(0.5f); 	// +0.5 BPM per scroll up
            } else {
                decrementBPM(0.5f); 	// -0.5 BPM per scroll down
            }
            return true;
        }
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
    
    // Get theme for text rendering
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // CRITICAL: Green when playing, white when stopped
    NomadUI::NUIColor textColor = m_isPlaying 
        ? themeManager.getColor("success") 	// Vibrant green when playing
        : themeManager.getColor("textPrimary"); 	 	// White when stopped
    
    float fontSize = themeManager.getFontSize("l"); // use larger theme size
    
    // Calculate text position with vertical centering (like dropdown does it)
    std::string timeText = formatTime(m_currentTime);
    
    // ALIGNMENT: use font metrics (drawText expects top-left Y)
    NomadUI::NUISize textSize = renderer.measureText(timeText, fontSize);
    float textY = std::round(renderer.calculateTextY(bounds, fontSize));
    float textX = std::round(bounds.x + 5.0f); // Small left padding
    
    // Draw text directly (not using label)
    renderer.drawText(timeText, NomadUI::NUIPoint(textX, textY), fontSize, textColor);
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

} // namespace Nomad
