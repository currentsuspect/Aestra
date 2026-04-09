// © 2025 Aestra Studios – All Rights Reserved. Licensed for personal & educational use only.
#include "WindowPanel.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraCore/include/AestraLog.h"

#include <algorithm>
#include <cmath>

using namespace Aestra::Audio;

WindowPanel::WindowPanel(const std::string& title)
    : m_title(title)
{
    const char* minimizeSvg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M6 12h12"/>
        </svg>
    )";
    const char* maximizeSvg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.0" stroke-linecap="round" stroke-linejoin="round">
            <rect x="6.5" y="6.5" width="11" height="11" rx="1.5"/>
        </svg>
    )";
    const char* restoreSvg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.0" stroke-linecap="round" stroke-linejoin="round">
            <rect x="8" y="8" width="10" height="10" rx="1.5"/>
            <path d="M6 14V7.5A1.5 1.5 0 0 1 7.5 6H14"/>
        </svg>
    )";
    const char* closeSvg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M7 7l10 10"/>
            <path d="M17 7L7 17"/>
        </svg>
    )";

    m_minimizeIcon = std::make_shared<AestraUI::NUIIcon>(minimizeSvg);
    m_minimizeIcon->setColor(AestraUI::NUIColor(0.92f, 0.92f, 0.96f, 0.9f));
    m_maximizeIcon = std::make_shared<AestraUI::NUIIcon>(maximizeSvg);
    m_maximizeIcon->setColor(AestraUI::NUIColor(0.92f, 0.92f, 0.96f, 0.9f));
    m_restoreIcon = std::make_shared<AestraUI::NUIIcon>(restoreSvg);
    m_restoreIcon->setColor(AestraUI::NUIColor(0.92f, 0.92f, 0.96f, 0.9f));
    m_closeIcon = std::make_shared<AestraUI::NUIIcon>(closeSvg);
    m_closeIcon->setColor(AestraUI::NUIColor(0.92f, 0.92f, 0.96f, 0.9f));

    // Create close button (X)
    m_closeButton = std::make_shared<AestraUI::NUIButton>();
    m_closeButton->setStyle(AestraUI::NUIButton::Style::Text);
    m_closeButton->setBackgroundColor(AestraUI::NUIColor::transparent());
    m_closeButton->setHoverColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.25f)); // Increased from 0.08
    m_closeButton->setPressedColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.14f));
    m_closeButton->setTextColor(AestraUI::NUIColor::transparent());
    m_closeButton->setOnClick([this]() {
        onCloseClicked();
    });
    addChild(m_closeButton);
    
    // Create maximize button ([] when normal, [ ] when maximized)
    m_maximizeButton = std::make_shared<AestraUI::NUIButton>();
    m_maximizeButton->setStyle(AestraUI::NUIButton::Style::Text);
    m_maximizeButton->setBackgroundColor(AestraUI::NUIColor::transparent());
    m_maximizeButton->setHoverColor(AestraUI::NUIColor::white().withAlpha(0.5f));
    m_maximizeButton->setPressedColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.14f));
    m_maximizeButton->setTextColor(AestraUI::NUIColor::transparent());
    m_maximizeButton->setOnClick([this]() {
        onMaximizeClicked();
    });
    addChild(m_maximizeButton);
    
    // Create minimize button (_)
    m_minimizeButton = std::make_shared<AestraUI::NUIButton>();
    m_minimizeButton->setStyle(AestraUI::NUIButton::Style::Text);
    m_minimizeButton->setBackgroundColor(AestraUI::NUIColor::transparent());
    m_minimizeButton->setHoverColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.25f)); // Increased
    m_minimizeButton->setPressedColor(AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.14f));
    m_minimizeButton->setTextColor(AestraUI::NUIColor::transparent());
    m_minimizeButton->setOnClick([this]() {
        onMinimizeClicked();
    });
    addChild(m_minimizeButton);
}

