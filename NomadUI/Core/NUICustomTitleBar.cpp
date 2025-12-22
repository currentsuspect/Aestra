// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUICustomTitleBar.h"
#include "../Graphics/NUIRenderer.h"
#include "../Core/NUIThemeSystem.h"
#include <iostream>
#include <cmath>

namespace NomadUI {

NUICustomTitleBar::NUICustomTitleBar()
    : NUIComponent()
    , title_("Nomad")
    , height_(32.0f)
    , isMaximized_(false)
    , hoveredButton_(HoverButton::None)
    , isDragging_(false)
    , dragStartPos_(0, 0)
    , windowStartPos_(0, 0)
{
    setId("titleBar"); // Set ID for debugging
    setSize(800, height_); // Default width, will be updated by parent
    createIcons();
    updateButtonRects();
}

void NUICustomTitleBar::createIcons() {
    // Create window control icons using the NUIIcon system
    minimizeIcon_ = NUIIcon::createMinimizeIcon();
    minimizeIcon_->setIconSize(NUIIconSize::Small);
    
    maximizeIcon_ = NUIIcon::createMaximizeIcon();
    maximizeIcon_->setIconSize(NUIIconSize::Small);
    
    // Create a restore icon (two overlapping squares)
    const char* restoreSvg = R"(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <rect x="8" y="8" width="13" height="13" rx="2" ry="2"/>
            <path d="M3 16V5a2 2 0 0 1 2-2h11"/>
        </svg>
    )";
    restoreIcon_ = std::make_shared<NUIIcon>(restoreSvg);
    restoreIcon_->setIconSize(NUIIconSize::Small);
    restoreIcon_->setColorFromTheme("textPrimary");
    
    closeIcon_ = NUIIcon::createCloseIcon();
    closeIcon_->setIconSize(NUIIconSize::Small);

    // App icon removed for minimal header (Ableton-style)
    appIcon_.reset();
}

void NUICustomTitleBar::setMaximized(bool maximized) {
    isMaximized_ = maximized;
    setDirty(true);
}

void NUICustomTitleBar::setTitle(const std::string& title) {
    title_ = title;
    setDirty(true);
}

void NUICustomTitleBar::setHeight(float height) {
    height_ = height;
    setSize(getBounds().width, height);
    updateButtonRects();
    setDirty(true);
}

void NUICustomTitleBar::onRender(NUIRenderer& renderer) {
    NUIRect bounds = getBounds();
    
    
    // Get theme colors
    auto& themeManager = NUIThemeManager::getInstance();
    NUIColor bgColor = themeManager.getColor("background"); // Use background color for flush look
    NUIColor textColor = themeManager.getColor("text");
    
    // Draw title bar background - flush with window background
    renderer.fillRect(bounds, bgColor);
    
    // No separator line for clean, flush appearance
    
    // Minimal left-aligned menu labels (Ableton-style)
    float fontSize = 12.0f;
    std::vector<std::string> menuItems = {"File", "Edit", "View"};
    float x = bounds.x + 10.0f;
    for (const auto& item : menuItems) {
        NUISize sz = renderer.measureText(item, fontSize);
        float textY = std::round(renderer.calculateTextY(bounds, fontSize));
        renderer.drawText(item, NUIPoint(x, textY), fontSize, textColor);
        x += sz.width + 14.0f; // spacing between menu items
    }
    
    // Draw window controls
    drawWindowControls(renderer);
}

