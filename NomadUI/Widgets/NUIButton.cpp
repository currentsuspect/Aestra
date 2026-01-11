#include "NUIButton.h"
#include "../Core/NUITheme.h"
#include <algorithm>

namespace NomadUI {

NUIButton::NUIButton() : NUIButton("Button") {
}

NUIButton::NUIButton(const std::string& text) : text_(text) {
}

// ============================================================================
// Configuration
// ============================================================================

void NUIButton::setStyle(Style style) {
    style_ = style;
    
    // Reset to defaults first
    resetColors();
    borderEnabled_ = true;
    
    switch (style) {
        case Style::Primary:
            break;
            
        case Style::Secondary:
            borderEnabled_ = true;
            break;
            
        case Style::Text:
        case Style::Icon:
            borderEnabled_ = false;
            break;
    }
    setDirty();
}

void NUIButton::setText(const std::string& text) {
    if (text_ != text) {
        text_ = text;
        setDirty();
    }
}

// ============================================================================
// Component Overrides
// ============================================================================

void NUIButton::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;

    auto theme = getTheme();
    
    // Check custom color flags
    NUIColor backgroundColor = getCurrentBackgroundColor();
    NUIColor borderColor;
    
    // Determine border color
    if (pressed_ || isHovered()) {
        borderColor = backgroundColor;
    } else {
        borderColor = theme ? theme->getBorder() : NUIColor::fromHex(0x555555);
    }
    
    auto bounds = getBounds();
    float radius = cornerRadius_ >= 0.0f ? cornerRadius_ : (theme ? theme->getBorderRadius() : 4.0f);

    // ANIMATION: "Squish" effect on press
    if (pressed_) {
        float scale = 0.96f;
        float cx = bounds.x + bounds.width * 0.5f;
        float cy = bounds.y + bounds.height * 0.5f;
        
        float newW = bounds.width * scale;
        float newH = bounds.height * scale;
        
        bounds.x = cx - newW * 0.5f;
        bounds.y = cy - newH * 0.5f;
        bounds.width = newW;
        bounds.height = newH;
    }
    
    // Create render rect for background
    // If background relies on flat design logic, we should be careful.
    bool shouldDrawBackground = isHovered() || pressed_ || hasCustomBg_;
    
    if (shouldDrawBackground) {
        NUIColor drawColor = backgroundColor;
        
        // Refined hover style for default buttons (not custom)
        // FIX: Only apply the 0.15f alpha reduction if we are using the THEME hover color.
        // If the user set a custom hover color (like in TrackUIComponent or WindowPanel), utilize it as is.
        if (!hasCustomBg_ && isHovered() && !pressed_ && !hasCustomHover_) {
            drawColor = drawColor.withAlpha(0.15f); 
        }
        
        renderer.fillRoundedRect(bounds, radius, drawColor);
    }
    
    // Draw border
    if (borderEnabled_) {
        // Adjust border color if theme is present
        if (theme) {
             borderColor = theme->getPrimary();
             if (pressed_) borderColor = borderColor.withBrightness(1.2f);
             else if (isHovered()) borderColor = borderColor.withBrightness(1.1f);
        }
        
        float borderWidth = theme ? theme->getBorderWidth() : 1.0f;
        // Inset stroke
        NUIRect strokeRect = bounds;
        strokeRect.x += borderWidth * 0.5f;
        strokeRect.y += borderWidth * 0.5f;
        strokeRect.width -= borderWidth;
        strokeRect.height -= borderWidth;
        float strokeRadius = std::max(0.0f, radius - borderWidth * 0.5f);
        
        renderer.strokeRoundedRect(strokeRect, strokeRadius, borderWidth, borderColor);
    }
    
    // Draw text
    float fontSize = fontSize_ > 0.0f ? fontSize_ : (theme ? theme->getFontSizeNormal() : 12.0f);
    NUIColor textColor = getCurrentTextColor();
    
    renderer.drawTextCentered(text_, bounds, fontSize, textColor);
    
    // Render children
    renderChildren(renderer); // Using NUIComponent helper
}

void NUIButton::onUpdate(double deltaTime) {
    // Animate hover effect
    float targetAlpha = isHovered() ? 1.0f : 0.0f;
    float speed = 8.0f; 
    
    if (hoverAlpha_ < targetAlpha) {
        hoverAlpha_ += speed * deltaTime;
        if (hoverAlpha_ > targetAlpha) hoverAlpha_ = targetAlpha;
        setDirty();
    } else if (hoverAlpha_ > targetAlpha) {
        hoverAlpha_ -= speed * deltaTime;
        if (hoverAlpha_ < targetAlpha) hoverAlpha_ = targetAlpha;
        setDirty();
    }
    
    NUIComponent::onUpdate(deltaTime);
}

bool NUIButton::onMouseEvent(const NUIMouseEvent& event) {
    if (!isEnabled()) return false;
    
    // CRITICAL: Call base class to handle hover state and callbacks (onMouseMove, etc.)
    // This allows parents to use onMouseMove for forced repaints when buttons are hovered.
    NUIComponent::onMouseEvent(event);
    
    if (!containsPoint(event.position)) {
        if (pressed_) {
            pressed_ = false;
            setDirty();
        }
        return false;
    }
    
    if (event.pressed && event.button == NUIMouseButton::Left) {
        pressed_ = true;
        setDirty();
        return true;
    }
    
    if (event.released && event.button == NUIMouseButton::Left) {
        if (pressed_) {
            pressed_ = false;
            setDirty();
            
            if (toggleable_) {
                toggled_ = !toggled_;
                if (onToggle_) onToggle_(toggled_);
            } else {
                if (onClick_) onClick_();
            }
        }
        return true;
    }
    
    return false;
}

// ============================================================================
// Private Helpers
// ============================================================================

NUIColor NUIButton::getCurrentBackgroundColor() const {
    auto theme = getTheme();
    
    if (pressed_ && hasCustomPressed_) return pressedColor_;
    if (isHovered() && hasCustomHover_) return hoverColor_;
    if (hasCustomBg_) return backgroundColor_;
    
    // Default Style Behavior
    // Text, Icon and Secondary styles should be transparent by default unless hovered/pressed state overrides
    if (style_ == Style::Text || style_ == Style::Icon || style_ == Style::Secondary) {
       if (!pressed_ && !isHovered()) {
           return NUIColor::transparent();
       }
    }

    if (!theme) return NUIColor::fromHex(0x333333);
    
    if (!isEnabled()) return theme->getDisabled();
    if (pressed_) return theme->getActive();
    if (isHovered()) return theme->getHover();
    return theme->getSurface();
}

NUIColor NUIButton::getCurrentTextColor() const {
    auto theme = getTheme();
    
    if (hasCustomText_) return textColor_;
    
    if (!theme) return NUIColor::white();
    if (!isEnabled()) return theme->getColor("textDisabled", NUIColor::fromHex(0x888888));
    
    // For primary style, text should verify contrast against surface
    if (style_ == Style::Primary) {
        // usually white works best on primary colors
        return NUIColor::white(); 
    }
    
    return theme->getText();
}

} // namespace NomadUI
