#include "NUIButton.h"
#include "../Graphics/NUIDepth.h"
#include "NUITheme.h"
#include "NUIThemeSystem.h"
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

void NUIButton::setLook(Look look, Tone tone, HoverStrength hover) {
    look_ = look;
    tone_ = tone;
    hoverStrength_ = hover;
    if (look != Look::Legacy)
        cornerRadius_ = -1.0f;
    setDirty();
}

namespace {
float easeOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}
} // namespace

void NUIButton::renderSpecLook(NUIRenderer& renderer) {
    // Design Spec, Buttons (locked 12 Nov 2026). Values from the spec's .ak-btn.
    const auto& t = NUIThemeManager::getInstance().getCurrentTheme();
    const auto b = getBounds();
    NUIRect r{std::floor(b.x), std::floor(b.y), std::floor(b.width), std::floor(b.height)};
    const float radius = r.height * 0.5f;
    const float fontSize = fontSize_ > 0.0f ? fontSize_ : (r.height <= 26.0f ? t.fontSizeS : t.fontSizeM);

    // tc: the tone; ton: what reads on it; tt: the tone as text.
    NUIColor tc = t.textPrimary, ton = t.backgroundPrimary, tt = t.textPrimary;
    if (tone_ == Tone::Accent) {
        tc = t.primary;
        ton = t.textOnPrimary;
        tt = t.secondary;
    } else if (tone_ == Tone::Danger) {
        tc = t.error;
        ton = t.textOnStatus;
        tt = t.error;
    }
    if (look_ == Look::Quiet && tone_ == Tone::Neutral)
        tt = t.textSecondary;

    if (!isEnabled()) {
        if (look_ != Look::Quiet)
            renderer.fillRoundedRect(r, radius, t.surfaceTertiary);
        renderer.drawTextCentered(text_, r, fontSize, t.textDisabled);
        renderChildren(renderer);
        return;
    }

    const bool full = hoverStrength_ == HoverStrength::Full;
    const float e = easeOut(fill_);
    const bool latched = toggleable_ && toggled_;

    if (full && e > 0.0f)
        r.y -= e; // the lift: 1px up while hovered
    NUIColor text = look_ == Look::Solid ? ton : tt;

    if (!full && latched) {
        // In a dense row an engaged button is pushed into the surface (Design Spec rule 1).
        Depth::bed(renderer, r, radius, Depth::Cut::Shallow);
        text = tone_ == Tone::Neutral ? t.textPrimary : tt;
    } else {
        const NUIColor fill = look_ == Look::Solid ? NUIColor::lerp(tc, ton, 0.18f) : tc;
        if (full && e > 0.0f) {
            // A soft shadow in the button's own colour, plus a small contact shadow.
            renderer.drawShadow(r, 0.0f, 6.0f * e, 8.0f, fill.withAlpha(0.55f * e));
            renderer.drawShadow(r, 0.0f, 1.0f, 1.0f, t.shadow.withAlpha(t.shadow.a * e));
        }
        switch (look_) {
        case Look::Solid:
            renderer.fillRoundedRect(r, radius, tc);
            break;
        case Look::Soft:
            renderer.fillRoundedRect(r, radius, (!full && e > 0.0f) ? NUIColor::lerp(t.surfaceTertiary, t.surfaceRaised, e)
                                                                    : t.surfaceTertiary);
            break;
        case Look::Outline:
            renderer.strokeRoundedRect(r, radius, 1.0f, tt.withAlpha(0.55f));
            break;
        default:
            break;
        }
        if (full && e > 0.0f) {
            // The fill grows out from where the pointer came in, as a capsule, until it covers the pill.
            const float reach = e * (r.width + r.height);
            const float x0 = std::max(r.x, entryX_ - reach), x1 = std::min(r.right(), entryX_ + reach);
            if (x1 > x0)
                renderer.fillRoundedRect({x0, r.y, x1 - x0, r.height}, radius, fill);
            text = NUIColor::lerp(text, ton, e);
        } else if (!full && e > 0.0f && look_ == Look::Quiet) {
            renderer.fillRoundedRect(r, radius, t.hover.withAlpha(t.hover.a * e));
            text = NUIColor::lerp(text, tone_ == Tone::Neutral ? t.textPrimary : tt, e);
        }
        if (pressed_)
            renderer.fillRoundedRect(r, radius, t.pressed);
    }

    if (ripple_ >= 0.0f) {
        const float p = easeOut(ripple_);
        const float reach = p * (r.width + r.height);
        const float x0 = std::max(r.x, rippleX_ - reach), x1 = std::min(r.right(), rippleX_ + reach);
        if (x1 > x0)
            renderer.fillRoundedRect({x0, r.y, x1 - x0, r.height}, radius, text.withAlpha(0.3f * (1.0f - ripple_)));
    }

    NUIRect textRect = r;
    if (trailingArrow_) {
        textRect.width -= 14.0f;
        const float ax = textRect.right() + 2.0f + 3.0f * (full ? e : 0.0f), ay = r.y + r.height * 0.5f;
        renderer.drawLine({ax - 4.0f, ay}, {ax + 4.0f, ay}, 1.5f, text);
        renderer.drawLine({ax + 1.0f, ay - 3.0f}, {ax + 4.0f, ay}, 1.5f, text);
        renderer.drawLine({ax + 1.0f, ay + 3.0f}, {ax + 4.0f, ay}, 1.5f, text);
    }
    renderer.drawTextCentered(text_, textRect, fontSize, text);
    renderChildren(renderer);
}

