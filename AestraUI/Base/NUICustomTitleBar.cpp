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
    , membershipTier_("Core")
    , membershipStatus_("Signed out")
    , membershipVerified_(false)
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
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
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
    minimizeIcon_ = std::make_shared<NUIIcon>(minimizeSvg);
    minimizeIcon_->setIconSize(NUIIconSize::Small);
    maximizeIcon_ = std::make_shared<NUIIcon>(maximizeSvg);
    maximizeIcon_->setIconSize(NUIIconSize::Small);
    restoreIcon_ = std::make_shared<NUIIcon>(restoreSvg);
    restoreIcon_->setIconSize(NUIIconSize::Small);
    restoreIcon_->setColorFromTheme("textPrimary");
    closeIcon_ = std::make_shared<NUIIcon>(closeSvg);
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

void NUICustomTitleBar::setMembershipBadge(const std::string& tier, const std::string& status, bool verified) {
    if (membershipTier_ == tier && membershipStatus_ == status && membershipVerified_ == verified) {
        return;
    }
    membershipTier_ = tier.empty() ? "Core" : tier;
    membershipStatus_ = status.empty() ? "Unknown" : status;
    membershipVerified_ = verified;
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
    const auto& props = themeManager.getCurrentTheme();

    // Draw title bar background - flat and minimal, slightly raised over canvas.
    const NUIColor bgColor = themeManager.getColor("backgroundSecondary");
    renderer.fillRect(bounds, bgColor);
    renderer.drawLine({bounds.x, bounds.bottom() - 1.0f},
                      {bounds.right(), bounds.bottom() - 1.0f},
                      1.0f,
                      themeManager.getColor("borderSubtle").withAlpha(0.42f));

    // Draw window controls
    drawWindowControls(renderer);

    const auto text = themeManager.getColor("textPrimary").withAlpha(0.90f);
    const auto muted = themeManager.getColor("textSecondary").withAlpha(0.58f);
    const auto accent = themeManager.getColor("accentPrimary");
    const auto verifiedAccent = themeManager.getColor("success");
    const float userFont = props.fontSizeXS; // 12.0
    const std::string status = membershipStatus_;
    const NUISize statusSize = renderer.measureText(status, userFont);
    constexpr float badgeW = 96.0f;
    constexpr float badgeH = 22.0f;
    constexpr float badgeGapToControls = 12.0f;
    const float badgeY = bounds.y + std::round((height_ - badgeH) * 0.5f);
    const NUIRect badge(minimizeButtonRect_.x - badgeGapToControls - badgeW,
                        badgeY,
                        badgeW,
                        badgeH);
    const float statusX = badge.x - statusSize.width - 14.0f;

    // Progressive collapse for narrow windows. The view toggle is centred on the
    // whole title bar (AestraContent positions it), so on a small window it would
    // collide with this right-hand status/badge cluster. Rather than let them
    // overlap the tabs, hide the least-critical chrome first — the "Signed out"
    // status text, then the Core badge — keeping the view tabs uncrowded. Primary
    // controls never hide. Compare against the toggle's centred right edge.
    const float toggleRightEdge = bounds.x + bounds.width * 0.5f
                                  + props.layout.viewToggleWidth * 0.5f;
    constexpr float kClusterGuard = 16.0f;
    const bool showBadge = badge.x > toggleRightEdge + kClusterGuard;
    const bool showStatus = showBadge && statusX > toggleRightEdge + kClusterGuard;

    if (showStatus) {
        const float sharedBaselineY = std::round(renderer.calculateTextY({statusX, badgeY, statusSize.width, badgeH}, userFont));
        renderer.drawText(status,
                          {statusX, sharedBaselineY},
                          userFont, muted);
    }

    if (showBadge) {
        const float badgeRadius = props.radiusM; // 8.0
        const NUIColor badgeFill = membershipVerified_
            ? accent.withAlpha(0.10f)
            : accent.withAlpha(0.04f);
        const NUIColor badgeStroke = membershipVerified_
            ? accent.withAlpha(0.50f)
            : accent.withAlpha(0.24f);
        renderer.fillRoundedRect(badge, badgeRadius, badgeFill);
        renderer.strokeRoundedRect(badge, badgeRadius, 1.0f, badgeStroke);

        if (membershipVerified_) {
            // Subtle status dot on the leading edge of the badge to reinforce verified state.
            const float dotRadius = 3.0f;
            const NUIPoint dotCenter(badge.x + 10.0f, badge.y + badge.height * 0.5f);
            renderer.fillCircle(dotCenter, dotRadius, verifiedAccent.withAlpha(0.92f));
            renderer.fillCircle(dotCenter, dotRadius * 0.45f, NUIColor::white().withAlpha(0.55f));
        }

        renderer.drawTextCentered(membershipTier_, badge, props.fontSizeS - 2.0f, text.withAlpha(membershipVerified_ ? 0.92f : 0.78f));
    }

    // Render custom children (NUIMenuBar, view toggle, etc.)
    renderChildren(renderer);
}

