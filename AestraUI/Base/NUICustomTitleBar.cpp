// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUICustomTitleBar.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "../../AestraCore/include/AestraLog.h"
#include "../../AestraCore/include/AestraUnifiedProfiler.h"

#include <cmath>
#include <string>

namespace AestraUI {

NUICustomTitleBar::NUICustomTitleBar()
    : NUIComponent()
    , title_("Aestra")
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

    // Export button rect (will be positioned in updateButtonRects)
    exportButtonRect_ = NUIRect(0, 0, 28.0f, 28.0f);
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

void NUICustomTitleBar::setExportProgress(float progress) {
    exportProgress_ = progress;
    exportAnimating_ = (progress < 0.0f);
    setDirty(true);
}

void NUICustomTitleBar::setExporting(bool exporting) {
    isExporting_ = exporting;
    if (!exporting) {
        exportProgress_ = 0.0f;
        exportAnimating_ = false;
    }
    setDirty(true);
}

void NUICustomTitleBar::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    if (isExporting_ && exportAnimating_) {
        exportAnimPhase_ += static_cast<float>(deltaTime) * 3.0f;
        if (exportAnimPhase_ > 6.28318f) exportAnimPhase_ -= 6.28318f;
        setDirty(true);
    }
}

void NUICustomTitleBar::onRender(NUIRenderer& renderer) {
    AESTRA_ZONE("TitleBar_Render");
    NUIRect bounds = getBounds();
    
    // Get theme colors
    auto& themeManager = NUIThemeManager::getInstance();
    NUIColor bgColor = themeManager.getColor("background"); // Use background color for flush look
    
    // Draw title bar background - flush with window background
    renderer.fillRect(bounds, bgColor);
    
    // No separator line for clean, flush appearance
    // Menu items are now handled by NUIMenuBar child component
    
    // Draw window controls
    drawWindowControls(renderer);
    
    // Render custom children (NUIMenuBar, view toggle, etc.)
    renderChildren(renderer);
}

void NUICustomTitleBar::drawWindowControls(NUIRenderer& renderer) {
    auto& themeManager = NUIThemeManager::getInstance();
    // Use config colors for hover states
    NUIColor hoverBgColor = themeManager.getColor("primary").withAlpha(0.2f); // Aestra Purple hover
    NUIColor closeHoverBg = themeManager.getColor("error"); // Red for close button
    NUIColor exportColor = themeManager.getColor("accentPrimary"); // Purple for export

    // Draw export button (left of window controls)
    if (isExporting_) {
        // Animated progress bar inside export button
        if (exportAnimating_) {
            // Indeterminate: sliding bar animation
            float phase = std::sin(exportAnimPhase_) * 0.5f + 0.5f;
            float barWidth = exportButtonRect_.width * 0.4f;
            float barX = exportButtonRect_.x + (exportButtonRect_.width - barWidth) * phase;
            renderer.fillRoundedRect(exportButtonRect_, 4.0f, exportColor.withAlpha(0.15f));
            renderer.fillRoundedRect(NUIRect(barX, exportButtonRect_.y + 4.0f, barWidth, exportButtonRect_.height - 8.0f), 3.0f, exportColor.withAlpha(0.7f));
        } else if (exportProgress_ >= 0.0f) {
            // Determinate progress
            renderer.fillRoundedRect(exportButtonRect_, 4.0f, exportColor.withAlpha(0.15f));
            float filledWidth = std::max(2.0f, exportButtonRect_.width * exportProgress_);
            renderer.fillRoundedRect(NUIRect(exportButtonRect_.x + 2.0f, exportButtonRect_.y + 4.0f,
                                             filledWidth - 4.0f, exportButtonRect_.height - 8.0f), 3.0f, exportColor.withAlpha(0.7f));
        }
        // Show percentage text
        int pct = static_cast<int>(exportProgress_ * 100.0f);
        std::string pctText = std::to_string(pct) + "%";
        auto textSize = renderer.measureText(pctText, 9.0f);
        float textX = exportButtonRect_.x + (exportButtonRect_.width - textSize.width) * 0.5f;
        float textY = exportButtonRect_.y + (exportButtonRect_.height - 9.0f) * 0.5f;
        renderer.drawText(pctText, NUIPoint(textX, textY), 9.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.9f));
    } else {
        // Export button: download/upload icon
        if (exportHovered_) {
            renderer.fillRoundedRect(exportButtonRect_, 4.0f, hoverBgColor);
        }
        // Draw a simple "export" icon (arrow pointing up from a line)
        float cx = exportButtonRect_.x + exportButtonRect_.width * 0.5f;
        float cy = exportButtonRect_.y + exportButtonRect_.height * 0.5f;
        float iconSize = 10.0f;
        // Arrow up
        renderer.drawLine(NUIPoint(cx, cy - iconSize * 0.5f), NUIPoint(cx, cy + iconSize * 0.3f), 1.5f, NUIColor(1.0f, 1.0f, 1.0f, 0.8f));
        // Arrow head
        renderer.drawLine(NUIPoint(cx - 4.0f, cy - iconSize * 0.15f), NUIPoint(cx, cy - iconSize * 0.5f), 1.5f, NUIColor(1.0f, 1.0f, 1.0f, 0.8f));
        renderer.drawLine(NUIPoint(cx + 4.0f, cy - iconSize * 0.15f), NUIPoint(cx, cy - iconSize * 0.5f), 1.5f, NUIColor(1.0f, 1.0f, 1.0f, 0.8f));
        // Base line
        renderer.drawLine(NUIPoint(cx - 5.0f, cy + iconSize * 0.35f), NUIPoint(cx + 5.0f, cy + iconSize * 0.35f), 1.5f, NUIColor(1.0f, 1.0f, 1.0f, 0.8f));
    }
    
    // Draw minimize button
    if (hoveredButton_ == HoverButton::Minimize) {
        // Rounded hover effect
        renderer.fillRoundedRect(minimizeButtonRect_, 4.0f, hoverBgColor);
    }
    minimizeIcon_->setColor(NUIColor(1.0f, 1.0f, 1.0f, 1.0f)); // White
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
    
    // Use appropriate icon based on maximized state and set to white
    auto& maxIcon = isMaximized_ ? restoreIcon_ : maximizeIcon_;
    maxIcon->setColor(NUIColor(1.0f, 1.0f, 1.0f, 1.0f)); // White
    maxIcon->setPosition(maxCenter.x - iconOffset, maxCenter.y - iconOffset);
    maxIcon->onRender(renderer);
    
    // Draw close button with red hover
    if (hoveredButton_ == HoverButton::Close) {
        // Rounded hover effect
        renderer.fillRoundedRect(closeButtonRect_, 4.0f, closeHoverBg);
    }
    closeIcon_->setColor(NUIColor(1.0f, 1.0f, 1.0f, 1.0f)); // Always white
    NUIPoint closeCenter(closeButtonRect_.x + closeButtonRect_.width * 0.5f,
                         closeButtonRect_.y + closeButtonRect_.height * 0.5f);
    closeIcon_->setPosition(closeCenter.x - iconOffset, closeCenter.y - iconOffset);
    closeIcon_->onRender(renderer);
}

