// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIItemSelector.h"
#include "../Core/NUIThemeSystem.h"
#include <cmath>
#include <algorithm>
#include <chrono>

namespace NomadUI {

UIItemSelector::UIItemSelector()
    : NUIComponent()
    , m_currentIndex(-1)
    , m_isHovered(false)
    , m_upArrowHovered(false)
    , m_downArrowHovered(false)
    , m_upArrowPressed(false)
    , m_downArrowPressed(false)
    , m_pulseAnimation(0.0f)
    , m_holdTimer(0.0f)
    , m_holdDelay(0.0f)
{
    // Create up arrow icon
    const char* upArrowSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M7 14l5-5 5 5z"/>
        </svg>
    )";
    m_upArrow = std::make_shared<NUIIcon>(upArrowSvg);
    m_upArrow->setIconSize(NUIIconSize::Small);
    m_upArrow->setColorFromTheme("textSecondary");
    
    // Create down arrow icon
    const char* downArrowSvg = R"(
        <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M7 10l5 5 5-5z"/>
        </svg>
    )";
    m_downArrow = std::make_shared<NUIIcon>(downArrowSvg);
    m_downArrow->setIconSize(NUIIconSize::Small);
    m_downArrow->setColorFromTheme("textSecondary");
    
    // Initialize editing
    m_lastClickTime = 0.0;
    m_isEditing = false;
    
    m_textInput = std::make_shared<NUITextInput>();
    m_textInput->setVisible(false);
    m_textInput->setJustification(NUITextInput::Justification::Center);
    m_textInput->setBorderWidth(0.0f); // Seamless
    m_textInput->setBackgroundColor(NUIColor::fromHex(0xff2a2d32)); // Dark bg
    
    m_textInput->setOnReturnKey([this]() { commitEditing(); });
    m_textInput->setOnEscapeKey([this]() { cancelEditing(); });
    m_textInput->setOnFocusLost([this]() { 
        // Delay commit slightly or just commit? Usually commit on blur is good.
        commitEditing(); 
    });
    
    addChild(m_textInput);
}

void UIItemSelector::onResize(int width, int height) {
    if (m_textInput) {
        // Position text input over the text area (excluding arrows)
        NUIRect bounds(0, 0, width, height);
        float arrowSpace = 25.0f; // Matching onRender logic
        m_textInput->setBounds(NUIRect(bounds.x, bounds.y, bounds.width - arrowSpace, bounds.height));
    }
    NUIComponent::onResize(width, height);
}

void UIItemSelector::startEditing() {
    if (m_isEditing) return;
    m_isEditing = true;
    m_textInput->setText(getSelectedItem());
    m_textInput->setVisible(true);
    m_textInput->onFocusGained(); // Force focus
    // Select all for quick replacement
    m_textInput->selectAll();
    setDirty(true);
}

void UIItemSelector::commitEditing() {
    if (!m_isEditing) return;
    
    std::string text = m_textInput->getText();
    
    // ROBUST LOOKUP LOGIC
    int matchIndex = -1;
    
    // 1. Exact/Case-Insensitive Match
    std::string lowerText = text;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
    
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        std::string item = m_items[i];
        std::transform(item.begin(), item.end(), item.begin(), ::tolower);
        if (item == lowerText) {
            matchIndex = i;
            break;
        }
    }
    
    // 2. "Track X" number parsing
    if (matchIndex == -1) {
        try {
            // Check if user just typed a number "7"
            int trackNum = std::stoi(text);
            
            // Try to find "Track NUM" or "Track NUM *"
            std::string prefix = "track " + std::to_string(trackNum);
             for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
                std::string item = m_items[i];
                std::transform(item.begin(), item.end(), item.begin(), ::tolower);
                if (item.find(prefix) == 0) { // Starts with "track N"
                    matchIndex = i;
                    break;
                }
            }
        } catch (...) {
            // Not a number
        }
    }
    
    // 3. Partial substring match (last resort)
    if (matchIndex == -1 && text.length() > 1) {
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            std::string item = m_items[i];
            std::transform(item.begin(), item.end(), item.begin(), ::tolower);
            if (item.find(lowerText) != std::string::npos) {
                matchIndex = i;
                break;
            }
        }
    }
    
    if (matchIndex != -1) {
        setSelectedIndex(matchIndex);
        if (m_onSelectionChanged) m_onSelectionChanged(m_currentIndex);
    } else {
        // Error visual feedback? Flash red?
        // For now just revert.
    }
    
    m_isEditing = false;
    m_textInput->setVisible(false);
    setDirty(true);
}

void UIItemSelector::cancelEditing() {
    if (!m_isEditing) return;
    m_isEditing = false;
    m_textInput->setVisible(false);
    setDirty(true);
}


