/**
 * @file TransportInfoContainer.cpp
 * @brief Implementation of Transport Info Container components
 */

#include "TransportInfoContainer.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace Nomad {

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
    m_label = std::make_shared<NomadUI::NUILabel>();
    m_label->setAlignment(NomadUI::NUILabel::Alignment::Center);
    addChild(m_label);
    
    // Create up arrow icon (small triangle pointing up)
    const char* upArrowSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M7 14l5-5 5 5z"/>
        </svg>
    )";
    m_upArrow = std::make_shared<NomadUI::NUIIcon>(upArrowSvg);
    m_upArrow->setIconSize(NomadUI::NUIIconSize::Small);
    m_upArrow->setColorFromTheme("textSecondary");  // #9a9aa3 - Inactive by default
    
    // Create down arrow icon (small triangle pointing down)
    const char* downArrowSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M7 10l5 5 5-5z"/>
        </svg>
    )";
    m_downArrow = std::make_shared<NomadUI::NUIIcon>(downArrowSvg);
    m_downArrow->setIconSize(NomadUI::NUIIconSize::Small);
    m_downArrow->setColorFromTheme("textSecondary");  // #9a9aa3 - Inactive by default
    
    // Initial text
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << m_displayBPM << " BPM";
    m_label->setText(ss.str());
}

void BPMDisplay::setBPM(float bpm) {
    m_targetBPM = std::max(20.0f, std::min(999.0f, bpm));
    m_currentBPM = m_targetBPM;
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
    float spacing = 4.0f;
    // Position up arrow to the right of BPM text, vertically centered
    float x = bounds.x + bounds.width / 2.0f + 30.0f; // Offset from center
    float y = bounds.y + (bounds.height - arrowSize) / 2.0f - spacing / 2.0f;
    return NomadUI::NUIRect(x, y, arrowSize, arrowSize);
}

NomadUI::NUIRect BPMDisplay::getDownArrowBounds() const {
    NomadUI::NUIRect bounds = getBounds();
    float arrowSize = 16.0f;
    float spacing = 4.0f;
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
    
    // Update label text
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << m_displayBPM << " BPM";
    m_label->setText(ss.str());
    
    NomadUI::NUIComponent::onUpdate(deltaTime);
}

void BPMDisplay::onRender(NomadUI::NUIRenderer& renderer) {
    // Position label to fill bounds (centered)
    m_label->setBounds(getBounds());
    
    renderChildren(renderer);
    
    // Render arrow icons to the right of BPM text
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    if (m_upArrow) {
        m_upArrow->setBounds(getUpArrowBounds());
        // Highlight on hover
        if (m_upArrowHovered) {
            m_upArrow->setColorFromTheme("accentCyan");  // #00bcd4
        } else {
            m_upArrow->setColorFromTheme("textSecondary");  // #9a9aa3
        }
        m_upArrow->onRender(renderer);
    }
    
    if (m_downArrow) {
        m_downArrow->setBounds(getDownArrowBounds());
        // Highlight on hover
        if (m_downArrowHovered) {
            m_downArrow->setColorFromTheme("accentCyan");  // #00bcd4
        } else {
            m_downArrow->setColorFromTheme("textSecondary");  // #9a9aa3
        }
        m_downArrow->onRender(renderer);
    }
}

bool BPMDisplay::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    NomadUI::NUIRect upBounds = getUpArrowBounds();
    NomadUI::NUIRect downBounds = getDownArrowBounds();
    
    // Check hover states
    m_upArrowHovered = upBounds.contains(event.position);
    m_downArrowHovered = downBounds.contains(event.position);
    
    // Handle clicks
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        if (m_upArrowHovered) {
            incrementBPM(1.0f);  // +1 BPM per click
            return true;
        } else if (m_downArrowHovered) {
            decrementBPM(1.0f);  // -1 BPM per click
            return true;
        }
    }
    
    // Handle mouse wheel for fine adjustment (anywhere on BPM display)
    if (event.wheelDelta != 0.0f) {
        NomadUI::NUIRect bounds = getBounds();
        if (bounds.contains(event.position)) {
            if (event.wheelDelta > 0) {
                incrementBPM(0.5f);  // +0.5 BPM per scroll up
            } else {
                decrementBPM(0.5f);  // -0.5 BPM per scroll down
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
{
    m_label = std::make_shared<NomadUI::NUILabel>();
    m_label->setAlignment(NomadUI::NUILabel::Alignment::Center);
    addChild(m_label);
    
    m_label->setText(formatTime(0.0));
}

void TimerDisplay::setTime(double seconds) {
    m_currentTime = std::max(0.0, seconds);
    m_label->setText(formatTime(m_currentTime));
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
    // Position label to fill bounds
    m_label->setBounds(getBounds());
    
    renderChildren(renderer);
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
    
    // Timer on the left
    float timerWidth = 120.0f;
    float timerHeight = 30.0f;
    float timerX = layout.transportButtonSize * 3 + layout.transportButtonSpacing * 4 + layout.panelMargin;
    float timerY = (bounds.height - timerHeight) / 2.0f; // Vertically centered
    
    m_timerDisplay->setBounds(NomadUI::NUIRect(timerX, timerY, timerWidth, timerHeight));
    
    // BPM in the center (horizontally centered in transport bar)
    float bpmWidth = 100.0f;
    float bpmHeight = 24.0f;
    float bpmX = (bounds.width - bpmWidth) / 2.0f;
    float bpmY = (bounds.height - bpmHeight) / 2.0f; // Vertically centered
    
    m_bpmDisplay->setBounds(NomadUI::NUIRect(bpmX, bpmY, bpmWidth, bpmHeight));
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