bool NUICustomTitleBar::onMouseEvent(const NUIMouseEvent& event) {
    NUIPoint mousePos = event.position;
    
    // Let children handle events first (NUIMenuBar, view toggle, etc.)
    if (NUIComponent::onMouseEvent(event)) {
        return true;
    }
    
    // Update hover state for window controls
    HoverButton previousHover = hoveredButton_;
    hoveredButton_ = HoverButton::None;
    
    bool previousExportHover = exportHovered_;
    exportHovered_ = false;
    
    if (isPointInButton(mousePos, exportButtonRect_) && !isExporting_) {
        exportHovered_ = true;
    } else if (isPointInButton(mousePos, minimizeButtonRect_)) {
        hoveredButton_ = HoverButton::Minimize;
    } else if (isPointInButton(mousePos, maximizeButtonRect_)) {
        hoveredButton_ = HoverButton::Maximize;
    } else if (isPointInButton(mousePos, closeButtonRect_)) {
        hoveredButton_ = HoverButton::Close;
    }
    
    // Mark dirty if hover state changed
    if (previousHover != hoveredButton_ || previousExportHover != exportHovered_) {
        setDirty(true);
    }
    
    if (event.pressed && event.button == NUIMouseButton::Left) {
        // Check export button first
        if (isPointInButton(mousePos, exportButtonRect_) && !isExporting_) {
            if (onExportRequested_) onExportRequested_();
            return true;
        }
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
    }
    
    return false;
    
    return false;
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
    currentX -= (buttonWidth + spacing + 8.0f);

    // Export button (left of minimize)
    exportButtonRect_ = NUIRect(currentX, buttonY, 28.0f, buttonHeight);
}

bool NUICustomTitleBar::isPointInButton(const NUIPoint& point, const NUIRect& buttonRect) {
    return point.x >= buttonRect.x && point.x <= buttonRect.x + buttonRect.width &&
           point.y >= buttonRect.y && point.y <= buttonRect.y + buttonRect.height;
}

void NUICustomTitleBar::handleButtonClick(const NUIRect& buttonRect) {
    // Visual feedback for button click
    setDirty(true);
}

Aestra::HitTestResult NUICustomTitleBar::hitTest(const NUIPoint& point) {
    // 1. Check Window Controls
    // We return Client because NUICustomTitleBar handles the clicks/hover itself in onMouseEvent.
    // Returning Caption or HTCLOSE would trigger Windows default handling which we don't want for custom drawn buttons.
    if (isPointInButton(point, minimizeButtonRect_)) return Aestra::HitTestResult::Client;
    if (isPointInButton(point, maximizeButtonRect_)) return Aestra::HitTestResult::Client;
    if (isPointInButton(point, closeButtonRect_)) return Aestra::HitTestResult::Client;
    
    // 2. Check Child Components (Menu Bar, Mode Toggle, etc.)
    // Recursively check if point hits any interactive child
    const auto& children = getChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        auto& child = *it;
        if (child->isVisible() && child->getBounds().contains(point)) {
            return Aestra::HitTestResult::Client;
        }
    }
    
    // 3. Fallback to Caption (Drag area)
    return Aestra::HitTestResult::Caption;
}

} // namespace AestraUI
