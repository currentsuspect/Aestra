// © 2025 Aestra Studios – All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIButton.h"
#include "NUIIcon.h"
#include <chrono>
#include <string>
#include <memory>
#include <functional>

// Forward declarations
namespace AestraUI {
    class NUIRenderer;
    enum class NUICursorStyle;
}

namespace Aestra {
namespace Audio {

/**
 * @brief WindowPanel - A dockable window panel with title bar
 * 
 * Features:
 * - Title bar with minimize/maximize buttons
 * - When minimized, shows only title bar (collapsed)
 * - When maximized, shows full content
 * - Draggable by title bar for future docking
 */
class WindowPanel : public AestraUI::NUIComponent {
public:
    WindowPanel(const std::string& title);
    ~WindowPanel() override = default;

    // Set the content component (piano roll, mixer, etc.)
    void setContent(std::shared_ptr<AestraUI::NUIComponent> content);
    std::shared_ptr<AestraUI::NUIComponent> getContent() const { return m_content; }

    // Window state
    void setMinimized(bool minimized);
    bool isMinimized() const { return m_minimized; }
    
    void setMaximized(bool maximized);
    bool isMaximized() const { return m_maximized; }
    
    void toggleMinimize();
    void toggleMaximize();

    // Title bar
    void setTitle(const std::string& title);
    const std::string& getTitle() const { return m_title; }

    float getTitleBarHeight() const { return m_titleBarHeight; }
    bool isUserPositioned() const { return m_userPositioned; }

    float getExpandedHeight() const { return m_expandedHeight; }
    void setUserPositioned(bool positioned) { m_userPositioned = positioned; }

    // Component overrides
    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;

    // Callbacks
    void setOnMinimizeToggle(std::function<void(bool)> callback) { m_onMinimizeToggle = callback; }
    void setOnMaximizeToggle(std::function<void(bool)> callback) { m_onMaximizeToggle = callback; }
    void setOnClose(std::function<void()> callback) { m_onClose = callback; }
    
    // Drag events (forwarded to parent controller)
    using DragCallback = std::function<void(const AestraUI::NUIPoint&)>;
    void setOnDragStart(DragCallback callback) { m_onDragStart = callback; }
    void setOnDragMove(DragCallback callback) { m_onDragMove = callback; }
    void setOnDragEnd(std::function<void()> callback) { m_onDragEnd = callback; }

    // Resize events (forwarded to parent controller)
    using ResizeRectCallback = std::function<void(const AestraUI::NUIRect&)>;
    void setOnResizeStart(std::function<void()> callback) { m_onResizeStart = callback; }
    void setOnResizeMove(ResizeRectCallback callback) { m_onResizeMove = callback; }
    void setOnResizeEnd(std::function<void()> callback) { m_onResizeEnd = callback; }
    void setMinimumPanelSize(float width, float height);
    AestraUI::NUICursorStyle getResizeCursorStyleForPoint(const AestraUI::NUIPoint& point) const;

private:
    enum ResizeEdge : int {
        ResizeNone   = 0,
        ResizeLeft   = 1 << 0,
        ResizeRight  = 1 << 1,
        ResizeTop    = 1 << 2,
        ResizeBottom = 1 << 3
    };

    std::string m_title;
    std::shared_ptr<AestraUI::NUIComponent> m_content;
    
    // Window state
    bool m_minimized{false};
    bool m_maximized{false};
    float m_titleBarHeight{28.0f};
    float m_expandedHeight{300.0f}; // Remember height when expanded
    
    // Title bar buttons
    std::shared_ptr<AestraUI::NUIButton> m_minimizeButton;
    std::shared_ptr<AestraUI::NUIButton> m_maximizeButton;
    std::shared_ptr<AestraUI::NUIButton> m_closeButton;
    std::shared_ptr<AestraUI::NUIIcon> m_minimizeIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_maximizeIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_restoreIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_closeIcon;
    
    // Dragging state (for future docking)
    bool m_draggingTitleBar{false};
    AestraUI::NUIPoint m_dragStartPos;
    AestraUI::NUIRect m_dragStartBounds;
    bool m_userPositioned{false};
    bool m_resizing{false};
    int m_resizeEdges{ResizeNone};
    AestraUI::NUIPoint m_resizeStartPos;
    AestraUI::NUIRect m_resizeStartBounds;
    float m_minPanelWidth{280.0f};
    float m_minPanelHeight{180.0f};
    
    // Hover states
    bool m_titleBarHovered{false};
    AestraUI::NUIRect m_titleBarBounds;
    std::chrono::steady_clock::time_point m_lastTitleBarClickTime{};
    
    // Callbacks
    std::function<void(bool)> m_onMinimizeToggle;
    std::function<void(bool)> m_onMaximizeToggle;
    std::function<void()> m_onClose;
    
    DragCallback m_onDragStart;
    DragCallback m_onDragMove;
    std::function<void()> m_onDragEnd;
    std::function<void()> m_onResizeStart;
    ResizeRectCallback m_onResizeMove;
    std::function<void()> m_onResizeEnd;
    
    void layoutContent();
    void onMinimizeClicked();
    void onMaximizeClicked();
    void onCloseClicked();
    int getResizeEdgesAtPoint(const AestraUI::NUIPoint& point) const;
};

} // namespace Audio
} // namespace Aestra
