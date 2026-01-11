// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIConfigLoader.h"
#include "../../NomadCore/include/NomadJSON.h"
#include <sstream>
#include <regex>
#include <algorithm>
#include <iomanip>

namespace NomadUI {

NUIConfigLoader& NUIConfigLoader::getInstance() {
    static NUIConfigLoader instance;
    return instance;
}

bool NUIConfigLoader::loadConfig(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << filePath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    return loadConfigFromString(content);
}

bool NUIConfigLoader::loadConfigFromString(const std::string& yamlContent) {
    config_ = Nomad::JSON::object();
    configLoaded_ = parseYAML(yamlContent);

    if (configLoaded_) {
        applyConfig();
    }

    return configLoaded_;
}

void NUIConfigLoader::applyConfig() {
    if (!configLoaded_) return;

    auto& themeManager = NUIThemeManager::getInstance();
    auto& theme = themeManager.getCurrentThemeMutable();

    // Apply colors
    if (config_.has("colors")) {
        applyColors(config_["colors"]);
    }

    // Apply layout dimensions
    if (config_.has("layout")) {
        applyLayout(config_["layout"]);
    }

    // Apply spacing
    if (config_.has("spacing")) {
        applySpacing(config_["spacing"]);
    }

    // Apply typography
    if (config_.has("typography")) {
        applyTypography(config_["typography"]);
    }
}

void NUIConfigLoader::saveConfig(const std::string& filePath) {
    auto& themeManager = NUIThemeManager::getInstance();
    const auto& theme = themeManager.getCurrentTheme();

    Nomad::JSON config = Nomad::JSON::object();

    // Save colors
    Nomad::JSON colors = Nomad::JSON::object();
    colors.set("backgroundPrimary", "#" + colorToHex(theme.backgroundPrimary));
    colors.set("backgroundSecondary", "#" + colorToHex(theme.backgroundSecondary));
    colors.set("primary", "#" + colorToHex(theme.primary));
    colors.set("accentCyan", "#" + colorToHex(theme.accentCyan));
    colors.set("textPrimary", "#" + colorToHex(theme.textPrimary));
    config.set("colors", colors);

    // Save layout
    Nomad::JSON layout = Nomad::JSON::object();
    layout.set("trackHeight", theme.layout.trackHeight);
    layout.set("trackControlsWidth", theme.layout.trackControlsWidth);
    layout.set("fileBrowserWidth", theme.layout.fileBrowserWidth);
    layout.set("transportBarHeight", theme.layout.transportBarHeight);
    config.set("layout", layout);

    // Save spacing
    Nomad::JSON spacing = Nomad::JSON::object();
    spacing.set("panelMargin", theme.spacingM);
    spacing.set("componentPadding", theme.spacingS);
    config.set("spacing", spacing);

    std::ofstream file(filePath);
    if (file.is_open()) {
        file << config.toString(2);
    }
}

bool NUIConfigLoader::parseYAML(const std::string& content) {
    // Simple YAML parser - handles basic key: value pairs and nested objects
    std::istringstream stream(content);
    std::string line;
    std::string currentPath;
    int indentLevel = 0;

    while (std::getline(stream, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Count indentation
        int currentIndent = 0;
        for (char c : line) {
            if (c == ' ') currentIndent++;
            else break;
        }

        // Remove leading/trailing whitespace
        line.erase(0, currentIndent);
        line = std::regex_replace(line, std::regex("\\s+$"), "");

        if (line.empty()) continue;

        // Check if this is a new object (ends with ':')
        if (line.back() == ':') {
            std::string key = line.substr(0, line.length() - 1);
            key = std::regex_replace(key, std::regex("\\s+$"), "");

            if (currentIndent / 2 > indentLevel) {
                currentPath += "." + key;
                indentLevel++;
            } else if (currentIndent / 2 < indentLevel) {
                // Go up levels
                int levelsUp = indentLevel - currentIndent / 2;
                for (int i = 0; i < levelsUp; i++) {
                    size_t lastDot = currentPath.find_last_of('.');
                    if (lastDot != std::string::npos) {
                        currentPath = currentPath.substr(0, lastDot);
                    }
                }
                currentPath += "." + key;
                indentLevel = currentIndent / 2;
            } else {
                size_t lastDot = currentPath.find_last_of('.');
                if (lastDot != std::string::npos) {
                    currentPath = currentPath.substr(0, lastDot);
                }
                currentPath += "." + key;
            }
        } else {
            // This is a value
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string key = line.substr(0, colonPos);
                std::string value = line.substr(colonPos + 1);

                key = std::regex_replace(key, std::regex("^\\s+|\\s+$"), "");
                value = std::regex_replace(value, std::regex("^\\s+|\\s+$"), "");

                // Set the value in the JSON structure
                setNestedValue(config_, currentPath + "." + key, value);
            }
        }
    }

    return true;
}

void NUIConfigLoader::setNestedValue(Nomad::JSON& json, const std::string& path, const std::string& value) {
    std::vector<std::string> keys;
    std::istringstream pathStream(path);
    std::string key;

    while (std::getline(pathStream, key, '.')) {
        if (!key.empty()) {
            keys.push_back(key);
        }
    }

    Nomad::JSON* current = &json;
    for (size_t i = 0; i < keys.size() - 1; i++) {
        if (current->isObject()) {
            if (!current->has(keys[i])) {
                current->set(keys[i], Nomad::JSON::object());
            }
            current = &((*current)[keys[i]]);
        }
    }

    if (!keys.empty() && current->isObject()) {
        // Try to parse as number first, then color, then string
        if (value.find('#') == 0) {
            // Color value
            NUIColor color = parseColor(value);
            current->set(keys.back(), "#" + colorToHex(color));
        } else if (std::regex_match(value, std::regex("-?\\d*\\.?\\d+"))) {
            // Number value
            current->set(keys.back(), std::stod(value));
        } else {
            // String value
            current->set(keys.back(), value);
        }
    }
}

void NUIConfigLoader::applyColors(const Nomad::JSON& colors) {
    auto& themeManager = NUIThemeManager::getInstance();
    auto& theme = themeManager.getCurrentThemeMutable();

    if (colors.has("backgroundPrimary")) theme.backgroundPrimary = parseColor(colors["backgroundPrimary"].asString());
    if (colors.has("backgroundSecondary")) theme.backgroundSecondary = parseColor(colors["backgroundSecondary"].asString());
    if (colors.has("primary")) theme.primary = parseColor(colors["primary"].asString());
    if (colors.has("accentCyan")) theme.accentCyan = parseColor(colors["accentCyan"].asString());
    if (colors.has("accentMagenta")) theme.accentMagenta = parseColor(colors["accentMagenta"].asString());
    if (colors.has("textPrimary")) theme.textPrimary = parseColor(colors["textPrimary"].asString());
    if (colors.has("textSecondary")) theme.textSecondary = parseColor(colors["textSecondary"].asString());
    if (colors.has("error")) theme.error = parseColor(colors["error"].asString());
    if (colors.has("success")) theme.success = parseColor(colors["success"].asString());
    if (colors.has("warning")) theme.warning = parseColor(colors["warning"].asString());

    // Glass Aesthetic tokens
    if (colors.has("glassHover")) theme.glassHover = parseColor(colors["glassHover"].asString());
    if (colors.has("glassBorder")) theme.glassBorder = parseColor(colors["glassBorder"].asString());
    if (colors.has("glassActive")) theme.glassActive = parseColor(colors["glassActive"].asString());
}

void NUIConfigLoader::applyLayout(const Nomad::JSON& layout) {
    auto& themeManager = NUIThemeManager::getInstance();
    auto& theme = themeManager.getCurrentThemeMutable();

    if (layout.has("trackHeight")) theme.layout.trackHeight = parseDimension(layout["trackHeight"]);
    if (layout.has("trackControlsWidth")) theme.layout.trackControlsWidth = parseDimension(layout["trackControlsWidth"]);
    if (layout.has("fileBrowserWidth")) theme.layout.fileBrowserWidth = parseDimension(layout["fileBrowserWidth"]);
    if (layout.has("transportBarHeight")) theme.layout.transportBarHeight = parseDimension(layout["transportBarHeight"]);
    if (layout.has("transportButtonSize")) theme.layout.transportButtonSize = parseDimension(layout["transportButtonSize"]);
    if (layout.has("controlButtonWidth")) theme.layout.controlButtonWidth = parseDimension(layout["controlButtonWidth"]);
    if (layout.has("controlButtonHeight")) theme.layout.controlButtonHeight = parseDimension(layout["controlButtonHeight"]);
    if (layout.has("gridLineSpacing")) theme.layout.gridLineSpacing = parseDimension(layout["gridLineSpacing"]);
    if (layout.has("panelMargin")) theme.layout.panelMargin = parseDimension(layout["panelMargin"]);
    if (layout.has("componentPadding")) theme.layout.componentPadding = parseDimension(layout["componentPadding"]);
}

void NUIConfigLoader::applySpacing(const Nomad::JSON& spacing) {
    auto& themeManager = NUIThemeManager::getInstance();
    auto& theme = themeManager.getCurrentThemeMutable();

    if (spacing.has("panelMargin")) theme.spacingM = parseDimension(spacing["panelMargin"]);
    if (spacing.has("componentPadding")) theme.spacingS = parseDimension(spacing["componentPadding"]);
    if (spacing.has("buttonPadding")) theme.spacingXS = parseDimension(spacing["buttonPadding"]);
}

void NUIConfigLoader::applyTypography(const Nomad::JSON& typography) {
    auto& themeManager = NUIThemeManager::getInstance();
    auto& theme = themeManager.getCurrentThemeMutable();

    if (typography.has("fontSizeM")) theme.fontSizeM = parseDimension(typography["fontSizeM"]);
    if (typography.has("fontSizeS")) theme.fontSizeS = parseDimension(typography["fontSizeS"]);
    if (typography.has("fontSizeL")) theme.fontSizeL = parseDimension(typography["fontSizeL"]);
}

NUIColor NUIConfigLoader::parseColor(const std::string& colorStr) {
    std::string hex = colorStr;
    if (hex[0] == '#') hex = hex.substr(1);

    if (hex.length() == 6) {
        uint32_t color = std::stoul(hex, nullptr, 16);
        return NUIColor::fromHex(color);
    } else if (hex.length() == 8) {
        // Handle 8-character hex (AARRGGBB)
        uint32_t color = std::stoul(hex, nullptr, 16);
        float a = ((color >> 24) & 0xFF) / 255.0f;
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        return NUIColor(r, g, b, a);
    }

    return NUIColor::black(); // fallback
}

float NUIConfigLoader::parseDimension(const Nomad::JSON& value) {
    if (value.isNumber()) {
        return static_cast<float>(value.asNumber());
    } else if (value.isString()) {
        std::string str = value.asString();
        // Remove 'px' suffix if present
        if (str.size() > 2 && str.substr(str.size() - 2) == "px") {
            str = str.substr(0, str.size() - 2);
        }
        try {
            return std::stof(str);
        } catch (...) {
            return 0.0f;
        }
    }
    return 0.0f;
}

std::string NUIConfigLoader::colorToHex(const NUIColor& color) {
    uint32_t hex = color.toHex();
    std::stringstream ss;
    if (color.a < 0.999f) {
        // 8-character hex (AARRGGBB)
        ss << std::hex << std::setw(8) << std::setfill('0') << hex;
    } else {
        // 6-character hex (RRGGBB) - exclude alpha channel
        ss << std::hex << std::setw(6) << std::setfill('0') << (hex & 0xFFFFFF);
    }
    return ss.str();
}

} // namespace NomadUI