void NUIButton::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    if (look_ != Look::Legacy) {
        renderSpecLook(renderer);
        return;
    }

    auto theme = getTheme();
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& themeProps = themeManager.getCurrentTheme();
    const bool active = isEnabled() && (pressed_ || (toggleable_ && toggled_));
    const bool hovered = isEnabled() && isHovered();
    
    // Check custom color flags
    NUIColor backgroundColor = getCurrentBackgroundColor();
    NUIColor borderColor;
    
    // Determine border color
    if (active || hovered) {
        borderColor = backgroundColor;
    } else {
        borderColor = theme ? theme->getBorder() : themeProps.borderSubtle;
    }
    
    auto bounds = getBounds();
    float radius = cornerRadius_ >= 0.0f ? cornerRadius_ : (theme ? theme->getBorderRadius() : themeProps.radiusM);
    NUIRect visualRect{
        std::floor(bounds.x) + 0.5f,
        std::floor(bounds.y) + 0.5f,
        std::max(1.0f, std::floor(bounds.width) - 1.0f),
        std::max(1.0f, std::floor(bounds.height) - 1.0f)
    };

    // Keep button geometry stable; visual press is conveyed by fill/border change.
    
    // Create render rect for background
    // If background relies on flat design logic, we should be careful.
    bool shouldDrawBackground = hovered || active || hasCustomBg_ || style_ == Style::Primary;
    
    if (shouldDrawBackground) {
        NUIColor drawColor = backgroundColor;
        
        // Refined hover style for default buttons (not custom)
        // FIX: Only apply the 0.15f alpha reduction if we are using the THEME hover color.
        // If the user set a custom hover color (like in TrackUIComponent or WindowPanel), utilize it as is.
        if (!hasCustomBg_ && hovered && !active && !hasCustomHover_) {
            drawColor = drawColor.withAlpha(std::max(0.18f, drawColor.a));
        }
        
        renderer.fillRoundedRect(visualRect, radius, drawColor);
    }
    
    // Draw border
    if (borderEnabled_) {
        // Adjust border color
        if (hasCustomBorderColor_) {
             borderColor = borderColor_;
             // Optionally brighten on interaction if desired, but custom usually implies "exact"
             if (active) borderColor = borderColor.lightened(0.2f);
             else if (hovered) borderColor = borderColor.lightened(0.1f);
        } else if (theme) {
             borderColor = theme->getBorder();
             if (active) borderColor = theme->getColor("borderActive").withAlpha(0.9f);
             else if (hovered) borderColor = theme->getColor("borderActive").withAlpha(0.55f);
             else borderColor = borderColor.withAlpha(0.85f);
        } else {
             NUIControlVisualState state{isEnabled(), hovered, pressed_, toggleable_ && toggled_, isFocused()};
             borderColor = resolveControlColors(themeProps, state).border;
        }
        
        const NUIControlVisualState state{isEnabled(), hovered, pressed_, toggleable_ && toggled_, isFocused()};
        const float resolvedBorderWidth = resolveControlColors(themeProps, state).borderWidth;
        float borderWidth = hasCustomBorderWidth_ ? borderWidth_ : (theme ? theme->getBorderWidth() : resolvedBorderWidth);
        // Inset stroke
        NUIRect strokeRect = visualRect;
        strokeRect.x += borderWidth * 0.5f;
        strokeRect.y += borderWidth * 0.5f;
        strokeRect.width -= borderWidth;
        strokeRect.height -= borderWidth;
        float strokeRadius = std::max(0.0f, radius - borderWidth * 0.5f);
        
        renderer.strokeRoundedRect(strokeRect, strokeRadius, borderWidth, borderColor);
    }
    
    // Draw text
    float fontSize = fontSize_ > 0.0f ? fontSize_ : (theme ? theme->getFontSizeNormal() : themeProps.fontSizeS);
    NUIColor textColor = getCurrentTextColor();
    
    renderer.drawTextCentered(text_, visualRect, fontSize, textColor);
    
    // Render children
    renderChildren(renderer); // Using NUIComponent helper
}