void NUICustomTitleBar::drawWindowControls(NUIRenderer& renderer) {
    auto& themeManager = NUIThemeManager::getInstance();
    // Use config colors for hover states
    NUIColor hoverBgColor = themeManager.getColor("surfaceRaised"); // Subtle hover
    NUIColor closeHoverBg = themeManager.getColor("error"); // Red for close button
    
    // Draw minimize button
    if (hoveredButton_ == HoverButton::Minimize) {
        // Rounded hover effect
        renderer.fillRoundedRect(minimizeButtonRect_, 4.0f, hoverBgColor);
    }
    NUIPoint minCenter(minimizeButtonRect_.x + minimizeButtonRect_.width * 0.5f,
                       minimizeButtonRect_.y + minimizeButtonRect_.height * 0.5f);
    float iconOffset = 8.0f; // Center the 16px icon
    minimizeIcon_->setPosition(minCenter.x - iconOffset, minCenter.y - iconOffset);
    minimizeIcon_->onRender(renderer);
    
    // Draw maximize/restore button
    if (hoveredButton_ == HoverButton::Maximize) {
        // Rounded hover effect
        renderer.fillRoundedRect(maximizeButtonRect_, 4.0f, hoverBgColor);
    }
    NUIPoint maxCenter(maximizeButtonRect_.x + maximizeButtonRect_.width * 0.5f,
                       maximizeButtonRect_.y + maximizeButtonRect_.height * 0.5f);
    
    // Use appropriate icon based on maximized state
    auto& maxIcon = isMaximized_ ? restoreIcon_ : maximizeIcon_;
    maxIcon->setPosition(maxCenter.x - iconOffset, maxCenter.y - iconOffset);
    maxIcon->onRender(renderer);
    
    // Draw close button with red hover
    if (hoveredButton_ == HoverButton::Close) {
        // Rounded hover effect
        renderer.fillRoundedRect(closeButtonRect_, 4.0f, closeHoverBg);
        closeIcon_->setColor(NUIColor(1.0f, 1.0f, 1.0f, 1.0f)); // White on red
    } else {
        closeIcon_->setColorFromTheme("textPrimary");
    }
    NUIPoint closeCenter(closeButtonRect_.x + closeButtonRect_.width * 0.5f,
                         closeButtonRect_.y + closeButtonRect_.height * 0.5f);
    closeIcon_->setPosition(closeCenter.x - iconOffset, closeCenter.y - iconOffset);
    closeIcon_->onRender(renderer);
}

bool NUICustomTitleBar::onMouseEvent(const NUIMouseEvent& event) {
    NUIPoint mousePos(event.position.x, event.position.y);
    
    // Update hover state
    HoverButton previousHover = hoveredButton_;
    hoveredButton_ = HoverButton::None;
    
    if (isPointInButton(mousePos, minimizeButtonRect_)) {
        hoveredButton_ = HoverButton::Minimize;
    } else if (isPointInButton(mousePos, maximizeButtonRect_)) {
        hoveredButton_ = HoverButton::Maximize;
    } else if (isPointInButton(mousePos, closeButtonRect_)) {
        hoveredButton_ = HoverButton::Close;
    }
    
    // Mark dirty if hover state changed
    if (previousHover != hoveredButton_) {
        setDirty(true);
    }
    
    if (event.pressed && event.button == NUIMouseButton::Left) {
        // Check if clicking on window controls
        if (isPointInButton(mousePos, minimizeButtonRect_)) {
            handleButtonClick(minimizeButtonRect_);
            if (onMinimize_) onMinimize_();
            return true;
        }
        else if (isPointInButton(mousePos, maximizeButtonRect_)) {
            handleButtonClick(maximizeButtonRect_);
            if (onMaximize_) onMaximize_();
            return true;
        }
        else if (isPointInButton(mousePos, closeButtonRect_)) {
            handleButtonClick(closeButtonRect_);
            if (onClose_) onClose_();
            return true;
        }
        // Window dragging is now handled by Windows via WM_NCHITTEST
        // No need to handle it here
    }
    
    return NUIComponent::onMouseEvent(event);
}

void NUICustomTitleBar::onResize(int width, int height) {
    // Update our size
    setSize(width, height_);
    
    // Update button positions
    updateButtonRects();
    
    // Call parent resize
    NUIComponent::onResize(width, height);
}

void NUICustomTitleBar::updateButtonRects() {
    NUIRect bounds = getBounds();
    
    // Button dimensions - Smaller, more spaced out (Ableton/FL style)
    float buttonWidth = 40.0f;
    float buttonHeight = 28.0f; // Slightly shorter than bar
    float buttonY = bounds.y + (height_ - buttonHeight) * 0.5f; // Vertically centered
    float spacing = 4.0f;
    
    // Position buttons from right edge with padding
    float currentX = bounds.x + bounds.width - buttonWidth - 6.0f;
    
    closeButtonRect_ = NUIRect(currentX, buttonY, buttonWidth, buttonHeight);
    currentX -= (buttonWidth + spacing);
    
    maximizeButtonRect_ = NUIRect(currentX, buttonY, buttonWidth, buttonHeight);
    currentX -= (buttonWidth + spacing);
    
    minimizeButtonRect_ = NUIRect(currentX, buttonY, buttonWidth, buttonHeight);
}

bool NUICustomTitleBar::isPointInButton(const NUIPoint& point, const NUIRect& buttonRect) {
    return point.x >= buttonRect.x && point.x <= buttonRect.x + buttonRect.width &&
           point.y >= buttonRect.y && point.y <= buttonRect.y + buttonRect.height;
}

void NUICustomTitleBar::handleButtonClick(const NUIRect& buttonRect) {
    // Visual feedback for button click
    setDirty(true);
}

} // namespace NomadUI
