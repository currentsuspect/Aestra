// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUITypes.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace NomadUI {

/**
 * Theme system for the Nomad UI framework.
 * 
 * Provides centralized styling for all UI components.
 * Can be loaded from JSON files for easy customization.
 */
class NUITheme {
public:
    NUITheme();
    ~NUITheme() = default;
    
    // ========================================================================
    // Factory
    // ========================================================================
    
    /**
     * Create the default dark theme.
     */
    static std::shared_ptr<NUITheme> createDefault();
    
    /**
     * Load a theme from a JSON file.
     */
    static std::shared_ptr<NUITheme> loadFromFile(const std::string& filepath);
    
    // ========================================================================
    // Colors
    // ========================================================================
    
    void setColor(const std::string& name, const NUIColor& color);
    NUIColor getColor(const std::string& name, const NUIColor& defaultColor = NUIColor::black()) const;
    
    // Common color accessors
    NUIColor getBackground() const { return getColor("background"); }
    NUIColor getSurface() const { return getColor("surface"); }
    NUIColor getPrimary() const { return getColor("primary"); }
    NUIColor getSecondary() const { return getColor("secondary"); }
    NUIColor getText() const { return getColor("text"); }
    NUIColor getTextSecondary() const { return getColor("textSecondary"); }
    NUIColor getBorder() const { return getColor("border"); }
    NUIColor getHover() const { return getColor("hover"); }
    NUIColor getActive() const { return getColor("active"); }
    NUIColor getDisabled() const { return getColor("disabled"); }
    
    // ========================================================================
    // Dimensions
    // ========================================================================
    
    void setDimension(const std::string& name, float value);
    float getDimension(const std::string& name, float defaultValue = 0.0f) const;
    
    // Common dimension accessors
    float getBorderRadius() const { return getDimension("borderRadius", 4.0f); }
    float getPadding() const { return getDimension("padding", 8.0f); }
    float getMargin() const { return getDimension("margin", 4.0f); }
    float getBorderWidth() const { return getDimension("borderWidth", 1.0f); }
    
    // ========================================================================
    // Effects
    // ========================================================================
    
    void setEffect(const std::string& name, float value);
    float getEffect(const std::string& name, float defaultValue = 0.0f) const;
    
    // Common effect accessors
    float getGlowIntensity() const { return getEffect("glowIntensity", 0.3f); }
    float getShadowBlur() const { return getEffect("shadowBlur", 8.0f); }
    float getAnimationDuration() const { return getEffect("animationDuration", 0.2f); }
    
    // ========================================================================
    // Fonts
    // ========================================================================
    
    void setFontSize(const std::string& name, float size);
    float getFontSize(const std::string& name, float defaultSize = 14.0f) const;
    
    // Common font size accessors
    float getFontSizeSmall() const { return getFontSize("small", 14.0f); }
    float getFontSizeNormal() const { return getFontSize("normal", 18.0f); }
    float getFontSizeLarge() const { return getFontSize("large", 22.0f); }
    float getFontSizeTitle() const { return getFontSize("title", 28.0f); }
    
    // Font creation
    // TODO: Implement font creation when NUIFont supports copying
    // NUIFont getDefaultFont() const;
    
private:
    std::unordered_map<std::string, NUIColor> colors_;
    std::unordered_map<std::string, float> dimensions_;
    std::unordered_map<std::string, float> effects_;
    std::unordered_map<std::string, float> fontSizes_;
};

} // namespace NomadUI
