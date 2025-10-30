// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUISliderConfig.h"
#include <algorithm>
#include <cctype>

namespace NomadUI {

NUISliderConfig& NUISliderConfig::getInstance() {
    static NUISliderConfig instance;
    return instance;
}

bool NUISliderConfig::loadFromFile(const std::string& filePath) {
    configFilePath_ = filePath;
    config_.clear();
    
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        parseLine(line);
    }
    
    file.close();
    return true;
}

int NUISliderConfig::getInt(const std::string& key, int defaultValue) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        try {
            return std::stoi(it->second);
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

float NUISliderConfig::getFloat(const std::string& key, float defaultValue) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        try {
            return std::stof(it->second);
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

bool NUISliderConfig::getBool(const std::string& key, bool defaultValue) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value == "true" || value == "1" || value == "yes";
    }
    return defaultValue;
}

void NUISliderConfig::setInt(const std::string& key, int value) {
    config_[key] = std::to_string(value);
}

void NUISliderConfig::setFloat(const std::string& key, float value) {
    config_[key] = std::to_string(value);
}

void NUISliderConfig::setBool(const std::string& key, bool value) {
    config_[key] = value ? "true" : "false";
}

void NUISliderConfig::reload() {
    if (!configFilePath_.empty()) {
        loadFromFile(configFilePath_);
    }
}

void NUISliderConfig::parseLine(const std::string& line) {
    // Skip empty lines and comments
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
        return;
    }
    
    // Parse key=value format
    size_t pos = trimmed.find('=');
    if (pos != std::string::npos) {
        std::string key = trim(trimmed.substr(0, pos));
        std::string value = trim(trimmed.substr(pos + 1));
        config_[key] = value;
    }
}

std::string NUISliderConfig::trim(const std::string& str) const {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

} // namespace NomadUI


