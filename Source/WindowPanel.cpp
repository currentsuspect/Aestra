// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#include "WindowPanel.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"

using namespace Nomad::Audio;

WindowPanel::WindowPanel(const std::string& title)
    : m_title(title)
{
    // Create close button (X)
    m_closeButton = std::make_shared<NomadUI::NUIButton>("×");
    m_closeButton->setOnClick([this]() {
        onCloseClicked();
    });
    addChild(m_closeButton);
    
    // Create maximize button (□ when normal, ▭ when maximized)
    m_maximizeButton = std::make_shared<NomadUI::NUIButton>("□");
    m_maximizeButton->setOnClick([this]() {
        onMaximizeClicked();
    });
    addChild(m_maximizeButton);
    
    // Create minimize button (−)
    m_minimizeButton = std::make_shared<NomadUI::NUIButton>("−");
    m_minimizeButton->setOnClick([this]() {
        onMinimizeClicked();
    });
    addChild(m_minimizeButton);
}

void WindowPanel::setContent(std::shared_ptr<NomadUI::NUIComponent> content) {
    // Remove old content if exists
    if (m_content) {
        removeChild(m_content);
    }
    
    m_content = content;
    
    if (m_content) {
        addChild(m_content);
        layoutContent();
    }
}

void WindowPanel::setMinimized(bool minimized) {
    if (m_minimized == minimized) return;
    
    m_minimized = minimized;
    
    if (m_minimized) {
        // Save current height before minimizing
        auto bounds = getBounds();
        m_expandedHeight = bounds.height;
        
        // Hide content when minimized
        if (m_content) {
            m_content->setVisible(false);
        }
        
        Log::info("WindowPanel '" + m_title + "' minimized (collapsed to title bar)");
    } else {
        // Show content when expanded
        if (m_content) {
            m_content->setVisible(true);
        }
        
        Log::info("WindowPanel '" + m_title + "' expanded");
    }
    
    // Update button text
    if (m_minimizeButton) {
        m_minimizeButton->setText(m_minimized ? "+" : "−"); // + for expand, − for minimize
    }
    
    // Notify parent to relayout
    if (m_onMinimizeToggle) {
        m_onMinimizeToggle(m_minimized);
    }
    
    layoutContent();
}

void WindowPanel::setMaximized(bool maximized) {
    if (m_maximized == maximized) return;
    
    // Clear minimized state when maximizing
    if (maximized && m_minimized) {
        setMinimized(false);
    }
    
    m_maximized = maximized;
    
    // Update maximize button icon
    if (m_maximizeButton) {
        m_maximizeButton->setText(m_maximized ? "▭" : "□"); // Different icon when maximized
    }
    
    // Notify parent to handle fullscreen layout
    if (m_onMaximizeToggle) {
        m_onMaximizeToggle(m_maximized);
    }
    
    Log::info("WindowPanel '" + m_title + (m_maximized ? "' maximized" : "' restored"));
}

void WindowPanel::toggleMinimize() {
    setMinimized(!m_minimized);
}

void WindowPanel::toggleMaximize() {
    setMaximized(!m_maximized);
}

void WindowPanel::setTitle(const std::string& title) {
    m_title = title;
}

void WindowPanel::onRender(NomadUI::NUIRenderer& renderer) {
    auto& theme = NomadUI::NUIThemeManager::getInstance();
    auto bounds = getBounds();
    
    // Draw title bar
    NomadUI::NUIRect titleBarRect(bounds.x, bounds.y, bounds.width, m_titleBarHeight);
    m_titleBarBounds = titleBarRect;
    
    // Title bar background (darker)
    auto titleBarColor = theme.getColor("backgroundDark");
    renderer.fillRect(titleBarRect, titleBarColor);
    
    // Title bar border
    auto borderColor = theme.getColor("border");
    renderer.strokeRect(titleBarRect, 1, borderColor);
    
    // Draw title text (centered vertically in title bar, top-left Y positioning)
    auto textColor = theme.getColor("text");
    float fontSize = 12.0f;
    float textX = bounds.x + 8.0f;
    float textY = bounds.y + (m_titleBarHeight - fontSize) * 0.5f;
    renderer.drawText(m_title, NomadUI::NUIPoint(textX, textY), fontSize, textColor);
    
    // Draw content background (if expanded)
    if (!m_minimized) {
        NomadUI::NUIRect contentBgRect(bounds.x, bounds.y + m_titleBarHeight, bounds.width, bounds.height - m_titleBarHeight);
        auto bgColor = theme.getColor("backgroundSecondary");
        renderer.fillRect(contentBgRect, bgColor);
        renderer.strokeRect(contentBgRect, 1, borderColor);
    }
    
    // Render children (content + buttons)
    renderChildren(renderer);
}

void WindowPanel::onResize(int width, int height) {
    layoutContent();
}

bool WindowPanel::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    // Let children handle events (buttons, etc.)
    return NomadUI::NUIComponent::onMouseEvent(event);
}

void WindowPanel::layoutContent() {
    auto bounds = getBounds();
    float buttonSize = m_titleBarHeight - 4.0f;
    float buttonPadding = 2.0f;
    float currentX = bounds.width - buttonSize - buttonPadding;
    
    // Layout buttons right-to-left: Close, Maximize, Minimize
    if (m_closeButton) {
        m_closeButton->setBounds(NUIAbsolute(bounds, currentX, buttonPadding, buttonSize, buttonSize));
        currentX -= buttonSize + buttonPadding;
    }
    
    if (m_maximizeButton) {
        m_maximizeButton->setBounds(NUIAbsolute(bounds, currentX, buttonPadding, buttonSize, buttonSize));
        currentX -= buttonSize + buttonPadding;
    }
    
    if (m_minimizeButton) {
        m_minimizeButton->setBounds(NUIAbsolute(bounds, currentX, buttonPadding, buttonSize, buttonSize));
    }
    
    // Layout content (below title bar)
    if (m_content && !m_minimized) {
        float contentY = m_titleBarHeight;
        float contentHeight = bounds.height - m_titleBarHeight;
        m_content->setBounds(NUIAbsolute(bounds, 0, contentY, bounds.width, contentHeight));
        
        // Trigger content's internal layout
        m_content->onResize(static_cast<int>(bounds.width), static_cast<int>(contentHeight));
    }
}

void WindowPanel::onMinimizeClicked() {
    toggleMinimize();
}

void WindowPanel::onMaximizeClicked() {
    toggleMaximize();
}

void WindowPanel::onCloseClicked() {
    if (m_onClose) {
        m_onClose();
    }
}
