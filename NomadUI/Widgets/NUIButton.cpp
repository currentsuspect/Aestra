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
    auto theme = getTheme();
    if (!theme) {
        // Fallback rendering without theme
        renderer.fillRect(getBounds(), NUIColor::fromHex(0x333333));
        return;
    }
    
    auto bounds = getBounds();
    float radius = theme->getBorderRadius();
    
    // Get colors
    NUIColor bgColor = getCurrentBackgroundColor();
    NUIColor textColor = getCurrentTextColor();
    
    // OPTIMIZATION: Disable glow by default (expensive blur effect)
    // Only enable for specific buttons that need it
    // Draw glow effect when hovered
    // if (glowEnabled_ && hoverAlpha_ > 0.01f) {
    //     renderer.drawGlow(
    //         bounds,
    //         radius * 3.0f,
    //         hoverAlpha_ * theme->getGlowIntensity(),
    //         theme->getPrimary()
    //     );
    // }
    
    // Draw background
    renderer.fillRoundedRect(bounds, radius, bgColor);
    
    // Draw border (only if enabled and state changed)
    if (borderEnabled_) {
        NUIColor borderColor = theme->getPrimary();
        if (pressed_) {
            borderColor = borderColor.withBrightness(1.2f);
        } else if (isHovered()) {
            borderColor = borderColor.withBrightness(1.1f);
        }
        
        renderer.strokeRoundedRect(
            bounds,
            radius,
            theme->getBorderWidth(),
            borderColor
        );
    }
    
    // Draw text
    float textSize = fontSize_ > 0.0f ? fontSize_ : theme->getFontSizeNormal();
    renderer.drawTextCentered(text_, bounds, textSize, textColor);
    
    // Render children
    NUIComponent::onRender(renderer);
}

void NUIButton::onUpdate(double deltaTime) {
    // Animate hover effect
    float targetAlpha = isHovered() ? 1.0f : 0.0f;
    float speed = 8.0f; // Animation speed
    
    if (hoverAlpha_ < targetAlpha) {
        hoverAlpha_ += speed * deltaTime;
        if (hoverAlpha_ > targetAlpha) {
            hoverAlpha_ = targetAlpha;
        }
        setDirty();
    } else if (hoverAlpha_ > targetAlpha) {
        hoverAlpha_ -= speed * deltaTime;
        if (hoverAlpha_ < targetAlpha) {
            hoverAlpha_ = targetAlpha;
        }
        setDirty();
    }
    
    NUIComponent::onUpdate(deltaTime);
}

bool NUIButton::onMouseEvent(const NUIMouseEvent& event) {
    if (!isEnabled()) {
        return false;
    }
    
    // Check if event is within bounds
    if (!containsPoint(event.position)) {
        if (pressed_) {
            pressed_ = false;
            setDirty();
        }
        return false;
    }
    
    // Handle mouse down
    if (event.pressed && event.button == NUIMouseButton::Left) {
        pressed_ = true;
        setDirty();
        return true;
    }
    
    // Handle mouse up (click)
    if (event.released && event.button == NUIMouseButton::Left) {
        if (pressed_) {
            pressed_ = false;
            setDirty();
            
            // Trigger click callback
            if (onClick_) {
                onClick_();
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
    
    if (useCustomColors_) {
        if (pressed_) {
            return pressedColor_;
        } else if (isHovered()) {
            return hoverColor_;
        }
        return backgroundColor_;
    }
    
    if (!theme) {
        return NUIColor::fromHex(0x333333);
    }
    
    if (!isEnabled()) {
        return theme->getDisabled();
    }
    
    if (pressed_) {
        return theme->getActive();
    }
    
    if (isHovered()) {
        return theme->getHover();
    }
    
    return theme->getSurface();
}

NUIColor NUIButton::getCurrentTextColor() const {
    auto theme = getTheme();
    
    if (useCustomColors_) {
        return textColor_;
    }
    
    if (!theme) {
        return NUIColor::white();
    }
    
    if (!isEnabled()) {
        return theme->getColor("textDisabled");
    }
    
    return theme->getText();
}

} // namespace NomadUI
