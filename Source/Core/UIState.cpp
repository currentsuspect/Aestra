// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIState.h"
#include "AestraLog.h"
#include "AestraPlat/include/IPlatformUtils.h"
#include <filesystem>
#include <fstream>

namespace Aestra {

// =============================================================================
// Path Helper
// =============================================================================
std::string UIState::getUIStatePath() {
    IPlatformUtils* utils = Platform::getUtils();
    if (!utils) {
        return (std::filesystem::current_path() / "ui_state.json").string();
    }
    std::string appDataDir = utils->getAppDataPath("Aestra");
    std::error_code ec;
    std::filesystem::create_directories(appDataDir, ec);
    return (std::filesystem::path(appDataDir) / "ui_state.json").string();
}

// =============================================================================
// Load/Save
// =============================================================================
void UIState::load() {
    std::string path = getUIStatePath();
    
    if (!std::filesystem::exists(path)) {
        Log::info("[UIState] No UI state file found, using defaults");
        return;
    }
    
    std::ifstream file(path);
    if (!file) {
        Log::warning("[UIState] Could not open UI state file");
        return;
    }
    
    std::string jsonStr((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    try {
        JSON json = JSON::parse(jsonStr);
        fromJson(json);
        Log::info("[UIState] Loaded from: " + path);
    } catch (...) {
        Log::warning("[UIState] Failed to parse UI state file, using defaults");
    }
}

void UIState::save() {
    std::string path = getUIStatePath();
    std::string jsonStr = toJson().toString(2);
    
    std::error_code ec;
    std::filesystem::path tmpPath = path + ".tmp";
    
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            Log::warning("[UIState] Failed to write UI state file");
            return;
        }
        out.write(jsonStr.data(), static_cast<std::streamsize>(jsonStr.size()));
        out.flush();
    }
    
    // Replace existing file
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::remove(path, ec);
    }
    std::filesystem::rename(tmpPath, path, ec);
    
    Log::info("[UIState] Saved to: " + path);
}

// =============================================================================
// Private Helpers
// =============================================================================
JSON UIState::toJson() const {
    JSON root = JSON::object();
    
    // Window
    JSON window = JSON::object();
    window.set("x", JSON(static_cast<double>(windowX)));
    window.set("y", JSON(static_cast<double>(windowY)));
    window.set("width", JSON(static_cast<double>(windowWidth)));
    window.set("height", JSON(static_cast<double>(windowHeight)));
    window.set("maximized", JSON(maximized));
    root.set("window", window);
    
    // Panels
    JSON panels = JSON::object();
    panels.set("browserWidth", JSON(static_cast<double>(browserWidth)));
    panels.set("mixerHeight", JSON(static_cast<double>(mixerHeight)));
    panels.set("browserVisible", JSON(browserVisible));
    panels.set("mixerVisible", JSON(mixerVisible));
    root.set("panels", panels);
    
    // File browser
    JSON fileBrowser = JSON::object();
    JSON expanded = JSON::array();
    for (const auto& folder : expandedFolders) {
        expanded.push(JSON(folder));
    }
    fileBrowser.set("expandedFolders", expanded);
    fileBrowser.set("lastPath", JSON(lastBrowsedPath));
    root.set("fileBrowser", fileBrowser);
    
    // Track view
    JSON trackView = JSON::object();
    trackView.set("horizontalZoom", JSON(static_cast<double>(horizontalZoom)));
    trackView.set("verticalZoom", JSON(static_cast<double>(verticalZoom)));
    trackView.set("scrollX", JSON(static_cast<double>(scrollPositionX)));
    trackView.set("scrollY", JSON(static_cast<double>(scrollPositionY)));
    root.set("trackView", trackView);
    
    return root;
}

void UIState::fromJson(const JSON& json) {
    // Window
    if (json.has("window") && json["window"].isObject()) {
        const JSON& window = json["window"];
        if (window.has("x") && window["x"].isNumber()) {
            windowX = static_cast<int>(window["x"].asNumber());
        }
        if (window.has("y") && window["y"].isNumber()) {
            windowY = static_cast<int>(window["y"].asNumber());
        }
        if (window.has("width") && window["width"].isNumber()) {
            windowWidth = static_cast<int>(window["width"].asNumber());
        }
        if (window.has("height") && window["height"].isNumber()) {
            windowHeight = static_cast<int>(window["height"].asNumber());
        }
        if (window.has("maximized") && window["maximized"].isBool()) {
            maximized = window["maximized"].asBool();
        }
    }
    
    // Panels
    if (json.has("panels") && json["panels"].isObject()) {
        const JSON& panels = json["panels"];
        if (panels.has("browserWidth") && panels["browserWidth"].isNumber()) {
            browserWidth = static_cast<float>(panels["browserWidth"].asNumber());
        }
        if (panels.has("mixerHeight") && panels["mixerHeight"].isNumber()) {
            mixerHeight = static_cast<float>(panels["mixerHeight"].asNumber());
        }
        if (panels.has("browserVisible") && panels["browserVisible"].isBool()) {
            browserVisible = panels["browserVisible"].asBool();
        }
        if (panels.has("mixerVisible") && panels["mixerVisible"].isBool()) {
            mixerVisible = panels["mixerVisible"].asBool();
        }
    }
    
    // File browser
    if (json.has("fileBrowser") && json["fileBrowser"].isObject()) {
        const JSON& fileBrowser = json["fileBrowser"];
        if (fileBrowser.has("expandedFolders") && fileBrowser["expandedFolders"].isArray()) {
            expandedFolders.clear();
            for (size_t i = 0; i < fileBrowser["expandedFolders"].size(); ++i) {
                const JSON& item = fileBrowser["expandedFolders"][i];
                if (item.isString()) {
                    expandedFolders.insert(item.asString());
                }
            }
        }
        if (fileBrowser.has("lastPath") && fileBrowser["lastPath"].isString()) {
            lastBrowsedPath = fileBrowser["lastPath"].asString();
        }
    }
    
    // Track view
    if (json.has("trackView") && json["trackView"].isObject()) {
        const JSON& trackView = json["trackView"];
        if (trackView.has("horizontalZoom") && trackView["horizontalZoom"].isNumber()) {
            horizontalZoom = static_cast<float>(trackView["horizontalZoom"].asNumber());
        }
        if (trackView.has("verticalZoom") && trackView["verticalZoom"].isNumber()) {
            verticalZoom = static_cast<float>(trackView["verticalZoom"].asNumber());
        }
        if (trackView.has("scrollX") && trackView["scrollX"].isNumber()) {
            scrollPositionX = static_cast<float>(trackView["scrollX"].asNumber());
        }
        if (trackView.has("scrollY") && trackView["scrollY"].isNumber()) {
            scrollPositionY = static_cast<float>(trackView["scrollY"].asNumber());
        }
    }
}

} // namespace Aestra
