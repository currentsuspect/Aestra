// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIButton.h"
#include <string>
#include <memory>
#include <functional>

// Forward declarations
namespace NomadUI {
    class NUIRenderer;
}

namespace Nomad {
namespace Audio {

/**
 * @brief WindowPanel - A dockable window panel with title bar (FL Studio style)
 * 
 * Features:
 * - Title bar with minimize/maximize buttons
 * - When minimized, shows only title bar (collapsed)
 * - When maximized, shows full content
 * - Draggable by title bar for future docking
 */
class WindowPanel : public NomadUI::NUIComponent {
public:
    WindowPanel(const std::string& title);
    ~WindowPanel() override = default;

    // Set the content component (piano roll, mixer, etc.)
    void setContent(std::shared_ptr<NomadUI::NUIComponent> content);
    std::shared_ptr<NomadUI::NUIComponent> getContent() const { return m_content; }

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

    // Component overrides
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;

    // Callbacks
    void setOnMinimizeToggle(std::function<void(bool)> callback) { m_onMinimizeToggle = callback; }
    void setOnMaximizeToggle(std::function<void(bool)> callback) { m_onMaximizeToggle = callback; }
    void setOnClose(std::function<void()> callback) { m_onClose = callback; }

private:
    std::string m_title;
    std::shared_ptr<NomadUI::NUIComponent> m_content;
    
    // Window state
    bool m_minimized{false};
    bool m_maximized{false};
    float m_titleBarHeight{25.0f};
    float m_expandedHeight{300.0f}; // Remember height when expanded
    
    // Title bar buttons
    std::shared_ptr<NomadUI::NUIButton> m_minimizeButton;
    std::shared_ptr<NomadUI::NUIButton> m_maximizeButton;
    std::shared_ptr<NomadUI::NUIButton> m_closeButton;
    
    // Dragging state (for future docking)
    bool m_draggingTitleBar{false};
    NomadUI::NUIPoint m_dragStartPos;
    
    // Hover states
    bool m_titleBarHovered{false};
    NomadUI::NUIRect m_titleBarBounds;
    
    // Callbacks
    std::function<void(bool)> m_onMinimizeToggle;
    std::function<void(bool)> m_onMaximizeToggle;
    std::function<void()> m_onClose;
    
    void layoutContent();
    void onMinimizeClicked();
    void onMaximizeClicked();
    void onCloseClicked();
};

} // namespace Audio
} // namespace Nomad
