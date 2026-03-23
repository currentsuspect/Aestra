// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUITheme.h"

namespace AestraUI {

NUITheme::NUITheme() {
}

std::shared_ptr<NUITheme> NUITheme::createDefault() {
    auto theme = std::make_shared<NUITheme>();
    
    // Aestra "Deep Glass" Theme
    theme->setColor("background", NUIColor::fromHex(0x0a0b10));   // Void
    theme->setColor("surface", NUIColor::fromHex(0x181b21, 0.9f)); // Dark Surface (Glass)
    theme->setColor("surfaceLight", NUIColor::fromHex(0x242730)); // Overlay
    
    // Accents
    theme->setColor("primary", NUIColor::fromHex(0x9d4edd));      // Aestra Purple
    theme->setColor("secondary", NUIColor::fromHex(0x4cc9f0));    // Neon Cyan
    theme->setColor("accent", NUIColor::fromHex(0x06d6a0));       // Signal Green
    theme->setColor("warning", NUIColor::fromHex(0xffd166));      // Warning Yellow/Orange
    theme->setColor("error", NUIColor::fromHex(0xef476f));        // Signal Red
    
    // UI Elements
    theme->setColor("text", NUIColor::fromHex(0xffffff, 0.95f));
    theme->setColor("textSecondary", NUIColor::fromHex(0xffffff, 0.6f));
    theme->setColor("textDisabled", NUIColor::fromHex(0xffffff, 0.3f));
    
    theme->setColor("border", NUIColor::fromHex(0xffffff, 0.1f)); // 10% White Border
    theme->setColor("hover", NUIColor::fromHex(0xffffff, 0.05f));  // Subtle white highlight
    theme->setColor("active", NUIColor::fromHex(0x9d4edd, 0.2f));  // Purple Tint
    theme->setColor("disabled", NUIColor::fromHex(0x181b21, 0.5f));

    // Dimensions
    theme->setDimension("borderRadius", 12.0f);        // Soft Geometry
    theme->setDimension("borderRadiusSmall", 6.0f);
    theme->setDimension("borderRadiusLarge", 16.0f);
    
    theme->setDimension("padding", 12.0f);             // More breathing room
    theme->setDimension("paddingSmall", 6.0f);
    theme->setDimension("paddingLarge", 24.0f);
    
    theme->setDimension("margin", 8.0f);
    theme->setDimension("borderWidth", 1.0f);
    
    // Effects
    theme->setEffect("glowIntensity", 0.5f);           // Enhanced Glow
    theme->setEffect("shadowBlur", 16.0f);             // Softer Shadows
    theme->setEffect("shadowOffsetX", 0.0f);
    theme->setEffect("shadowOffsetY", 4.0f);
    theme->setEffect("animationDuration", 0.25f);
    theme->setEffect("hoverScale", 1.02f);             // Subtler scale
    
    // Font sizes (Variable)
    theme->setFontSize("tiny", 10.0f);
    theme->setFontSize("small", 12.0f);
    theme->setFontSize("normal", 15.0f);               // Improved readability
    theme->setFontSize("large", 20.0f);
    theme->setFontSize("title", 28.0f);
    theme->setFontSize("huge", 48.0f);
    
    return theme;
}

std::shared_ptr<NUITheme> NUITheme::loadFromFile(const std::string& filepath) {
    // TODO: Implement JSON loading
    // For now, return default theme
    return createDefault();
}

// ============================================================================
// Colors
// ============================================================================

void NUITheme::setColor(const std::string& name, const NUIColor& color) {
    colors_[name] = color;
}

NUIColor NUITheme::getColor(const std::string& name, const NUIColor& defaultColor) const {
    auto it = colors_.find(name);
    if (it != colors_.end()) {
        return it->second;
    }
    return defaultColor;
}

// ============================================================================
// Dimensions
// ============================================================================

void NUITheme::setDimension(const std::string& name, float value) {
    dimensions_[name] = value;
}

float NUITheme::getDimension(const std::string& name, float defaultValue) const {
    auto it = dimensions_.find(name);
    if (it != dimensions_.end()) {
        return it->second;
    }
    return defaultValue;
}

// ============================================================================
// Effects
// ============================================================================

void NUITheme::setEffect(const std::string& name, float value) {
    effects_[name] = value;
}

float NUITheme::getEffect(const std::string& name, float defaultValue) const {
    auto it = effects_.find(name);
    if (it != effects_.end()) {
        return it->second;
    }
    return defaultValue;
}

// ============================================================================
// Fonts
// ============================================================================

void NUITheme::setFontSize(const std::string& name, float size) {
    fontSizes_[name] = size;
}

float NUITheme::getFontSize(const std::string& name, float defaultSize) const {
    auto it = fontSizes_.find(name);
    if (it != fontSizes_.end()) {
        return it->second;
    }
    return defaultSize;
}

// TODO: Implement font creation when NUIFont supports copying
// NUIFont NUITheme::getDefaultFont() const {
//     NUIFont font;
//     // TODO: Load default font from file
//     return font;
// }

} // namespace AestraUI
