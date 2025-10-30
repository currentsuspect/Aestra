// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUICustomWindow.h"
#include "../Platform/NUIPlatformBridge.h"
#include "../Graphics/NUIRenderer.h"
#include "../Core/NUIThemeSystem.h"
#include <iostream>

// Windows includes for PostMessageW
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace NomadUI {

NUICustomWindow::NUICustomWindow()
    : NUIComponent()
    , content_(nullptr)
    , windowHandle_(nullptr)
    , isFullScreen_(false)
{
    // Create title bar
    titleBar_ = std::make_shared<NUICustomTitleBar>();
    titleBar_->setTitle("Nomad");
    titleBar_->setBounds(NUIRect(0, 0, 800, 32)); // Set initial bounds
    addChild(titleBar_);
    
    // Setup title bar callbacks
    setupTitleBarCallbacks();
    
    // Set initial size - this will be updated when the window is created
    setSize(800, 600);
    updateContentArea();
}

void NUICustomWindow::setTitle(const std::string& title) {
    titleBar_->setTitle(title);
}

void NUICustomWindow::toggleFullScreen() {
    if (isFullScreen_) {
        exitFullScreen();
    } else {
        enterFullScreen();
    }
}

bool NUICustomWindow::isFullScreen() const {
    return isFullScreen_;
}

void NUICustomWindow::enterFullScreen() {
    if (isFullScreen_ || !windowHandle_) return;
    
    isFullScreen_ = true;
    windowHandle_->enterFullScreen();
    
    // Hide title bar in full screen
    titleBar_->setVisible(false);
    updateContentArea();
}

void NUICustomWindow::exitFullScreen() {
    if (!isFullScreen_ || !windowHandle_) return;
    
    isFullScreen_ = false;
    windowHandle_->exitFullScreen();
    
    // Show title bar when exiting full screen
    titleBar_->setVisible(true);
    
    // Force title bar to update its bounds and visibility
    titleBar_->setBounds(NUIRect(0, 0, getBounds().width, titleBar_->getHeight()));
    titleBar_->onResize(getBounds().width, static_cast<int>(titleBar_->getHeight()));
    
    updateContentArea();
}

void NUICustomWindow::setContent(NUIComponent* content) {
    if (content_) {
        // Find and remove the content component
        auto children = getChildren();
        for (auto it = children.begin(); it != children.end(); ++it) {
            if (it->get() == content_) {
                removeChild(*it);
                break;
            }
        }
    }
    
    content_ = content;
    if (content_) {
        // Add content as a shared_ptr (this is a bit of a hack, but works for now)
        std::shared_ptr<NUIComponent> contentPtr(content, [](NUIComponent*){}); // No-op deleter
        addChild(contentPtr);
        updateContentArea();
    }
}

void NUICustomWindow::setMaximized(bool maximized) {
    titleBar_->setMaximized(maximized);
}

void NUICustomWindow::onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    
    // Get theme colors
    auto& themeManager = NUIThemeManager::getInstance();
    NUIColor bgColor = themeManager.getColor("background");
    
    // Draw window background
    renderer.fillRect(bounds, bgColor);
    
    
    // Render children (title bar and content)
    renderChildren(renderer);
}

void NUICustomWindow::onResize(int width, int height) {
    // Update our bounds
    setBounds(NUIRect(0, 0, width, height));
    
    // Update title bar size (only if not in full screen)
    if (titleBar_ && !isFullScreen_) {
        titleBar_->setBounds(NUIRect(0, 0, width, titleBar_->getHeight()));
        titleBar_->onResize(width, static_cast<int>(titleBar_->getHeight()));
    }
    
    // Update content area based on new size
    updateContentArea();
    
    // Call parent resize
    NUIComponent::onResize(width, height);
}

bool NUICustomWindow::onKeyEvent(const NUIKeyEvent& event) {
    // Handle F11 for full screen toggle
    if (event.pressed && event.keyCode == NUIKeyCode::F11) {
        toggleFullScreen();
        return true;
    }
    
    // Handle Alt+F4 for close
    if (event.pressed && event.keyCode == NUIKeyCode::F4) {
        // Check for Alt modifier (would need to be implemented)
        handleWindowClose();
        return true;
    }
    
    return NUIComponent::onKeyEvent(event);
}

void NUICustomWindow::updateContentArea() {
    NUIRect bounds = getBounds();
    
    if (isFullScreen_) {
        // In full screen, content takes up entire area
        contentArea_ = bounds;
    } else {
        // In windowed mode, content area is below title bar
        float titleHeight = titleBar_->getHeight();
        contentArea_ = NUIRect(0, titleHeight, 
                              bounds.width, bounds.height - titleHeight);
    }
    
    // Update content component bounds
    if (content_) {
        content_->setBounds(contentArea_);
    }
}

void NUICustomWindow::setupTitleBarCallbacks() {
    titleBar_->setOnMinimize([this]() { handleWindowMinimize(); });
    titleBar_->setOnMaximize([this]() { handleWindowMaximize(); });
    titleBar_->setOnClose([this]() { handleWindowClose(); });
    titleBar_->setOnDrag([this](int deltaX, int deltaY) { handleWindowDrag(deltaX, deltaY); });
}

void NUICustomWindow::handleWindowDrag(int deltaX, int deltaY) {
    if (windowHandle_) {
        int x, y;
        windowHandle_->getPosition(x, y);
        windowHandle_->setPosition(x + deltaX, y + deltaY);
    }
}

void NUICustomWindow::handleWindowMinimize() {
    if (windowHandle_) {
        windowHandle_->minimize();
    }
}

void NUICustomWindow::handleWindowMaximize() {
    if (windowHandle_) {
        // Toggle between maximize and restore
        if (windowHandle_->isMaximized()) {
            windowHandle_->restore();
        } else {
            windowHandle_->maximize();
        }
        // Update title bar state
        titleBar_->setMaximized(windowHandle_->isMaximized());
        setDirty(true);
    }
}

void NUICustomWindow::handleWindowClose() {
    if (windowHandle_) {
        // Trigger the window close event
        PostMessageW(static_cast<HWND>(windowHandle_->getNativeHandle()), WM_CLOSE, 0, 0);
    }
}

} // namespace NomadUI
