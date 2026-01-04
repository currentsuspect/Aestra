// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Graphics/NUIRenderer.h"
#include <string>
#include <functional>

namespace NomadUI {

/**
 * Button widget for Nomad UI.
 * 
 * Features:
 * - Text label
 * - Hover/pressed states
 * - Click callback
 * - Customizable colors
 * - Smooth animations
 */
class NUIButton : public NUIComponent {
public:
    // Button styles
    enum class Style
    {
        Primary,    // Main action button
        Secondary,  // Secondary action button
        Text,       // Text-only button
        Icon        // Icon-only button
    };

    NUIButton();
    NUIButton(const std::string& text);
    ~NUIButton() override = default;
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * Set the button text.
     */
    void setText(const std::string& text);
    std::string getText() const { return text_; }

    void setStyle(Style style);
    Style getStyle() const { return style_; }

    /**
     * Set the click callback.
     */
    void setOnClick(std::function<void()> callback) { onClick_ = callback; }

    /**
     * Set the toggle callback.
     */
    void setOnToggle(std::function<void(bool)> callback) { onToggle_ = callback; }

    /**
     * Toggle state.
     */
    void setToggleable(bool toggleable) { toggleable_ = toggleable; }
    bool isToggleable() const { return toggleable_; }

    void setToggled(bool toggled) { toggled_ = toggled; setDirty(); }
    bool isToggled() const { return toggled_; }
    
    /**
     * Set custom colors (overrides theme).
     */
    void setBackgroundColor(const NUIColor& color) { backgroundColor_ = color; hasCustomBg_ = true; setDirty(); }
    void setTextColor(const NUIColor& color) { textColor_ = color; hasCustomText_ = true; setDirty(); }
    void setHoverColor(const NUIColor& color) { hoverColor_ = color; hasCustomHover_ = true; setDirty(); }
    void setPressedColor(const NUIColor& color) { pressedColor_ = color; hasCustomPressed_ = true; setDirty(); }
    
    /**
     * Reset to theme colors.
     */
    void resetColors() { 
        hasCustomBg_ = false; 
        hasCustomText_ = false; 
        hasCustomHover_ = false; 
        hasCustomPressed_ = false; 
        setDirty(); 
    }
    
    /**
     * Set font size.
     */
    void setFontSize(float size) { fontSize_ = size; setDirty(); }
    float getFontSize() const { return fontSize_; }

    /**
     * Set corner radius.
     * -1.0f = use theme/default radius.
     */
    void setCornerRadius(float radius) { cornerRadius_ = radius; setDirty(); }
    float getCornerRadius() const { return cornerRadius_; }

    /**
     * Check if button is currently pressed.
     */
    bool isPressed() const { return pressed_; }
    
    /**
     * Enable/disable border.
     */
    void setBorderEnabled(bool enabled) { borderEnabled_ = enabled; setDirty(); }
    bool isBorderEnabled() const { return borderEnabled_; }
    
    /**
     * Enable/disable glow effect on hover.
     */
    void setGlowEnabled(bool enabled) { glowEnabled_ = enabled; setDirty(); }
    bool isGlowEnabled() const { return glowEnabled_; }
    
    // ========================================================================
    // Component Overrides
    // ========================================================================
    
    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    
private:
    // Text
    std::string text_;
    float fontSize_ = 0.0f; // 0 = use theme default
    float cornerRadius_ = -1.0f; // -1 = use theme default
    
    // Colors
    // Granular flags to allow mixing custom and theme colors
    bool hasCustomBg_ = false;
    bool hasCustomText_ = false;
    bool hasCustomHover_ = false;
    bool hasCustomPressed_ = false;
    
    NUIColor backgroundColor_;
    NUIColor textColor_;
    NUIColor hoverColor_;
    NUIColor pressedColor_;
    
    // State
    bool pressed_ = false;
    float hoverAlpha_ = 0.0f; // Animated hover state (0.0 to 1.0)
    bool toggleable_ = false;
    bool toggled_ = false;
    
    // Style
    Style style_ = Style::Primary;
    bool borderEnabled_ = true;
    bool glowEnabled_ = true;
    
    // Callbacks
    std::function<void()> onClick_;
    std::function<void(bool)> onToggle_;
    
    // Helpers
    NUIColor getCurrentBackgroundColor() const;
    NUIColor getCurrentTextColor() const;
};

} // namespace NomadUI
