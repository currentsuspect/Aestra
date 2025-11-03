// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIThemeSystem.h"
#include "../../NomadCore/include/NomadJSON.h"
#include <string>
#include <fstream>
#include <iostream>

namespace NomadUI {

/**
 * @brief Configuration loader for YAML-based theme and layout customization
 *
 * Loads YAML configuration files and applies them to the theme system.
 * Supports runtime customization of colors, dimensions, spacing, and layout properties.
 */
class NUIConfigLoader {
public:
    static NUIConfigLoader& getInstance();

    /**
     * Load configuration from YAML file
     * @param filePath Path to the YAML configuration file
     * @return true if loaded successfully, false otherwise
     */
    bool loadConfig(const std::string& filePath);

    /**
     * Load configuration from YAML string
     * @param yamlContent YAML content as string
     * @return true if loaded successfully, false otherwise
     */
    bool loadConfigFromString(const std::string& yamlContent);

    /**
     * Apply loaded configuration to current theme
     */
    void applyConfig();

    /**
     * Save current theme configuration to YAML file
     * @param filePath Path to save the YAML file
     */
    void saveConfig(const std::string& filePath);

    /**
     * Get loaded configuration as JSON
     */
    const Nomad::JSON& getConfig() const { return config_; }

private:
    NUIConfigLoader() = default;

    void setNestedValue(Nomad::JSON& json, const std::string& path, const std::string& value);
    std::string colorToHex(const NUIColor& color);

    bool parseYAML(const std::string& content);
    void applyColors(const Nomad::JSON& colors);
    void applyLayout(const Nomad::JSON& layout);
    void applySpacing(const Nomad::JSON& spacing);
    void applyTypography(const Nomad::JSON& typography);

    NUIColor parseColor(const std::string& colorStr);
    float parseDimension(const Nomad::JSON& value);

    Nomad::JSON config_;
    bool configLoaded_ = false;
};

} // namespace NomadUI