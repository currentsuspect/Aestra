// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUICustomTitleBar.h"
#include "NUITypes.h"
#include <memory>
#include <functional>

namespace NomadUI {

// Forward declarations
class NUIPlatformBridge;
using NUIWindowWin32 = NUIPlatformBridge;  // Compatibility typedef

/**
 * Custom window component that provides full screen and custom title bar functionality
 * This is the main window container for Nomad applications
 */
class NUICustomWindow : public NUIComponent {
public:
    NUICustomWindow();
    ~NUICustomWindow() = default;

    // Window management
    void setTitle(const std::string& title);
    std::string getTitle() const { return titleBar_->getTitle(); }
    
    void setWindowHandle(NUIWindowWin32* windowHandle) { windowHandle_ = windowHandle; }
    
    // Full screen support
    void toggleFullScreen();
    bool isFullScreen() const;
    void enterFullScreen();
    void exitFullScreen();
    
    // Content area management
    void setContent(NUIComponent* content);
    NUIComponent* getContent() const { return content_; }
    
    // Window state
    void setMaximized(bool maximized);
    bool isMaximized() const { return titleBar_->isMaximized(); }
    
    // Access title bar for adding custom components (like toggle buttons)
    std::shared_ptr<NUICustomTitleBar> getTitleBar() const { return titleBar_; }

    // Component overrides
    void onRender(NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;

private:
    // Window components
    std::shared_ptr<NUICustomTitleBar> titleBar_;
    NUIComponent* content_;
    NUIWindowWin32* windowHandle_;
    
    // Window state
    bool isFullScreen_;
    NUIRect contentArea_;
    
    // Helper methods
    void updateContentArea();
    void setupTitleBarCallbacks();
    void handleWindowDrag(int deltaX, int deltaY);
    void handleWindowMinimize();
    void handleWindowMaximize();
    void handleWindowClose();
};

} // namespace NomadUI
