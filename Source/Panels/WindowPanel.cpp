// © 2025 Aestra Studios – All Rights Reserved. Licensed for personal & educational use only.
#include "WindowPanel.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Platform/NUIPlatformBridge.h"
#include "../AestraCore/include/AestraLog.h"

#include <algorithm>
#include <cmath>

using namespace Aestra::Audio;

namespace {
AestraUI::NUICursorStyle cursorStyleForResizeEdges(int edges) {
    constexpr int kResizeLeft = 1 << 0;
    constexpr int kResizeRight = 1 << 1;
    constexpr int kResizeTop = 1 << 2;
    constexpr int kResizeBottom = 1 << 3;
    const bool left = (edges & kResizeLeft) != 0;
    const bool right = (edges & kResizeRight) != 0;
    const bool top = (edges & kResizeTop) != 0;
    const bool bottom = (edges & kResizeBottom) != 0;

    if ((left && top) || (right && bottom)) {
        return AestraUI::NUICursorStyle::ResizeNWSE;
    }
    if ((right && top) || (left && bottom)) {
        return AestraUI::NUICursorStyle::ResizeNESW;
    }
    if (left || right) {
        return AestraUI::NUICursorStyle::ResizeEW;
    }
    if (top || bottom) {
        return AestraUI::NUICursorStyle::ResizeNS;
    }
    return AestraUI::NUICursorStyle::Arrow;
}
} // namespace

