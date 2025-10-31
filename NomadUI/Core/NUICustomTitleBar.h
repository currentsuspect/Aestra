// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "NUIIcon.h"
#include <string>
#include <functional>
#include <memory>

namespace NomadUI {

/**
 * Custom title bar for Nomad windows
 * Provides window controls (minimize, maximize, close) and drag functionality
 */
class NUICustomTitleBar : public NUIComponent {
public:
    NUICustomTitleBar();
    ~NUICustomTitleBar() = default;

    // Title bar properties
    void setTitle(const std::string& title);
    std::string getTitle() const { return title_; }
    
    void setHeight(float height);
    float getHeight() const { return height_; }
    
    // Window control callbacks
    void setOnMinimize(std::function<void()> callback) { onMinimize_ = callback; }
    void setOnMaximize(std::function<void()> callback) { onMaximize_ = callback; }
    void setOnClose(std::function<void()> callback) { onClose_ = callback; }
    void setOnDrag(std::function<void(int, int)> callback) { onDrag_ = callback; }
    
    // Window state
    void setMaximized(bool maximized);
    bool isMaximized() const { return isMaximized_; }

    // Component overrides
    void onRender(NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

private:
    // Title bar content
    std::string title_;
    float height_;
    
    // Window controls
    bool isMaximized_;
    NUIRect minimizeButtonRect_;
    NUIRect maximizeButtonRect_;
    NUIRect closeButtonRect_;
    
    // Window control icons
    std::shared_ptr<NUIIcon> minimizeIcon_;
    std::shared_ptr<NUIIcon> maximizeIcon_;
    std::shared_ptr<NUIIcon> restoreIcon_;
    std::shared_ptr<NUIIcon> closeIcon_;
    // App icon (left side of title bar)
    std::shared_ptr<NUIIcon> appIcon_;
    
    // Hover states
    enum class HoverButton {
        None,
        Minimize,
        Maximize,
        Close
    };
    HoverButton hoveredButton_;
    
    // Drag state
    bool isDragging_;
    NUIPoint dragStartPos_;
    NUIPoint windowStartPos_;
    
    // Callbacks
    std::function<void()> onMinimize_;
    std::function<void()> onMaximize_;
    std::function<void()> onClose_;
    std::function<void(int, int)> onDrag_;
    
    // Helper methods
    void updateButtonRects();
    void drawWindowControls(NUIRenderer& renderer);
    bool isPointInButton(const NUIPoint& point, const NUIRect& buttonRect);
    void handleButtonClick(const NUIRect& buttonRect);
    void createIcons();
};

} // namespace NomadUI