void UIItemSelector::setItems(const std::vector<std::string>& items) {
    m_items = items;
    if (m_currentIndex >= static_cast<int>(m_items.size())) {
        m_currentIndex = -1;
    }
    setDirty(true);
}

void UIItemSelector::setSelectedIndex(int index) {
    if (index >= -1 && index < static_cast<int>(m_items.size())) {
        if (m_currentIndex != index) {
            m_currentIndex = index;
            m_pulseAnimation = 1.0f;
            setDirty(true);
        }
    }
}

std::string UIItemSelector::getSelectedItem() const {
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_items.size())) {
        return m_items[m_currentIndex];
    }
    return "";
}

void UIItemSelector::selectNext() {
    if (m_items.empty()) return;
    int next = m_currentIndex + 1;
    if (next >= static_cast<int>(m_items.size())) next = 0; // Wrap around? Or clamp? Let's wrap for speed.
    if (m_currentIndex != next) {
        m_currentIndex = next;
        m_pulseAnimation = 1.0f;
        if (m_onSelectionChanged) m_onSelectionChanged(m_currentIndex);
        setDirty(true);
    }
}

void UIItemSelector::selectPrevious() {
    if (m_items.empty()) return;
    int prev = m_currentIndex - 1;
    if (prev < 0) prev = static_cast<int>(m_items.size()) - 1; // Wrap around
    if (m_currentIndex != prev) {
        m_currentIndex = prev;
        m_pulseAnimation = 1.0f;
        if (m_onSelectionChanged) m_onSelectionChanged(m_currentIndex);
        setDirty(true);
    }
}

NUIRect UIItemSelector::getUpArrowBounds() const {
    NUIRect bounds = getBounds();
    float arrowSize = 16.0f;
    float spacing = 2.0f; // Tighter vertical spacing for compact sends
    
    // Position arrows at the right edge
    float x = bounds.x + bounds.width - arrowSize - 5.0f; 
    float totalArrowHeight = arrowSize * 2 + spacing;
    float y = bounds.y + (bounds.height - totalArrowHeight) / 2.0f;
    
    return NUIRect(x, y, arrowSize, arrowSize);
}

NUIRect UIItemSelector::getDownArrowBounds() const {
    NUIRect bounds = getBounds();
    float arrowSize = 16.0f;
    float spacing = 2.0f;
    NUIRect upBounds = getUpArrowBounds();
    return NUIRect(upBounds.x, upBounds.y + arrowSize + spacing, arrowSize, arrowSize);
}

void UIItemSelector::onUpdate(double deltaTime) {
    // Pulse decay
    if (m_pulseAnimation > 0.0f) {
        m_pulseAnimation -= static_cast<float>(deltaTime) * 4.0f;
        if (m_pulseAnimation < 0.0f) m_pulseAnimation = 0.0f;
        setDirty(true);
    }
    
    // Hold-to-repeat
    if (m_upArrowPressed || m_downArrowPressed) {
        m_holdDelay -= static_cast<float>(deltaTime);
        if (m_holdDelay <= 0.0f) {
            m_holdTimer += static_cast<float>(deltaTime);
            if (m_holdTimer >= 0.05f) { // 50ms repeat
                m_holdTimer = 0.0f;
                if (m_upArrowPressed) selectNext(); // Up arrow usually means "next" (down the list) or "increment"
                // Actually in a list, "Up" arrow typically moves SELECTION UP (previous index).
                // But specifically for this UI, Up arrow usually increments index?
                // BPM: Up arrow -> Increment BPM.
                // List: If I click Up arrow, do I want Item 1 -> Item 2?
                // Standard spinners: Up arrow increments value.
                // Let's make Up Arrow -> Next Item (Index++)
                else selectPrevious();
            }
        }
    }
    
    NUIComponent::onUpdate(deltaTime);
}

