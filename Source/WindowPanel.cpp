// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#include "WindowPanel.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"

#include <algorithm>
#include <cmath>

using namespace Nomad::Audio;

WindowPanel::WindowPanel(const std::string& title)
    : m_title(title)
{
    // Create close button (X)
    m_closeButton = std::make_shared<NomadUI::NUIButton>();
    m_closeButton->setText("X");
    m_closeButton->setStyle(NomadUI::NUIButton::Style::Text);
    // NOTE: NUIButton::Style::Icon intentionally does not draw text.
    // These titlebar controls use text + transparent background.
    m_closeButton->setBackgroundColor(NomadUI::NUIColor::transparent());
    m_closeButton->setHoverColor(NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.25f)); // Increased from 0.08
    m_closeButton->setPressedColor(NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.14f));
    m_closeButton->setTextColor(NomadUI::NUIColor(0.92f, 0.92f, 0.96f, 0.9f));
    m_closeButton->setOnClick([this]() {
        onCloseClicked();
    });
    addChild(m_closeButton);
    
    // Create maximize button ([] when normal, [ ] when maximized)
    m_maximizeButton = std::make_shared<NomadUI::NUIButton>();
    m_maximizeButton->setText("[]");
    m_maximizeButton->setStyle(NomadUI::NUIButton::Style::Text);
    m_maximizeButton->setBackgroundColor(NomadUI::NUIColor::transparent());
    m_maximizeButton->setHoverColor(NomadUI::NUIColor::white().withAlpha(0.5f));
    m_maximizeButton->setPressedColor(NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.14f));
    m_maximizeButton->setTextColor(NomadUI::NUIColor(0.92f, 0.92f, 0.96f, 0.9f));
    m_maximizeButton->setOnClick([this]() {
        onMaximizeClicked();
    });
    addChild(m_maximizeButton);
    
    // Create minimize button (_)
    m_minimizeButton = std::make_shared<NomadUI::NUIButton>();
    m_minimizeButton->setText("_");
    m_minimizeButton->setStyle(NomadUI::NUIButton::Style::Text);
    m_minimizeButton->setBackgroundColor(NomadUI::NUIColor::transparent());
    m_minimizeButton->setHoverColor(NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.25f)); // Increased
    m_minimizeButton->setPressedColor(NomadUI::NUIColor(1.0f, 1.0f, 1.0f, 0.14f));
    m_minimizeButton->setTextColor(NomadUI::NUIColor(0.92f, 0.92f, 0.96f, 0.9f));
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
    
    // Update maximize button icon
    if (m_maximizeButton) {
        m_maximizeButton->setText(m_maximized ? "[ ]" : "[]"); // Different icon when maximized
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
    
    // GLASS DESIGN: Unified semi-transparent background + border
    // We draw ONE rounded rect for the whole window if possible, or composed rects.
    // Since WindowPanel is a floating window, let's treat the whole thing as one glass pane.
    
    // Draw content background (if expanded) - Unified with Title Bar in Glass Mode
    if (!m_minimized) {
        // Draw one large glass pane for the whole window
        auto glassColor = theme.getColor("surfaceTertiary"); // Now 0.85 alpha
        auto glassBorder = theme.getColor("glassBorder");
        
        // Full window body
        renderer.fillRect(bounds, glassColor);
        renderer.strokeRect(bounds, 1.0f, glassBorder);
        
        // Separator for title bar (subtle)
        renderer.drawLine(
            NomadUI::NUIPoint(bounds.x, bounds.y + m_titleBarHeight),
            NomadUI::NUIPoint(bounds.x + bounds.width, bounds.y + m_titleBarHeight),
            1.0f, 
            glassBorder.withAlpha(0.05f)
        );
    } else {
        // Minimized: Just title bar
        auto glassColor = theme.getColor("surfaceTertiary");
        auto glassBorder = theme.getColor("glassBorder");
        renderer.fillRect(titleBarRect, glassColor);
        renderer.strokeRect(titleBarRect, 1.0f, glassBorder);
    }
    
    // Draw title text
    auto textColor = theme.getColor("textSecondary"); // Use secondary text color
    float fontSize = 12.0f;
    auto titleSize = renderer.measureText(m_title, fontSize);
    float textX = bounds.x + 8.0f;
    float textY = bounds.y + (m_titleBarHeight - titleSize.height) * 0.5f;
    renderer.drawText(m_title, NomadUI::NUIPoint(textX, textY), fontSize, textColor);
    
    // Render children (content + buttons)
    renderChildren(renderer);
}

