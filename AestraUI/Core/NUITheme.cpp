// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUITheme.h"

namespace AestraUI {

NUITheme::NUITheme() {
}

std::shared_ptr<NUITheme> NUITheme::createDefault() {
    auto theme = std::make_shared<NUITheme>();
    
    // Aestra landing-aligned premium dark palette
    theme->setColor("background", NUIColor::fromHex(0x080a0e));
    theme->setColor("surface", NUIColor::fromHex(0x0f1118, 0.96f));
    theme->setColor("surfaceLight", NUIColor::fromHex(0x13161e));
    
    // Accents
    theme->setColor("primary", NUIColor::fromHex(0x8b7de8));      // Brand purple
    theme->setColor("secondary", NUIColor::fromHex(0x4a9eff));    // Electric blue
    theme->setColor("accent", NUIColor::fromHex(0x1db4a6));       // Teal
    theme->setColor("warning", NUIColor::fromHex(0xe8a230));      // Amber
    theme->setColor("error", NUIColor::fromHex(0xe06a4e));        // Coral
    
    // UI Elements
    theme->setColor("text", NUIColor::fromHex(0xc0c8e0));
    theme->setColor("textSecondary", NUIColor::fromHex(0x8b95b0));
    theme->setColor("textDisabled", NUIColor::fromHex(0x3a4060));
    
    theme->setColor("border", NUIColor::fromHex(0x2a2f42, 0.86f));
    theme->setColor("hover", NUIColor::fromHex(0xffffff, 0.06f));
    theme->setColor("active", NUIColor::fromHex(0x8b7de8, 0.2f));
    theme->setColor("disabled", NUIColor::fromHex(0x1a1e2a, 0.58f));

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
