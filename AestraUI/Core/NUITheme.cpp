// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUITheme.h"

#include "AestraJSON.h"
#include "AestraLog.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <unordered_set>

namespace AestraUI {

namespace {

// Parses "#RRGGBB" or "#RRGGBBAA" (leading '#' required). Returns false on
// malformed input without touching `out`.
bool parseHexColor(const std::string& text, NUIColor& out) {
    if (text.empty() || text[0] != '#') {
        return false;
    }
    const std::string hex = text.substr(1);
    if (hex.size() != 6 && hex.size() != 8) {
        return false;
    }
    for (char c : hex) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    const auto rgb = static_cast<uint32_t>(std::stoul(hex.substr(0, 6), nullptr, 16));
    float alpha = 1.0f;
    if (hex.size() == 8) {
        alpha = static_cast<float>(std::stoul(hex.substr(6, 2), nullptr, 16)) / 255.0f;
    }
    out = NUIColor::fromHex(rgb, alpha);
    return true;
}

bool requiresPositiveDimension(const std::string& name) {
    static const std::unordered_set<std::string> positiveDimensions{
        "borderRadius", "borderRadiusSmall", "borderRadiusLarge", "padding", "paddingSmall", "paddingLarge",
        "margin", "borderWidth", "compactControlHeight", "standardControlHeight", "dialogActionHeight",
        "standardRowHeight", "compactMenuRowHeight", "standardMenuRowHeight", "panelHeaderHeight",
        "sectionHeaderHeight", "standardIconSize", "minimumHitArea", "dividerWidth", "panelPadding",
        "dialogPadding"};
    return positiveDimensions.find(name) != positiveDimensions.end();
}

} // namespace

NUITheme::NUITheme() {
}

std::shared_ptr<NUITheme> NUITheme::createDefault() {
    auto theme = std::make_shared<NUITheme>();
    
    // Aestra landing-aligned premium dark palette
    theme->setColor("background", NUIColor::fromHex(0x0a0a0a));
    theme->setColor("surface", NUIColor::fromHex(0x101010, 0.96f));
    theme->setColor("surfaceLight", NUIColor::fromHex(0x161616));
    
    // Accents
    theme->setColor("primary", NUIColor::fromHex(0x8b7de8));      // Brand purple
    theme->setColor("secondary", NUIColor::fromHex(0x4a9eff));    // Electric blue
    theme->setColor("accent", NUIColor::fromHex(0x1db4a6));       // Teal
    theme->setColor("warning", NUIColor::fromHex(0xe8a230));      // Amber
    theme->setColor("error", NUIColor::fromHex(0xe06a4e));        // Coral
    
    // UI Elements
    theme->setColor("text", NUIColor::fromHex(0xc8c8c8));
    theme->setColor("textSecondary", NUIColor::fromHex(0x999999));
    theme->setColor("textMuted", NUIColor::fromHex(0x777777));
    theme->setColor("textDisabled", NUIColor::fromHex(0x474747));

    theme->setColor("border", NUIColor::fromHex(0x2d2d2d, 0.86f));
    theme->setColor("borderSubtle", NUIColor::fromHex(0x2b2b2b, 0.90f));
    theme->setColor("borderStrong", NUIColor::fromHex(0x3a3a3a, 0.95f));
    theme->setColor("hover", NUIColor::fromHex(0xffffff, 0.06f));
    theme->setColor("active", NUIColor::fromHex(0x8b7de8, 0.2f));
    theme->setColor("disabled", NUIColor::fromHex(0x1d1d1d, 0.58f));
    theme->setColor("focusRing", NUIColor::fromHex(0x8b7de8, 0.86f));
    theme->setColor("armed", NUIColor::fromHex(0xe06a4e));
    theme->setColor("muted", NUIColor::fromHex(0xe8a230));
    theme->setColor("soloed", NUIColor::fromHex(0x4a9eff));
    theme->setColor("bypassed", NUIColor::fromHex(0x777777));
    theme->setColor("dragTarget", NUIColor::fromHex(0x8b7de8, 0.28f));
    theme->setColor("meterBackground", NUIColor::fromHex(0x080808, 0.92f));
    theme->setColor("meterActive", NUIColor::fromHex(0x4a9eff));
    theme->setColor("gridMajor", NUIColor::fromHex(0xffffff, 0.10f));
    theme->setColor("gridMinor", NUIColor::fromHex(0xffffff, 0.06f));

    // Dimensions
    theme->setDimension("borderRadius", 12.0f);        // Soft Geometry
    theme->setDimension("borderRadiusSmall", 6.0f);
    theme->setDimension("borderRadiusLarge", 16.0f);
    
    theme->setDimension("padding", 12.0f);             // More breathing room
    theme->setDimension("paddingSmall", 6.0f);
    theme->setDimension("paddingLarge", 24.0f);
    
    theme->setDimension("margin", 8.0f);
    theme->setDimension("borderWidth", 1.0f);
    theme->setDimension("compactControlHeight", 24.0f);
    theme->setDimension("standardControlHeight", 28.0f);
    theme->setDimension("dialogActionHeight", 36.0f);
    theme->setDimension("standardRowHeight", 28.0f);
    theme->setDimension("compactMenuRowHeight", 24.0f);
    theme->setDimension("standardMenuRowHeight", 28.0f);
    theme->setDimension("panelHeaderHeight", 32.0f);
    theme->setDimension("sectionHeaderHeight", 24.0f);
    theme->setDimension("standardIconSize", 16.0f);
    theme->setDimension("minimumHitArea", 24.0f);
    theme->setDimension("dividerWidth", 1.0f);
    theme->setDimension("panelPadding", 8.0f);
    theme->setDimension("dialogPadding", 16.0f);
    
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
    // Start from the default theme so every token missing from the file keeps
    // its default value. On any failure this default theme is returned (never
    // nullptr) and the failure is logged.
    auto theme = createDefault();

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        Aestra::Log::warning("[NUITheme] Cannot open theme file '" + filepath + "' — using default theme");
        return theme;
    }

