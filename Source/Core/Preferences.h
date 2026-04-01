// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraJSON.h"
#include <string>
#include <vector>
#include <filesystem>

namespace Aestra {

// =============================================================================
// User Preferences - Persisted across sessions
// =============================================================================
class Preferences {
public:
    static Preferences& instance();
    
    // Audio settings
    std::string audioDeviceId = "default";
    int sampleRate = 48000;
    int bufferSize = 512;
    bool exclusiveMode = false;
    
    // UI settings
    bool showGrid = true;
    bool snapToGrid = true;
    float gridSize = 0.25f;  // beats
    std::string theme = "Aestra-dark";
    
    // Auto-save
    bool autoSaveEnabled = true;
    int autoSaveIntervalSeconds = 300;  // 5 minutes default
    
    // Recent files
    std::vector<std::string> recentFiles;
    static constexpr size_t MAX_RECENT_FILES = 10;
    
    // Load/save
    void load();
    void save();
    void addRecentFile(const std::string& path);
    void clearRecentFiles();
    
    // Path helpers
    static std::string getPreferencesPath();
    static std::string getRecentFilesPath();
    
private:
    Preferences() = default;
    ~Preferences() = default;
    Preferences(const Preferences&) = delete;
    Preferences& operator=(const Preferences&) = delete;
    
    void createDefaults();
    bool loadFromJson(const std::string& jsonStr);
    JSON toJson() const;
    void fromJson(const JSON& json);
};

} // namespace Aestra