void UIItemSelector::onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    auto& themeManager = NUIThemeManager::getInstance();
    
    // Colors
    NUIColor bgColor = themeManager.getColor("surfaceTertiary").withAlpha(0.3f); 
    NUIColor borderColor = themeManager.getColor("glassBorder");
    NUIColor accentColor = themeManager.getColor("accentPrimary");
    NUIColor textPrimary = themeManager.getColor("textPrimary");
    NUIColor textSecondary = themeManager.getColor("textSecondary");
    
    const float radius = 4.0f; // Slightly more boxy than BPM pill? Or same? Let's use 4.0f
    
    // Hover glow
    if (m_isHovered) {
        NUIRect glowBounds = bounds;
        glowBounds.x -= 1.0f; glowBounds.y -= 1.0f;
        glowBounds.width += 2.0f; glowBounds.height += 2.0f;
        NUIColor glowColor = accentColor;
        glowColor.a = 0.2f;
        renderer.strokeRoundedRect(glowBounds, radius + 1.0f, 2.0f, glowColor);
    }
    
    // Background
    renderer.fillRoundedRect(bounds, radius, bgColor);
    
    // Border
    NUIColor currentBorder = m_isHovered ? accentColor : borderColor;
    if (m_isHovered) currentBorder.a = 0.5f;
    renderer.strokeRoundedRect(bounds, radius, 1.0f, currentBorder);
    
    // Pulse
    if (m_pulseAnimation > 0.0f) {
        NUIColor pulseColor = accentColor;
        pulseColor.a = m_pulseAnimation * 0.2f;
        renderer.fillRoundedRect(bounds, radius, pulseColor);
    }
    
    // Text
    NUIColor textColor = textPrimary;
    if (m_pulseAnimation > 0.5f) textColor = accentColor;
    
    std::string text = getSelectedItem();
    if (text.empty()) text = "None"; // Or "Select..."
    
    // Truncate if too long (simple char limit for now)
    if (text.length() > 20) text = text.substr(0, 17) + "...";
    
    float fontSize = 12.0f; // Compact
    
    // Calculate layout
    NUISize textSize = renderer.measureText(text, fontSize);
    float textY = std::round(renderer.calculateTextY(bounds, fontSize));
    
    // Center text in available space (bounds width - arrow space)
    float arrowSpace = 25.0f;
    float contentWidth = bounds.width - arrowSpace;
    float textX = std::round(bounds.x + (contentWidth - textSize.width) * 0.5f);
    
    // Clip text rect?
    renderer.drawText(text, NUIPoint(textX, textY), fontSize, textColor);
    
    // Arrows
    NUIRect upBounds = getUpArrowBounds();
    NUIRect downBounds = getDownArrowBounds();
    
    NUIColor upColor = textSecondary;
    if (m_upArrowPressed) upColor = accentColor;
    else if (m_upArrowHovered) upColor = textPrimary;
    
    NUIColor downColor = textSecondary;
    if (m_downArrowPressed) downColor = accentColor;
    else if (m_downArrowHovered) downColor = textPrimary;
    
    if (m_upArrow) {
        m_upArrow->setBounds(upBounds);
        m_upArrow->setColor(upColor);
        m_upArrow->onRender(renderer);
    }
    
    if (m_downArrow) {
        m_downArrow->setBounds(downBounds);
        m_downArrow->setColor(downColor);
        m_downArrow->onRender(renderer);
    }
}

bool UIItemSelector::onMouseEvent(const NUIMouseEvent& event) {
    NUIRect bounds = getBounds();
    NUIRect upBounds = getUpArrowBounds();
    NUIRect downBounds = getDownArrowBounds();
    
    bool inBounds = bounds.contains(event.position);
    bool inUp = upBounds.contains(event.position);
    bool inDown = downBounds.contains(event.position);
    
    bool wasHovered = m_isHovered;
    m_isHovered = inBounds;
    m_upArrowHovered = inUp;
    m_downArrowHovered = inDown;
    
    // Mouse Wheel
    if (event.wheelDelta != 0.0f && inBounds) {
        if (event.wheelDelta > 0) selectNext();
        else selectPrevious();
        return true;
    }
    
    // Click
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (inUp) {
            m_upArrowPressed = true;
            m_holdDelay = 0.3f;
            m_holdTimer = 0.0f;
            selectNext();
            return true;
        }
        if (inDown) {
            m_downArrowPressed = true;
            m_holdDelay = 0.3f;
            m_holdTimer = 0.0f;
            selectPrevious();
            return true;
        }
        
        // Double Click Detection
        // Use a simple time check provided by chrono or assume event might have click count
        // Since we don't have access to global time easily here, we use a static clock or rely on event
        // Assuming event.clickCount from typical UI frameworks, or manual timing using std::chrono.
        // Let's use std::chrono for robust "Double Click"
        static auto lastClick = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClick).count();
        
        // Threshold 400ms
        if (diff < 400 && inBounds && !inUp && !inDown) {
             startEditing();
             return true;
        }
        lastClick = now;
    }
    
    if (event.released && event.button == NUIMouseButton::Left) {
        m_upArrowPressed = false;
        m_downArrowPressed = false;
    }
    
    if (wasHovered != m_isHovered || inUp || inDown) return true;
    
    // Pass event to text input if visible (though component system should handle this if child is active)
    if (m_isEditing && m_textInput && m_textInput->isVisible()) {
        if (m_textInput->onMouseEvent(event)) return true;
    }
    
    return NUIComponent::onMouseEvent(event);
}

} // namespace NomadUI
