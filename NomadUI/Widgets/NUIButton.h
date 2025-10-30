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
    
    /**
     * Set the click callback.
     */
    void setOnClick(std::function<void()> callback) { onClick_ = callback; }
    
    /**
     * Set custom colors (overrides theme).
     */
    void setBackgroundColor(const NUIColor& color) { backgroundColor_ = color; useCustomColors_ = true; }
    void setTextColor(const NUIColor& color) { textColor_ = color; useCustomColors_ = true; }
    void setHoverColor(const NUIColor& color) { hoverColor_ = color; useCustomColors_ = true; }
    void setPressedColor(const NUIColor& color) { pressedColor_ = color; useCustomColors_ = true; }
    
    /**
     * Reset to theme colors.
     */
    void resetColors() { useCustomColors_ = false; setDirty(); }
    
    /**
     * Set font size.
     */
    void setFontSize(float size) { fontSize_ = size; setDirty(); }
    float getFontSize() const { return fontSize_; }
    
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
    
    // Colors
    bool useCustomColors_ = false;
    NUIColor backgroundColor_;
    NUIColor textColor_;
    NUIColor hoverColor_;
    NUIColor pressedColor_;
    
    // State
    bool pressed_ = false;
    float hoverAlpha_ = 0.0f; // Animated hover state (0.0 to 1.0)
    
    // Style
    bool borderEnabled_ = true;
    bool glowEnabled_ = true;
    
    // Callback
    std::function<void()> onClick_;
    
    // Helpers
    NUIColor getCurrentBackgroundColor() const;
    NUIColor getCurrentTextColor() const;
};

} // namespace NomadUI
