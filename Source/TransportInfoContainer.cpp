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
{
    m_label = std::make_shared<NomadUI::NUILabel>();
    m_label->setAlignment(NomadUI::NUILabel::Alignment::Center);
    addChild(m_label);
    
    // Initial text
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << m_displayBPM << " BPM";
    m_label->setText(ss.str());
}

void BPMDisplay::setBPM(float bpm) {
    m_targetBPM = std::max(20.0f, std::min(999.0f, bpm));
    m_currentBPM = m_targetBPM;
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
    // Position label to fill bounds
    m_label->setBounds(getBounds());
    
    renderChildren(renderer);
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
