// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>

namespace NomadUI {

/**
 * Configuration loader for title bar settings
 * Loads settings from YAML files and provides easy access
 */
class NUITitleBarConfig {
public:
    static NUITitleBarConfig& getInstance();
    
    // Load configuration from file
    bool loadFromFile(const std::string& filename);
    
    // Get configuration values
    float getFloat(const std::string& key, float defaultValue = 0.0f) const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    
    // Convenience methods for common values
    float getIconSizeMultiplier() const { return getFloat("icons.size_multiplier", 0.6f); }
    float getIconLineThickness() const { return getFloat("icons.line_thickness", 3.0f); }
    float getCloseLineThickness() const { return getFloat("icons.close_line_thickness", 3.0f); }
    
    float getTextFontSizeMultiplier() const { return getFloat("text.font_size_multiplier", 0.4f); }
    float getTextLeftPadding() const { return getFloat("text.left_padding", 2.0f); }
    bool getTextVerticalCenter() const { return getBool("text.vertical_center", true); }
    
    float getButtonWidth() const { return getFloat("buttons.width", 32.0f); }
    float getButtonHeight() const { return getFloat("buttons.height", 32.0f); }
    float getButtonRightPadding() const { return getFloat("buttons.right_padding", 4.0f); }
    float getButtonSpacing() const { return getFloat("buttons.spacing", 2.0f); }
    
    std::string getBackgroundColor() const { return getString("colors.background", "#1a1a1a"); }
    std::string getTextColor() const { return getString("colors.text", "#ffffff"); }
    std::string getIconColor() const { return getString("colors.icon", "#ffffff"); }
    std::string getHoverMinimizeColor() const { return getString("colors.hover_minimize", "#a855f7"); }
    std::string getHoverMaximizeColor() const { return getString("colors.hover_maximize", "#a855f7"); }
    std::string getHoverCloseColor() const { return getString("colors.hover_close", "#dc2626"); }
    std::string getPressMinimizeColor() const { return getString("colors.press_minimize", "#a855f7"); }
    std::string getPressMaximizeColor() const { return getString("colors.press_maximize", "#a855f7"); }
    std::string getPressCloseColor() const { return getString("colors.press_close", "#dc2626"); }
    
    float getHoverAlpha() const { return getFloat("effects.hover_alpha", 0.8f); }
    float getPressAlpha() const { return getFloat("effects.press_alpha", 0.9f); }
    float getBorderWidth() const { return getFloat("effects.border_width", 1.0f); }

private:
    NUITitleBarConfig() = default;
    ~NUITitleBarConfig() = default;
    NUITitleBarConfig(const NUITitleBarConfig&) = delete;
    NUITitleBarConfig& operator=(const NUITitleBarConfig&) = delete;
    
    std::map<std::string, std::string> config_;
    
    // Helper methods
    std::string trim(const std::string& str) const;
    std::vector<std::string> split(const std::string& str, char delimiter) const;
    bool parseYamlLine(const std::string& line, std::string& key, std::string& value) const;
};

} // namespace NomadUI