    // Theme files are tiny; reject pathological sizes before parsing
    // (file parsing is security-sensitive input handling).
    constexpr std::streamoff kMaxThemeFileBytes = 1 << 20; // 1 MiB
    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    if (fileSize < 0 || fileSize > kMaxThemeFileBytes) {
        Aestra::Log::error("[NUITheme] Theme file '" + filepath + "' rejected (size " + std::to_string(fileSize) +
                           " bytes) — using default theme");
        return theme;
    }
    file.seekg(0, std::ios::beg);
    std::string content(static_cast<size_t>(fileSize), '\0');
    file.read(content.data(), fileSize);

    Aestra::JSON root = Aestra::JSON::parse(content);
    if (!root.isObject()) {
        Aestra::Log::error("[NUITheme] Theme file '" + filepath + "' is not a valid JSON object — using default theme");
        return theme;
    }

    int applied = 0;
    int skipped = 0;

    // NOTE: Aestra::JSON's const asObject() intentionally returns an empty map
    // (SEC-RTM-014); iterate via the non-const copy-returning overload.
    auto rootObj = root.asObject();

    auto forEachEntry = [&](const char* section, const std::function<bool(const std::string&, Aestra::JSON&)>& apply) {
        auto it = rootObj.find(section);
        if (it == rootObj.end()) {
            return;
        }
        if (!it->second.isObject()) {
            Aestra::Log::warning("[NUITheme] Section '" + std::string(section) + "' in '" + filepath +
                                 "' is not an object — ignored");
            return;
        }
        auto entries = it->second.asObject();
        for (auto& [key, value] : entries) {
            if (apply(key, value)) {
                ++applied;
            } else {
                ++skipped;
                Aestra::Log::warning("[NUITheme] Invalid value for '" + std::string(section) + "." + key + "' in '" +
                                     filepath + "' — keeping default");
            }
        }
    };

    forEachEntry("colors", [&](const std::string& key, Aestra::JSON& value) {
        NUIColor color;
        if (!value.isString() || !parseHexColor(value.asString(), color)) {
            return false;
        }
        theme->setColor(key, color);
        return true;
    });

    auto numericEntry = [](Aestra::JSON& value, float& out, bool requirePositive) {
        if (!value.isNumber() || !std::isfinite(value.asNumber())) {
            return false;
        }
        if (requirePositive && value.asNumber() <= 0.0) {
            return false;
        }
        out = static_cast<float>(value.asNumber());
        return true;
    };

    forEachEntry("dimensions", [&](const std::string& key, Aestra::JSON& value) {
        float v = 0.0f;
        if (!numericEntry(value, v, requiresPositiveDimension(key)))
            return false;
        theme->setDimension(key, v);
        return true;
    });

    forEachEntry("effects", [&](const std::string& key, Aestra::JSON& value) {
        float v = 0.0f;
        if (!numericEntry(value, v, false))
            return false;
        theme->setEffect(key, v);
        return true;
    });

    forEachEntry("fontSizes", [&](const std::string& key, Aestra::JSON& value) {
        float v = 0.0f;
        if (!numericEntry(value, v, true))
            return false; // font sizes must be > 0
        theme->setFontSize(key, v);
        return true;
    });

    Aestra::Log::info("[NUITheme] Loaded theme '" + filepath + "': " + std::to_string(applied) + " tokens applied, " +
                      std::to_string(skipped) + " invalid tokens kept as defaults");
    return theme;
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
