// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIButton.h"
#include "NUITheme.h"
#include "NUIAnimation.h"
#include "NUIThemeSystem.h"
#include "Graphics/NUIRenderer.h"
#include <algorithm>
#include <cmath>

namespace NomadUI {

NUIButton::NUIButton(const std::string& text)
    : text_(text)
{
    setSize(100, 32); // Default size
}

void NUIButton::onRender(NUIRenderer& renderer)
{
    // Instrument button rendering cost
#ifdef NOMAD_ENABLE_PROFILING
    if (Nomad::Profiler::getInstance().isEnabled()) {
        if (style_ == Style::Secondary) Nomad::Profiler::getInstance().beginZone("Button_Secondary");
        else if (style_ == Style::Text) Nomad::Profiler::getInstance().beginZone("Button_Text");
        else Nomad::Profiler::getInstance().beginZone("Button_Primary");
    }
#endif
    auto bounds = getBounds();
    if (bounds.isEmpty()) return;

    // Get theme colors
    const auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
    
    // PERFORMANCE MODE: Simplified rendering with minimal draw calls
    // Get current colors based on state
    NUIColor bgColor = backgroundColor_;
    NUIColor textColor = textColor_;
    NUIColor borderColor = textColor.withAlpha(0.65f);

    // Style-specific baselines
    if (style_ == Style::Primary) {
        bgColor = theme.primary;
        textColor = NUIColor::white();
        borderColor = theme.primary.lightened(0.15f);
    } else if (style_ == Style::Secondary) {
        bgColor = theme.surface.lightened(0.08f);
        borderColor = theme.border.withAlpha(0.8f);
    } else if (style_ == Style::Icon) {
        bgColor = theme.surface.darkened(0.06f);
        borderColor = theme.border.withAlpha(0.55f);
    }

    // Update colors based on state (simple color changes, no animations)
    switch (state_)
    {
        case State::Hovered:
            if (enabled_) {
                bgColor = hoverColor_;
                borderColor = borderColor.lightened(0.12f);
            }
            break;
        case State::Pressed:
            if (enabled_) {
                bgColor = pressedColor_;
                borderColor = borderColor.darkened(0.1f);
            }
            break;
        case State::Disabled:
            bgColor = backgroundColor_.withAlpha(0.5f);
            textColor = textColor_.withAlpha(0.5f);
            borderColor = borderColor.withAlpha(0.35f);
            break;
        case State::Normal:
        default:
            break;
    }

    // Apply toggle state
    if (toggleable_ && toggled_)
    {
        bgColor = pressedColor_;
        borderColor = borderColor.darkened(0.08f);
    }

    // Single-call rendering based on style
    float cornerRadius = (style_ == Style::Icon) ? std::min(bounds.width, bounds.height) * 0.5f : theme.radiusM;
    
    // Depth: subtle shadow for lift
    if (enabled_) {
        const auto& shadow = theme.shadowS;
        float blur = shadow.blurRadius > 0.0f ? shadow.blurRadius : 4.0f;
        float offsetX = shadow.offsetX;
        float offsetY = (shadow.offsetY != 0.0f ? shadow.offsetY : 1.5f);
        NUIColor shadowColor = shadow.color.withAlpha(shadow.opacity * bgColor.a);
        renderer.drawShadow(bounds, offsetX, offsetY, blur, shadowColor);
    }

    // Base fill
    renderer.fillRoundedRect(bounds, cornerRadius, bgColor);

    // Soft sheen on the top half for a tactile feel
    NUIRect sheenRect = bounds;
    sheenRect.height *= 0.55f;
    renderer.fillRoundedRect(sheenRect, cornerRadius, bgColor.lightened(0.12f).withAlpha(0.35f));

    // Border only for non-text buttons
    if (style_ != Style::Text) {
        float borderWidth = (style_ == Style::Secondary) ? 2.0f : 1.0f;
        // Inset the stroke so the arc matches the fill curvature
        NUIRect strokeRect = bounds;
        strokeRect.x += borderWidth * 0.5f;
        strokeRect.y += borderWidth * 0.5f;
        strokeRect.width -= borderWidth;
        strokeRect.height -= borderWidth;
        float strokeRadius = std::max(0.0f, cornerRadius - borderWidth * 0.5f);
        renderer.strokeRoundedRect(strokeRect, strokeRadius, borderWidth, borderColor);
    }

    // Draw text (1 draw call)
    if (!text_.empty() && style_ != Style::Icon)
    {
        float fontSize = fontSize_ > 0.0f ? fontSize_ : std::clamp(bounds.height * 0.56f, 12.0f, 18.0f);
        
        // Calculate text color - grey on hover by default
        NUIColor finalTextColor = textColor;
        
        if (state_ == State::Hovered && !isPressed_)
        {
            // Default to grey on hover
            finalTextColor = NUIColor(70.0f/255.0f, 70.0f/255.0f, 70.0f/255.0f);
            
            // Special preview for M and S buttons only
            if (style_ == Style::Secondary) {
                if (text_ == "M") {
                    // Show red preview for mute button (like when muted)
                    finalTextColor = NUIColor(1.0f, 0.27f, 0.4f, 1.0f); // Error red color
                } else if (text_ == "S") {
                    // Show lime preview for solo button (like when soloed)
                    finalTextColor = NUIColor(0.8f, 1.0f, 0.2f, 1.0f); // Accent lime color
                }
            }
        }

        // Measure text for precise centering; drawText expects top-left Y
        NUISize textSize = renderer.measureText(text_, fontSize);
        float textX = std::round(bounds.x + (bounds.width - textSize.width) * 0.5f);
        float textY = std::round(bounds.y + (bounds.height - textSize.height) * 0.5f);
        
        renderer.drawText(text_, NUIPoint(textX, textY), fontSize, finalTextColor);
    }

#ifdef NOMAD_ENABLE_PROFILING
    if (Nomad::Profiler::getInstance().isEnabled()) {
        if (style_ == Style::Secondary) Nomad::Profiler::getInstance().endZone("Button_Secondary");
        else if (style_ == Style::Text) Nomad::Profiler::getInstance().endZone("Button_Text");
        else Nomad::Profiler::getInstance().endZone("Button_Primary");
    }
#endif
}

void NUIButton::onMouseEnter()
{
    if (enabled_)
    {
        state_ = State::Hovered;
        repaint();
    }
}

void NUIButton::onMouseLeave()
{
    // Always reset to normal state when mouse leaves, regardless of press state
    state_ = State::Normal;
    repaint();
}

bool NUIButton::onMouseEvent(const NUIMouseEvent& event)
{
    if (!enabled_) return false;

    // Let base class handle hover detection and child events first
    bool handled = NUIComponent::onMouseEvent(event);

    // If this event was not within bounds, return early
    if (!containsPoint(event.position)) {
        return false;
    }

    // Handle mouse down
    if (event.pressed && event.button == NUIMouseButton::Left)
    {
        isPressed_ = true;
        state_ = State::Pressed;
        repaint();
        return true;
    }

    // Handle mouse up
    if (event.released && event.button == NUIMouseButton::Left)
    {
        bool wasPressed = isPressed_;
        isPressed_ = false;

        if (containsPoint(event.position))
        {
            if (toggleable_)
            {
                toggled_ = !toggled_;
                triggerToggle();
            }
            else
            {
                triggerClick();
            }
        }

        // Don't manually set state - let hover system handle it
        repaint();
        return true;
    }

    return handled;
}

void NUIButton::onMouseDown(int x, int y, int button)
{
    if (!enabled_) return;

    state_ = State::Pressed;
    isPressed_ = true;
    repaint();
}

void NUIButton::onMouseUp(int x, int y, int button)
{
    if (!enabled_) return;

    bool wasPressed = isPressed_;
    isPressed_ = false;

    // Let the hover system handle the state - don't set it manually here
    // The base class hover tracking will set it to Hovered if mouse is still over button

    if (wasPressed)
    {
        if (toggleable_)
        {
            toggled_ = !toggled_;
            triggerToggle();
        }
        else
        {
            triggerClick();
        }
    }

    repaint();
}

void NUIButton::setText(const std::string& text)
{
    text_ = text;
    repaint();
}

void NUIButton::setStyle(Style style)
{
    style_ = style;
    repaint();
}

void NUIButton::setEnabled(bool enabled)
{
    enabled_ = enabled;
    updateState();
    repaint();
}

void NUIButton::setToggleable(bool toggleable)
{
    toggleable_ = toggleable;
}

void NUIButton::setToggled(bool toggled)
{
    if (toggleable_)
    {
        toggled_ = toggled;
        repaint();
    }
}

void NUIButton::setOnClick(std::function<void()> callback)
{
    onClickCallback_ = callback;
}

void NUIButton::setOnToggle(std::function<void(bool)> callback)
{
    onToggleCallback_ = callback;
}

void NUIButton::setBackgroundColor(const NUIColor& color)
{
    backgroundColor_ = color;
    repaint();
}

void NUIButton::setTextColor(const NUIColor& color)
{
    textColor_ = color;
    repaint();
}

void NUIButton::setHoverColor(const NUIColor& color)
{
    hoverColor_ = color;
    repaint();
}

void NUIButton::setPressedColor(const NUIColor& color)
{
    pressedColor_ = color;
    repaint();
}

void NUIButton::updateState()
{
    if (!enabled_)
    {
        state_ = State::Disabled;
    }
    else if (state_ == State::Disabled)
    {
        // If coming out of disabled state, go to normal or hovered
        state_ = isHovered() ? State::Hovered : State::Normal;
    }
    else if (state_ != State::Pressed)
    {
        // Preserve current state unless it's pressed (pressed state is managed by mouse events)
        state_ = isHovered() ? State::Hovered : State::Normal;
    }
}

void NUIButton::triggerClick()
{
    if (onClickCallback_)
    {
        onClickCallback_();
    }
}

void NUIButton::triggerToggle()
{
    if (onToggleCallback_)
    {
        onToggleCallback_(toggled_);
    }
}

} // namespace NomadUI