void NUICustomTitleBar::drawWindowControls(NUIRenderer& renderer) {
    auto& themeManager = NUIThemeManager::getInstance();
    // Use config colors for hover states
    NUIColor hoverBgColor = themeManager.getColor("primary").withAlpha(0.16f);
    NUIColor closeHoverBg = themeManager.getColor("error");
    NUIColor exportColor = themeManager.getColor("accentPrimary");
    NUIColor iconColor = themeManager.getColor("textPrimary").withAlpha(0.94f);

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
        renderer.drawText(pctText, NUIPoint(textX, textY), 9.0f, themeManager.getColor("textPrimary").withAlpha(0.96f));
    }
    
    // Draw minimize button
    if (hoveredButton_ == HoverButton::Minimize) {
        // Rounded hover effect
        renderer.fillRoundedRect(minimizeButtonRect_, 8.0f, hoverBgColor);
    }
    minimizeIcon_->setColor(iconColor);
    NUIPoint minCenter(minimizeButtonRect_.x + minimizeButtonRect_.width * 0.5f,
                       minimizeButtonRect_.y + minimizeButtonRect_.height * 0.5f);
    float iconOffset = 8.0f; // Center the 16px icon
    minimizeIcon_->setPosition(minCenter.x - iconOffset, minCenter.y - iconOffset);
    minimizeIcon_->onRender(renderer);
    
    // Draw maximize/restore button
    if (hoveredButton_ == HoverButton::Maximize) {
        // Rounded hover effect
        renderer.fillRoundedRect(maximizeButtonRect_, 8.0f, hoverBgColor);
    }
    NUIPoint maxCenter(maximizeButtonRect_.x + maximizeButtonRect_.width * 0.5f,
                       maximizeButtonRect_.y + maximizeButtonRect_.height * 0.5f);
    
    // Use appropriate icon based on maximized state and set to white
    auto& maxIcon = isMaximized_ ? restoreIcon_ : maximizeIcon_;
    maxIcon->setColor(iconColor);
    maxIcon->setPosition(maxCenter.x - iconOffset, maxCenter.y - iconOffset);
    maxIcon->onRender(renderer);
    
    // Draw close button with red hover
    if (hoveredButton_ == HoverButton::Close) {
        // Rounded hover effect
        renderer.fillRoundedRect(closeButtonRect_, 8.0f, closeHoverBg.withAlpha(0.88f));
    }
    closeIcon_->setColor(themeManager.getColor("textPrimary").withAlpha(hoveredButton_ == HoverButton::Close ? 1.0f : 0.94f));
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
    
    if (false && isPointInButton(mousePos, exportButtonRect_) && !isExporting_) {
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
        if (false && isPointInButton(mousePos, exportButtonRect_) && !isExporting_) {
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
    float buttonWidth = 34.0f;
    float buttonHeight = 24.0f;
    float buttonY = bounds.y + (height_ - buttonHeight) * 0.5f; // Vertically centered
    float spacing = 4.0f;

    // Position buttons from right edge with padding
    float currentX = bounds.x + bounds.width - buttonWidth - 8.0f;

    closeButtonRect_ = NUIRect(currentX, buttonY, buttonWidth, buttonHeight);
    currentX -= (buttonWidth + spacing);

    maximizeButtonRect_ = NUIRect(currentX, buttonY, buttonWidth, buttonHeight);
    currentX -= (buttonWidth + spacing);

    minimizeButtonRect_ = NUIRect(currentX, buttonY, buttonWidth, buttonHeight);
    currentX -= (buttonWidth + spacing + 10.0f);

    // Export button (left of minimize)
    exportButtonRect_ = NUIRect(currentX, buttonY, 24.0f, buttonHeight);
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
