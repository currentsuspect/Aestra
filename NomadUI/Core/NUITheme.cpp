// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUITheme.h"

namespace NomadUI {

NUITheme::NUITheme() {
}

std::shared_ptr<NUITheme> NUITheme::createDefault() {
    auto theme = std::make_shared<NUITheme>();
    
    // Nomad Dark Theme - FL Studio inspired
    theme->setColor("background", NUIColor::fromHex(0x0d0d0d));
    theme->setColor("surface", NUIColor::fromHex(0x1a1a1a));
    theme->setColor("surfaceLight", NUIColor::fromHex(0x2a2a2a));
    theme->setColor("primary", NUIColor::fromHex(0xa855f7));      // Purple
    theme->setColor("secondary", NUIColor::fromHex(0x3b82f6));    // Blue
    theme->setColor("accent", NUIColor::fromHex(0x22c55e));       // Green
    theme->setColor("warning", NUIColor::fromHex(0xf59e0b));      // Orange
    theme->setColor("error", NUIColor::fromHex(0xef4444));        // Red
    theme->setColor("text", NUIColor::fromHex(0xffffff));
    theme->setColor("textSecondary", NUIColor::fromHex(0x999999));
    theme->setColor("textDisabled", NUIColor::fromHex(0x666666));
    theme->setColor("border", NUIColor::fromHex(0x333333));
    theme->setColor("hover", NUIColor::fromHex(0x2a2a2a));
    theme->setColor("active", NUIColor::fromHex(0x3a3a3a));
    theme->setColor("disabled", NUIColor::fromHex(0x1a1a1a));
    
    // Dimensions
    theme->setDimension("borderRadius", 4.0f);
    theme->setDimension("borderRadiusSmall", 2.0f);
    theme->setDimension("borderRadiusLarge", 8.0f);
    theme->setDimension("padding", 8.0f);
    theme->setDimension("paddingSmall", 4.0f);
    theme->setDimension("paddingLarge", 12.0f);
    theme->setDimension("margin", 4.0f);
    theme->setDimension("borderWidth", 1.0f);
    
    // Effects
    theme->setEffect("glowIntensity", 0.3f);
    theme->setEffect("shadowBlur", 8.0f);
    theme->setEffect("shadowOffsetX", 0.0f);
    theme->setEffect("shadowOffsetY", 2.0f);
    theme->setEffect("animationDuration", 0.2f);
    theme->setEffect("hoverScale", 1.05f);
    
    // Font sizes
    theme->setFontSize("tiny", 9.0f);
    theme->setFontSize("small", 11.0f);
    theme->setFontSize("normal", 14.0f);
    theme->setFontSize("large", 18.0f);
    theme->setFontSize("title", 24.0f);
    theme->setFontSize("huge", 32.0f);
    
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

} // namespace NomadUI