void NUIButton::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    if (look_ == Look::Legacy)
        return;
    const bool hovered = isEnabled() && isHovered();
    // Full hover grows over 420ms and shrinks the same way; quiet hover is duration-fast (150ms).
    const float seconds = hoverStrength_ == HoverStrength::Full ? 0.42f : 0.15f;
    const float target = hovered ? 1.0f : 0.0f;
    if (fill_ != target) {
        const float step = static_cast<float>(deltaTime) / seconds;
        fill_ = hovered ? std::min(1.0f, fill_ + step) : std::max(0.0f, fill_ - step);
        setDirty();
    }
    if (ripple_ >= 0.0f) {
        ripple_ += static_cast<float>(deltaTime) / 0.52f;
        if (ripple_ >= 1.0f)
            ripple_ = -1.0f;
        setDirty();
    }
}

bool NUIButton::onMouseEvent(const NUIMouseEvent& event) {
    // A hidden button must not act on input, and it has to say so itself (#672).
    //
    // NUIComponent::onMouseEvent already refuses events for invisible components,
    // but that is not enough here: this override calls the base for its side
    // effects and DISCARDS the result (below), then proceeds to hit-test and fire
    // onClick_ regardless. Hidden components keep their bounds, so containsPoint
    // still succeeds — a press forwarded to a hidden button used to invoke its
    // callback.
    //
    // Only #674's parent-side visibility filter stands between that and a live
    // defect, and it is bypassed by the ~12 places that forward directly to a
    // named child instead of relying on the generic child walk (SettingsDialog's
    // footer buttons, PianoRollToolbar's tool strip, TrackUIComponent's
    // mute/solo/record routing). None of those buttons is hidden today, so this
    // is a trap rather than a bug — but it is the exact shape of #671, and the
    // sibling base widgets (NUISlider, NUIDropdown, NUITextInput, NUIContextMenu,
    // NUIScrollbar, NUIToggle, NUICheckbox) all already self-guard. NUIButton was
    // the only one that did not.
    // Refusing the event is not enough on its own: every release used to clear
    // pressed_, via either the out-of-bounds branch below or the in-bounds one, and
    // returning early here skips both. A press accepted while visible would stay
    // latched when its release arrived hidden — the button reappeared looking
    // pressed, and the next release landing inside it completed a click whose press
    // had been cancelled. Hiding or disabling mid-gesture must CANCEL the gesture,
    // not suspend it.
    if (!isVisible() || !isEnabled()) {
        if (pressed_) {
            pressed_ = false;
            setDirty();
        }
        return false;
    }

    // CRITICAL: Call base class to handle hover state and callbacks (onMouseMove, etc.)
    // This allows parents to use onMouseMove for forced repaints when buttons are hovered.
    NUIComponent::onMouseEvent(event);
    if (look_ != Look::Legacy) {
        // The hover fill grows from where the pointer came in and shrinks back toward where it left.
        const bool hoveredNow = isHovered();
        if (hoveredNow != wasHovered_) {
            entryX_ = std::clamp(event.position.x, getBounds().x, getBounds().right());
            wasHovered_ = hoveredNow;
        }
        if (event.pressed && event.button == NUIMouseButton::Left && containsPoint(event.position)) {
            rippleX_ = event.position.x;
            ripple_ = 0.0f;
        }
    }
    
    if (!containsPoint(event.position)) {
        if (pressed_) {
            pressed_ = false;
            setDirty();
        }
        return false;
    }
    
    if (event.pressed && event.button == NUIMouseButton::Left) {
        pressed_ = true;
        setFocused(true);
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
    const auto& themeProps = NUIThemeManager::getInstance().getCurrentTheme();
    const bool active = isEnabled() && (pressed_ || (toggleable_ && toggled_));
    const bool hovered = isEnabled() && isHovered();
    
    if (active && hasCustomPressed_) return pressedColor_;
    if (hovered && hasCustomHover_) return hoverColor_;
    if (hasCustomBg_) return backgroundColor_;
    
    // Default Style Behavior
    // Text, Icon and Secondary styles should be transparent by default unless hovered/pressed state overrides
    if (style_ == Style::Text || style_ == Style::Icon || style_ == Style::Secondary) {
       if (!active && !hovered) {
           return NUIColor::transparent();
       }
    }

    if (!theme) {
        const NUIControlVisualState state{isEnabled(), hovered, pressed_, toggleable_ && toggled_, isFocused()};
        auto colors = resolveControlColors(themeProps, state);
        if (style_ == Style::Primary) {
            if (!isEnabled()) return themeProps.buttonBgDefault.withAlpha(0.55f);
            if (active) return themeProps.primaryPressed;
            if (hovered) return themeProps.primaryHover;
            return themeProps.primary;
        }
        if ((style_ == Style::Text || style_ == Style::Icon || style_ == Style::Secondary) && !active && !hovered) {
            return NUIColor::transparent();
        }
        return colors.background;
    }
    
    if (!isEnabled()) return theme->getDisabled();
    
    // Style-specific colors
    if (style_ == Style::Primary) {
        if (active) return theme->getColor("primaryPressed", theme->getPrimary().darkened(0.1f));
        if (hovered) return theme->getColor("primaryHover", theme->getPrimary().lightened(0.1f));
        return theme->getPrimary();
    }
    
    // Default / Secondary Styles
    if (active) return theme->getActive();
    if (hovered) return theme->getHover();
    return theme->getSurface();
}

NUIColor NUIButton::getCurrentTextColor() const {
    auto theme = getTheme();
    
    if (hasCustomText_) return textColor_;
    
    if (!theme) {
        const auto& props = NUIThemeManager::getInstance().getCurrentTheme();
        if (!isEnabled()) return props.textDisabled;
        return style_ == Style::Primary ? props.buttonTextActive : props.buttonTextDefault;
    }
    if (!isEnabled()) return theme->getColor("textDisabled", NUIColor::fromHex(0x888888));
    
    // For primary style, text should verify contrast against surface
    if (style_ == Style::Primary) {
        // usually white works best on primary colors
        return NUIColor::white(); 
    }
    
    return theme->getText();
}

} // namespace AestraUI