void WindowPanel::onResize(int width, int height) {
    layoutContent();
}

bool WindowPanel::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    if (!isVisible() || !isEnabled()) return false;

    // DEBUG: Trace mouse events for Piano Roll
    bool debugTrace = (m_title == "Piano Roll");
    if (debugTrace && event.pressed) {
        std::cout << "[WindowPanel] '" << m_title << "' MouseDown at " << event.position.x << "," << event.position.y << std::endl;
        std::cout << "  Bounds: " << getBounds().x << "," << getBounds().y << " " << getBounds().width << "x" << getBounds().height << std::endl;
        std::cout << "  TitleBarBounds: " << m_titleBarBounds.x << "," << m_titleBarBounds.y << " " << m_titleBarBounds.width << "x" << m_titleBarBounds.height << std::endl;
    }

    // Title-bar drag handling (panels can float independently of the playlist layout).
    if (m_draggingTitleBar) {
        if (event.released && event.button == NomadUI::NUIMouseButton::Left) {
            m_draggingTitleBar = false;

            if (m_onDragEnd) {
                m_onDragEnd();
            }
            return true;
        }

        // Dragging (mouse move events set button = None)
        if (event.button == NomadUI::NUIMouseButton::None) {
            if (m_onDragMove) {
                m_onDragMove(event.position);
            }
            return true;
        }
    }

    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        // Double-click the title bar to toggle maximize (excluding buttons).
        bool insideTitle = m_titleBarBounds.contains(event.position);
        if (debugTrace) {
             std::cout << "  Inside TitleBar: " << (insideTitle ? "YES" : "NO") << std::endl;
        }
        
        if (event.doubleClick && insideTitle) {
            const auto onButton =
                (m_closeButton && m_closeButton->getBounds().contains(event.position)) ||
                (m_maximizeButton && m_maximizeButton->getBounds().contains(event.position)) ||
                (m_minimizeButton && m_minimizeButton->getBounds().contains(event.position));
            if (!onButton) {
                toggleMaximize();
                return true;
            }
        }

        // Start dragging when the title bar is clicked (excluding the title-bar buttons).
        if (insideTitle) {
            const auto onButton =
                (m_closeButton && m_closeButton->getBounds().contains(event.position)) ||
                (m_maximizeButton && m_maximizeButton->getBounds().contains(event.position)) ||
                (m_minimizeButton && m_minimizeButton->getBounds().contains(event.position));
            
            if (debugTrace) std::cout << "  On Button Check: " << (onButton ? "YES" : "NO") << std::endl;

            if (!onButton) {
                m_userPositioned = true;
                m_draggingTitleBar = true;
                if (m_onDragStart) {
                    m_onDragStart(event.position);
                }
                return true;
            } else {
                 if (debugTrace) std::cout << "  Title logic skipped (on button), passing to children." << std::endl;
            }
        }
    }

    // Let children handle events (buttons, content, etc.)
    if (debugTrace && event.pressed) {
        std::cout << "  Iterating Children (Reverse Order):" << std::endl;
        const auto& children = getChildren();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            auto b = (*it)->getBounds();
            bool contains = b.contains(event.position);
            std::cout << "    Child (Type: " << typeid(*(*it)).name() << ") Bounds: " << b.x << "," << b.y 
                      << " " << b.width << "x" << b.height 
                      << " Contains: " << (contains ? "YES" : "NO") << std::endl;
        }
    }
    const bool handledByChildren = NomadUI::NUIComponent::onMouseEvent(event);
    if (debugTrace && event.pressed) std::cout << "  Handled by Children: " << (handledByChildren ? "YES" : "NO") << std::endl;
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
    float buttonSize = m_titleBarHeight - 4.0f;
    float buttonPadding = 2.0f;
    float currentX = bounds.width - buttonSize - buttonPadding;
    
    // Update title bar bounds for hit testing (absolute coordinates)
    m_titleBarBounds = NUIAbsolute(bounds, 0, 0, bounds.width, m_titleBarHeight);
    
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
    std::cout << "[WindowPanel] onCloseClicked invoked." << std::endl;
    if (m_onClose) {
        std::cout << "[WindowPanel] Executing m_onClose." << std::endl;
        m_onClose();
    } else {
        std::cout << "[WindowPanel] m_onClose is NULL." << std::endl;
    }
}