void WindowPanel::setContent(std::shared_ptr<AestraUI::NUIComponent> content) {
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
        m_minimizeButton->setText(m_minimized ? "+" : "_"); // + for expand, _ for minimize
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
    
    if (m_maximizeIcon) m_maximizeIcon->setVisible(!m_maximized);
    if (m_restoreIcon) m_restoreIcon->setVisible(m_maximized);
    
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

void WindowPanel::onRender(AestraUI::NUIRenderer& renderer) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    auto bounds = getBounds();
    
    // Draw title bar
    AestraUI::NUIRect titleBarRect(bounds.x, bounds.y, bounds.width, m_titleBarHeight);
    m_titleBarBounds = titleBarRect;
    
    // GLASS DESIGN: Unified semi-transparent background + border
    // We draw ONE rounded rect for the whole window if possible, or composed rects.
    // Since WindowPanel is a floating window, let's treat the whole thing as one glass pane.
    
    // Consistent corner radius matching child components
    const float windowRadius = theme.getRadius("m");
    auto glassColor = theme.getColor("surfaceTertiary");
    auto borderColor = theme.getColor("border");

    renderer.drawShadow(bounds, 0.0f, 10.0f, 28.0f, AestraUI::NUIColor(0, 0, 0, 0.22f));
    
    // Draw content background (if expanded) - Unified with Title Bar in Glass Mode
    if (!m_minimized) {
        // Draw one large glass pane for the whole window
        // Full window body with rounded corners
        renderer.fillRoundedRect(bounds, windowRadius, glassColor);
        renderer.strokeRoundedRect(bounds, windowRadius, 1.0f, borderColor);
        renderer.fillRoundedRect({bounds.x + 1.0f, bounds.y + 1.0f, bounds.width - 2.0f, std::min(44.0f, bounds.height - 2.0f)},
                                 std::max(0.0f, windowRadius - 1.0f),
                                 AestraUI::NUIColor::white().withAlpha(0.020f));
        
        // Separator for title bar (subtle)
        renderer.drawLine(
            AestraUI::NUIPoint(bounds.x + 4, bounds.y + m_titleBarHeight),
            AestraUI::NUIPoint(bounds.x + bounds.width - 4, bounds.y + m_titleBarHeight),
            1.0f, 
            borderColor.withAlpha(0.5f)
        );
    } else {
        // Minimized: Just title bar with rounded corners
        renderer.fillRoundedRect(titleBarRect, windowRadius, glassColor);
        renderer.strokeRoundedRect(titleBarRect, windowRadius, 1.0f, borderColor);
        renderer.fillRoundedRect({titleBarRect.x + 1.0f, titleBarRect.y + 1.0f, titleBarRect.width - 2.0f, titleBarRect.height * 0.52f},
                                 std::max(0.0f, windowRadius - 1.0f),
                                 AestraUI::NUIColor::white().withAlpha(0.020f));
    }
    
    // Draw title text
    auto textColor = theme.getColor("textSecondary"); // Use secondary text color
    float fontSize = 12.0f;
    auto titleSize = renderer.measureText(m_title, fontSize);
    float textX = bounds.x + 10.0f;
    float textY = bounds.y + (m_titleBarHeight - titleSize.height) * 0.5f;
    renderer.drawText(m_title, AestraUI::NUIPoint(textX, textY), fontSize, textColor);
    
    // Render children (content + buttons)
    renderChildren(renderer);

    const auto drawButtonIcon = [&renderer](const std::shared_ptr<AestraUI::NUIButton>& button,
                                            const std::shared_ptr<AestraUI::NUIIcon>& icon) {
        if (!button || !icon || !button->isVisible()) return;
        auto b = button->getBounds();
        const float iconSize = 14.0f;
        icon->setBounds({
            b.x + (b.width - iconSize) * 0.5f,
            b.y + (b.height - iconSize) * 0.5f,
            iconSize,
            iconSize
        });
        icon->onRender(renderer);
    };

    drawButtonIcon(m_minimizeButton, m_minimizeIcon);
    drawButtonIcon(m_maximizeButton, m_maximized ? m_restoreIcon : m_maximizeIcon);
    drawButtonIcon(m_closeButton, m_closeIcon);
}

