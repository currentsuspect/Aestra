// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUITitleBarConfig.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace NomadUI {

NUITitleBarConfig& NUITitleBarConfig::getInstance() {
    static NUITitleBarConfig instance;
    return instance;
}

bool NUITitleBarConfig::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << filename << std::endl;
        // Try alternative paths
        std::string altPath1 = "NomadUI/" + filename;
        std::string altPath2 = "../" + filename;
        std::string altPath3 = "../../" + filename;
        
        std::ifstream altFile1(altPath1);
        if (altFile1.is_open()) {
            std::cout << "Found config file at: " << altPath1 << std::endl;
            file = std::move(altFile1);
        } else {
            std::ifstream altFile2(altPath2);
            if (altFile2.is_open()) {
                std::cout << "Found config file at: " << altPath2 << std::endl;
                file = std::move(altFile2);
            } else {
                std::ifstream altFile3(altPath3);
                if (altFile3.is_open()) {
                    std::cout << "Found config file at: " << altPath3 << std::endl;
                    file = std::move(altFile3);
                } else {
                    std::cerr << "Config file not found in any location" << std::endl;
                    return false;
                }
            }
        }
    }
    
    std::string line;
    std::string currentSection;
    
    while (std::getline(file, line)) {
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Check for section headers
        if (line.back() == ':') {
            currentSection = line.substr(0, line.length() - 1);
            continue;
        }
        
        // Parse key-value pairs
        std::string key, value;
        if (parseYamlLine(line, key, value)) {
            std::string fullKey = currentSection.empty() ? key : currentSection + "." + key;
            config_[fullKey] = value;
        }
    }
    
    file.close();
    std::cout << "Loaded " << config_.size() << " configuration values from " << filename << std::endl;
    return true;
}

float NUITitleBarConfig::getFloat(const std::string& key, float defaultValue) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        try {
            return std::stof(it->second);
        } catch (const std::exception&) {
            std::cerr << "Failed to parse float value for key: " << key << std::endl;
        }
    }
    return defaultValue;
}

int NUITitleBarConfig::getInt(const std::string& key, int defaultValue) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception&) {
            std::cerr << "Failed to parse int value for key: " << key << std::endl;
        }
    }
    return defaultValue;
}

std::string NUITitleBarConfig::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        // Remove quotes if present
        std::string value = it->second;
        if (value.length() >= 2 && value[0] == '"' && value.back() == '"') {
            value = value.substr(1, value.length() - 2);
        }
        return value;
    }
    return defaultValue;
}

bool NUITitleBarConfig::getBool(const std::string& key, bool defaultValue) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value == "true" || value == "1" || value == "yes";
    }
    return defaultValue;
}

std::string NUITitleBarConfig::trim(const std::string& str) const {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

std::vector<std::string> NUITitleBarConfig::split(const std::string& str, char delimiter) const {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

bool NUITitleBarConfig::parseYamlLine(const std::string& line, std::string& key, std::string& value) const {
    size_t colonPos = line.find(':');
    if (colonPos == std::string::npos) {
        return false;
    }
    
    key = trim(line.substr(0, colonPos));
    value = trim(line.substr(colonPos + 1));
    
    return !key.empty() && !value.empty();
}

} // namespace NomadUI
