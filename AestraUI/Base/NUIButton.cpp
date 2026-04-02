#include "NUIButton.h"
#include "NUITheme.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

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
    const bool active = isEnabled() && (pressed_ || (toggleable_ && toggled_));
    
    // Check custom color flags
    NUIColor backgroundColor = getCurrentBackgroundColor();
    NUIColor borderColor;
    
    // Determine border color
    if (active || isHovered()) {
        borderColor = backgroundColor;
    } else {
        borderColor = theme ? theme->getBorder() : NUIColor::fromHex(0x555555);
    }
    
    auto bounds = getBounds();
    float radius = cornerRadius_ >= 0.0f ? cornerRadius_ : (theme ? theme->getBorderRadius() : 4.0f);
    NUIRect visualRect{
        std::floor(bounds.x) + 0.5f,
        std::floor(bounds.y) + 0.5f,
        std::max(1.0f, std::floor(bounds.width) - 1.0f),
        std::max(1.0f, std::floor(bounds.height) - 1.0f)
    };

    // Keep button geometry stable; visual press is conveyed by fill/border change.
    
    // Create render rect for background
    // If background relies on flat design logic, we should be careful.
    bool shouldDrawBackground = isHovered() || active || hasCustomBg_ || style_ == Style::Primary;
    
    if (shouldDrawBackground) {
        NUIColor drawColor = backgroundColor;
        
        // Refined hover style for default buttons (not custom)
        // FIX: Only apply the 0.15f alpha reduction if we are using the THEME hover color.
        // If the user set a custom hover color (like in TrackUIComponent or WindowPanel), utilize it as is.
        if (!hasCustomBg_ && isHovered() && !active && !hasCustomHover_) {
            drawColor = drawColor.withAlpha(std::max(0.18f, drawColor.a));
        }
        
        const bool raisedButton = style_ != Style::Text && style_ != Style::Icon;
        if (raisedButton) {
            renderer.drawShadow(visualRect, 0.0f, 6.0f, 16.0f, NUIColor(0, 0, 0, active ? 0.12f : 0.20f));
        }
        renderer.fillRoundedRect(visualRect, radius, drawColor);
        if (!hasCustomBg_) {
            NUIRect sheen = visualRect;
            sheen.height = std::max(1.0f, visualRect.height * 0.42f);
            renderer.fillRoundedRect(sheen, radius, NUIColor::white().withAlpha(active ? 0.03f : 0.05f));
        }
    }
    
    // Draw border
    if (borderEnabled_) {
        // Adjust border color
        if (hasCustomBorderColor_) {
             borderColor = borderColor_;
             // Optionally brighten on interaction if desired, but custom usually implies "exact"
             if (active) borderColor = borderColor.lightened(0.2f);
             else if (isHovered()) borderColor = borderColor.lightened(0.1f);
        } else if (theme) {
             borderColor = theme->getBorder();
             if (active) borderColor = theme->getColor("borderActive").withAlpha(0.9f);
             else if (isHovered()) borderColor = theme->getColor("borderActive").withAlpha(0.55f);
             else borderColor = borderColor.withAlpha(0.85f);
        } else {
             borderColor = NUIColor::fromHex(0x555555);
        }
        
        float borderWidth = hasCustomBorderWidth_ ? borderWidth_ : (theme ? theme->getBorderWidth() : 1.0f);
        // Inset stroke
        NUIRect strokeRect = visualRect;
        strokeRect.x += borderWidth * 0.5f;
        strokeRect.y += borderWidth * 0.5f;
        strokeRect.width -= borderWidth;
        strokeRect.height -= borderWidth;
        float strokeRadius = std::max(0.0f, radius - borderWidth * 0.5f);
        
        renderer.strokeRoundedRect(strokeRect, strokeRadius, borderWidth, borderColor);
        if (style_ != Style::Text && style_ != Style::Icon) {
            NUIRect innerStroke = strokeRect;
            innerStroke.x += 1.0f;
            innerStroke.y += 1.0f;
            innerStroke.width -= 2.0f;
            innerStroke.height -= 2.0f;
            if (innerStroke.width > 0.0f && innerStroke.height > 0.0f) {
                renderer.strokeRoundedRect(innerStroke,
                                           std::max(0.0f, strokeRadius - 1.0f),
                                           1.0f,
                                           NUIColor::white().withAlpha(active ? 0.015f : 0.04f));
            }
        }
    }
    
    // Draw text
    float fontSize = fontSize_ > 0.0f ? fontSize_ : (theme ? theme->getFontSizeNormal() : 12.0f);
    NUIColor textColor = getCurrentTextColor();
    
    renderer.drawTextCentered(text_, visualRect, fontSize, textColor);
    
    // Render children
    renderChildren(renderer); // Using NUIComponent helper
}

void NUIButton::onUpdate(double deltaTime) {
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
    const bool active = isEnabled() && (pressed_ || (toggleable_ && toggled_));
    
    if (active && hasCustomPressed_) return pressedColor_;
    if (isHovered() && hasCustomHover_) return hoverColor_;
    if (hasCustomBg_) return backgroundColor_;
    
    // Default Style Behavior
    // Text, Icon and Secondary styles should be transparent by default unless hovered/pressed state overrides
    if (style_ == Style::Text || style_ == Style::Icon || style_ == Style::Secondary) {
       if (!active && !isHovered()) {
           return NUIColor::transparent();
       }
    }

    if (!theme) return NUIColor::fromHex(0x333333);
    
    if (!isEnabled()) return theme->getDisabled();
    
    // Style-specific colors
    if (style_ == Style::Primary) {
        if (active) return theme->getColor("primaryPressed", theme->getPrimary().darkened(0.1f));
        if (isHovered()) return theme->getColor("primaryHover", theme->getPrimary().lightened(0.1f));
        return theme->getPrimary();
    }
    
    // Default / Secondary Styles
    if (active) return theme->getActive();
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

} // namespace AestraUI