void WindowPanel::onResize(int width, int height) {
    layoutContent();
}

bool WindowPanel::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled()) return false;

    // Title-bar drag handling (panels can float independently of the playlist layout).
    if (m_draggingTitleBar) {
        if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
            m_draggingTitleBar = false;

            if (m_onDragEnd) {
                m_onDragEnd();
            }
            return true;
        }

        // Dragging (mouse move events set button = None)
        if (event.button == AestraUI::NUIMouseButton::None) {
            if (m_onDragMove) {
                m_onDragMove(event.position);
            }
            return true;
        }
    }

    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        // Double-click the title bar to toggle maximize (excluding buttons).
        bool insideTitle = m_titleBarBounds.contains(event.position);
        
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastTitleBarClickTime).count();
        const bool manualDoubleClick = elapsedMs > 0 && elapsedMs < 350;
        if ((event.doubleClick || manualDoubleClick) && insideTitle) {
            const auto onButton =
                (m_closeButton && m_closeButton->getBounds().contains(event.position)) ||
                (m_maximizeButton && m_maximizeButton->getBounds().contains(event.position)) ||
                (m_minimizeButton && m_minimizeButton->getBounds().contains(event.position));
            if (!onButton) {
                toggleMaximize();
                m_lastTitleBarClickTime = std::chrono::steady_clock::time_point{};
                return true;
            }
        }

        // Start dragging when the title bar is clicked (excluding the title-bar buttons).
        if (insideTitle) {
            const auto onButton =
                (m_closeButton && m_closeButton->getBounds().contains(event.position)) ||
                (m_maximizeButton && m_maximizeButton->getBounds().contains(event.position)) ||
                (m_minimizeButton && m_minimizeButton->getBounds().contains(event.position));

            if (!onButton) {
                m_lastTitleBarClickTime = now;
                m_userPositioned = true;
                m_draggingTitleBar = true;
                if (m_onDragStart) {
                    m_onDragStart(event.position);
                }
                return true;
            }
        }
    }

    // Let children handle events (buttons, content, etc.)
    const bool handledByChildren = AestraUI::NUIComponent::onMouseEvent(event);
    if (handledByChildren) return true;

    // Consume events that occur inside the panel bounds to prevent "click-through"
    // into underlying timeline/content layers.
    if (getBounds().contains(event.position)) {
        return true;
    }

    return false;
}

void WindowPanel::layoutContent() {
    auto bounds = getBounds();
    const float buttonSize = std::max(18.0f, m_titleBarHeight - 8.0f);
    const float buttonPadding = 4.0f;
    float currentX = bounds.width - buttonSize - buttonPadding;
    
    // Update title bar bounds for hit testing (absolute coordinates)
    m_titleBarBounds = NUIAbsolute(bounds, 0, 0, bounds.width, m_titleBarHeight);
    
    // Layout buttons right-to-left: Close, Maximize, Minimize
    if (m_closeButton) {
        m_closeButton->setBounds(NUIAbsolute(bounds, currentX, (m_titleBarHeight - buttonSize) * 0.5f, buttonSize, buttonSize));
        currentX -= buttonSize + buttonPadding;
    }
    
    if (m_maximizeButton) {
        m_maximizeButton->setBounds(NUIAbsolute(bounds, currentX, (m_titleBarHeight - buttonSize) * 0.5f, buttonSize, buttonSize));
        currentX -= buttonSize + buttonPadding;
    }
    
    if (m_minimizeButton) {
        m_minimizeButton->setBounds(NUIAbsolute(bounds, currentX, (m_titleBarHeight - buttonSize) * 0.5f, buttonSize, buttonSize));
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
    m_lastTitleBarClickTime = std::chrono::steady_clock::time_point{};
    toggleMinimize();
}

void WindowPanel::onMaximizeClicked() {
    m_lastTitleBarClickTime = std::chrono::steady_clock::time_point{};
    toggleMaximize();
}

void WindowPanel::onCloseClicked() {
    if (m_onClose) {
        m_onClose();
    }
}
