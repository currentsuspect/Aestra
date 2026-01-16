// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <map>

namespace NomadUI {

/**
 * @brief Singleton for managing slider configurations
 */
class NUISliderConfig {
public:
    static NUISliderConfig& getInstance();

    bool loadFromFile(const std::string& filePath);
    void reload();

    int getInt(const std::string& key, int defaultValue = 0) const;
    float getFloat(const std::string& key, float defaultValue = 0.0f) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;

    void setInt(const std::string& key, int value);
    void setFloat(const std::string& key, float value);
    void setBool(const std::string& key, bool value);

private:
    NUISliderConfig() = default;
    void parseLine(const std::string& line);
    std::string trim(const std::string& str) const;

    std::map<std::string, std::string> config_;
    std::string configFilePath_;
};

} // namespace NomadUI