WindowPanel::WindowPanel(const std::string& title)
    : m_title(title)
{
    auto& theme = AestraUI::NUIThemeManager::getInstance();
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

    const auto iconColor = theme.getColor("textPrimary").withAlpha(0.92f);
    m_minimizeIcon = std::make_shared<AestraUI::NUIIcon>(minimizeSvg);
    m_minimizeIcon->setColor(iconColor);
    m_maximizeIcon = std::make_shared<AestraUI::NUIIcon>(maximizeSvg);
    m_maximizeIcon->setColor(iconColor);
    m_restoreIcon = std::make_shared<AestraUI::NUIIcon>(restoreSvg);
    m_restoreIcon->setColor(iconColor);
    m_closeIcon = std::make_shared<AestraUI::NUIIcon>(closeSvg);
    m_closeIcon->setColor(iconColor);

    // Create close button (X)
    m_closeButton = std::make_shared<AestraUI::NUIButton>();
    m_closeButton->setStyle(AestraUI::NUIButton::Style::Text);
    m_closeButton->setText("");
    m_closeButton->setBackgroundColor(AestraUI::NUIColor::transparent());
    m_closeButton->setHoverColor(theme.getColor("error").withAlpha(0.24f));
    m_closeButton->setPressedColor(theme.getColor("error").withAlpha(0.16f));
    m_closeButton->setTextColor(AestraUI::NUIColor::transparent());
    m_closeButton->setOnClick([this]() {
        onCloseClicked();
    });
    addChild(m_closeButton);
    
    // Create maximize button ([] when normal, [ ] when maximized)
    m_maximizeButton = std::make_shared<AestraUI::NUIButton>();
    m_maximizeButton->setStyle(AestraUI::NUIButton::Style::Text);
    m_maximizeButton->setText("");
    m_maximizeButton->setBackgroundColor(AestraUI::NUIColor::transparent());
    m_maximizeButton->setHoverColor(theme.getColor("primary").withAlpha(0.20f));
    m_maximizeButton->setPressedColor(theme.getColor("primary").withAlpha(0.14f));
    m_maximizeButton->setTextColor(AestraUI::NUIColor::transparent());
    m_maximizeButton->setOnClick([this]() {
        onMaximizeClicked();
    });
    addChild(m_maximizeButton);
    
    // Create minimize button (_)
    m_minimizeButton = std::make_shared<AestraUI::NUIButton>();
    m_minimizeButton->setStyle(AestraUI::NUIButton::Style::Text);
    m_minimizeButton->setText("");
    m_minimizeButton->setBackgroundColor(AestraUI::NUIColor::transparent());
    m_minimizeButton->setHoverColor(theme.getColor("primary").withAlpha(0.20f));
    m_minimizeButton->setPressedColor(theme.getColor("primary").withAlpha(0.14f));
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
    
    // Unified panel shell + flat titlebar treatment
    const float windowRadius = theme.getRadius("m");
    auto bodyColor = theme.getColor("surfaceTertiary");
    // Title bar shares the transport bar's charcoal (backgroundSecondary) so all
    // docked-panel chrome reads as one consistent surface.
    auto titleBarColor = theme.getColor("backgroundSecondary");
    auto borderColor = theme.getColor("border");

    // Flat: no panel drop shadow — border + surface separation carry the frame.

    // Draw content background
    if (!m_minimized) {
        renderer.fillRoundedRect(bounds, windowRadius, bodyColor);
        renderer.strokeRoundedRect(bounds, windowRadius, 1.0f, borderColor);
        renderer.fillRect(titleBarRect, titleBarColor.withAlpha(0.98f));
        
        // Separator for title bar
        renderer.drawLine(
            AestraUI::NUIPoint(bounds.x + 4, bounds.y + m_titleBarHeight),
            AestraUI::NUIPoint(bounds.x + bounds.width - 4, bounds.y + m_titleBarHeight),
            1.0f, 
            borderColor.withAlpha(0.72f)
        );
    } else {
        // Minimized: titlebar only
        renderer.fillRoundedRect(titleBarRect, windowRadius, titleBarColor.withAlpha(0.98f));
        renderer.strokeRoundedRect(titleBarRect, windowRadius, 1.0f, borderColor);
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

    if (event.pressed) {
        bringToFront();
    }

    if (m_resizing) {
        if (event.released && event.button == AestraUI::NUIMouseButton::Left) {
            m_resizing = false;
            m_resizeEdges = ResizeNone;
            if (m_onResizeEnd) m_onResizeEnd();
            return true;
        }

        if (event.button == AestraUI::NUIMouseButton::None) {
            const auto delta = event.position - m_resizeStartPos;
            AestraUI::NUIRect proposed = m_resizeStartBounds;

            if (m_resizeEdges & ResizeLeft) {
                proposed.x = m_resizeStartBounds.x + delta.x;
                proposed.width = m_resizeStartBounds.width - delta.x;
            }
            if (m_resizeEdges & ResizeRight) {
                proposed.width = m_resizeStartBounds.width + delta.x;
            }
            if (m_resizeEdges & ResizeTop) {
                proposed.y = m_resizeStartBounds.y + delta.y;
                proposed.height = m_resizeStartBounds.height - delta.y;
            }
            if (m_resizeEdges & ResizeBottom) {
                proposed.height = m_resizeStartBounds.height + delta.y;
            }

            if (proposed.width < m_minPanelWidth) {
                if (m_resizeEdges & ResizeLeft) {
                    proposed.x = m_resizeStartBounds.right() - m_minPanelWidth;
                }
                proposed.width = m_minPanelWidth;
            }
            if (proposed.height < m_minPanelHeight) {
                if (m_resizeEdges & ResizeTop) {
                    proposed.y = m_resizeStartBounds.bottom() - m_minPanelHeight;
                }
                proposed.height = m_minPanelHeight;
            }

            if (m_onResizeMove) {
                m_onResizeMove(proposed);
            } else {
                setBounds(proposed);
            }
            return true;
        }
    }

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
        // Edge/corner resize handling (skip when minimized/maximized).
        if (!m_minimized && !m_maximized) {
            const int edges = getResizeEdgesAtPoint(event.position);
            if (edges != ResizeNone) {
                m_resizing = true;
                m_resizeEdges = edges;
                m_resizeStartPos = event.position;
                m_resizeStartBounds = getBounds();
                if (m_onResizeStart) m_onResizeStart();
                return true;
            }
        }

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
    if (getBounds().contains(event.position)) {        return true;
    }

    return false;
}

void WindowPanel::layoutContent() {
    auto bounds = getBounds();
    const float panelWidth = std::max(100.0f, bounds.width);
    const float panelHeight = std::max(m_titleBarHeight + 20.0f, bounds.height);
    const float buttonSize = std::max(18.0f, m_titleBarHeight - 8.0f);
    const float buttonPadding = 4.0f;
    float currentX = panelWidth - buttonSize - buttonPadding;
    
    // Update title bar bounds for hit testing (absolute coordinates)
    m_titleBarBounds = NUIAbsolute(bounds, 0, 0, panelWidth, m_titleBarHeight);
    
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
        float contentHeight = std::max(20.0f, panelHeight - m_titleBarHeight);
        m_content->setBounds(NUIAbsolute(bounds, 0, contentY, panelWidth, contentHeight));
        
        // Trigger content's internal layout
        m_content->onResize(static_cast<int>(panelWidth), static_cast<int>(contentHeight));
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

void WindowPanel::setMinimumPanelSize(float width, float height) {
    m_minPanelWidth = std::max(100.0f, width);
    m_minPanelHeight = std::max(m_titleBarHeight + 40.0f, height);
}

AestraUI::NUICursorStyle WindowPanel::getResizeCursorStyleForPoint(const AestraUI::NUIPoint& point) const {
    if (!isVisible() || !isEnabled() || m_minimized || m_maximized) {
        return AestraUI::NUICursorStyle::Arrow;
    }

    const int edges = (m_resizing && m_resizeEdges != ResizeNone)
        ? m_resizeEdges
        : getResizeEdgesAtPoint(point);
    return cursorStyleForResizeEdges(edges);
}

int WindowPanel::getResizeEdgesAtPoint(const AestraUI::NUIPoint& point) const {
    constexpr float kResizeHit = 7.0f;
    const auto b = getBounds();
    if (!b.contains(point)) return ResizeNone;

    int edges = ResizeNone;
    if (std::abs(point.x - b.x) <= kResizeHit) edges |= ResizeLeft;
    if (std::abs(point.x - b.right()) <= kResizeHit) edges |= ResizeRight;
    if (std::abs(point.y - b.y) <= kResizeHit) edges |= ResizeTop;
    if (std::abs(point.y - b.bottom()) <= kResizeHit) edges |= ResizeBottom;
    return edges;
}
