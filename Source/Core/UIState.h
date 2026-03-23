// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraJSON.h"
#include <set>
#include <string>

namespace Aestra {

// =============================================================================
// UI State - Persisted window/panel states (not user preferences)
// =============================================================================
struct UIState {
    // Window geometry
    int windowX = 100;
    int windowY = 100;
    int windowWidth = 1280;
    int windowHeight = 720;
    bool maximized = false;
    
    // Panel sizes
    float browserWidth = 250.0f;
    float mixerHeight = 200.0f;
    bool browserVisible = true;
    bool mixerVisible = true;
    
    // File browser state
    std::set<std::string> expandedFolders;
    std::string lastBrowsedPath;
    
    // Track view state
    float horizontalZoom = 1.0f;
    float verticalZoom = 1.0f;
    float scrollPositionX = 0.0f;
    float scrollPositionY = 0.0f;
    
    // Load/save
    void load();
    void save();
    
    // Path helper
    static std::string getUIStatePath();
    
private:
    JSON toJson() const;
    void fromJson(const JSON& json);
};

} // namespace Aestra
