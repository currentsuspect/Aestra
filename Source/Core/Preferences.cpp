// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Preferences.h"
#include "AestraLog.h"
#include <filesystem>
#include <fstream>

namespace Aestra {

// =============================================================================
// Singleton
// =============================================================================
Preferences& Preferences::instance() {
    static Preferences instance;
    return instance;
}

// =============================================================================
// Path Helpers
// =============================================================================
std::string Preferences::getPreferencesPath() {
    IPlatformUtils* utils = Platform::getUtils();
    if (!utils) {
        return (std::filesystem::current_path() / "preferences.json").string();
    }
    std::string appDataDir = utils->getAppDataPath("Aestra");
    std::error_code ec;
    std::filesystem::create_directories(appDataDir, ec);
    return (std::filesystem::path(appDataDir) / "preferences.json").string();
}

std::string Preferences::getRecentFilesPath() {
    IPlatformUtils* utils = Platform::getUtils();
    if (!utils) {
        return (std::filesystem::current_path() / "recent_files.json").string();
    }
    std::string appDataDir = utils->getAppDataPath("Aestra");
    std::error_code ec;
    std::filesystem::create_directories(appDataDir, ec);
    return (std::filesystem::path(appDataDir) / "recent_files.json").string();
}

// =============================================================================
// Load/Save
// =============================================================================
void Preferences::load() {
    std::string prefPath = getPreferencesPath();
    std::string recentPath = getRecentFilesPath();
    
    // Create defaults if files don't exist
    if (!std::filesystem::exists(prefPath)) {
        Log::info("[Preferences] No preferences file found, creating defaults");
        createDefaults();
        save();
        return;
    }
    
    // Load preferences
    std::ifstream prefFile(prefPath);
    if (prefFile) {
        std::string jsonStr((std::istreambuf_iterator<char>(prefFile)),
                            std::istreambuf_iterator<char>());
        if (!loadFromJson(jsonStr)) {
            Log::warning("[Preferences] Failed to parse preferences, using defaults");
            createDefaults();
        } else {
            Log::info("[Preferences] Loaded from: " + prefPath);
        }
    } else {
        Log::warning("[Preferences] Could not open preferences file, using defaults");
        createDefaults();
    }
    
    // Load recent files separately
    std::ifstream recentFile(recentPath);
    if (recentFile) {
        std::string jsonStr((std::istreambuf_iterator<char>(recentFile)),
                            std::istreambuf_iterator<char>());
        try {
            JSON json = JSON::parse(jsonStr);
            if (json.has("recentFiles") && json["recentFiles"].isArray()) {
                recentFiles.clear();
                for (size_t i = 0; i < json["recentFiles"].size(); ++i) {
                    const JSON& item = json["recentFiles"][i];
                    if (item.isString()) {
                        recentFiles.push_back(item.asString());
                    }
                }
            }
        } catch (...) {
            Log::warning("[Preferences] Failed to parse recent files");
        }
    }
}

void Preferences::save() {
    std::string prefPath = getPreferencesPath();
    std::string recentPath = getRecentFilesPath();
    
    // Save preferences atomically
    std::string jsonStr = toJson().toString(2);
    
    std::error_code ec;
    std::filesystem::path tmpPath = prefPath + ".tmp";
    
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(jsonStr.data(), static_cast<std::streamsize>(jsonStr.size()));
            out.flush();
        }
    }
    
    // Replace existing file
    if (std::filesystem::exists(prefPath, ec)) {
        std::filesystem::remove(prefPath, ec);
    }
    std::filesystem::rename(tmpPath, prefPath, ec);
    
    // Save recent files separately
    JSON recentJson = JSON::object();
    JSON filesArray = JSON::array();
    for (const auto& path : recentFiles) {
        filesArray.push(JSON(path));
    }
    recentJson.set("recentFiles", filesArray);
    
    std::string recentJsonStr = recentJson.toString(2);
    std::filesystem::path recentTmpPath = recentPath + ".tmp";
    
    {
        std::ofstream out(recentTmpPath, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(recentJsonStr.data(), static_cast<std::streamsize>(recentJsonStr.size()));
            out.flush();
        }
    }
    
    if (std::filesystem::exists(recentPath, ec)) {
        std::filesystem::remove(recentPath, ec);
    }
    std::filesystem::rename(recentTmpPath, recentPath, ec);
    
    Log::info("[Preferences] Saved to: " + prefPath);
}

void Preferences::addRecentFile(const std::string& path) {
    if (path.empty()) return;
    
    // Remove if already exists
    auto it = std::find(recentFiles.begin(), recentFiles.end(), path);
    if (it != recentFiles.end()) {
        recentFiles.erase(it);
    }
    
    // Add to front
    recentFiles.insert(recentFiles.begin(), path);
    
    // Trim to max
    if (recentFiles.size() > MAX_RECENT_FILES) {
        recentFiles.resize(MAX_RECENT_FILES);
    }
}

void Preferences::clearRecentFiles() {
    recentFiles.clear();
}

// =============================================================================
// Private Helpers
// =============================================================================
void Preferences::createDefaults() {
    audioDeviceId = "default";
    sampleRate = 48000;
    bufferSize = 512;
    exclusiveMode = false;
    showGrid = true;
    snapToGrid = true;
    gridSize = 0.25f;
    theme = "nomad-dark";
    autoSaveEnabled = true;
    autoSaveIntervalSeconds = 300;
    recentFiles.clear();
}

bool Preferences::loadFromJson(const std::string& jsonStr) {
    try {
        JSON json = JSON::parse(jsonStr);
        fromJson(json);
        return true;
    } catch (...) {
        return false;
    }
}

JSON Preferences::toJson() const {
    JSON root = JSON::object();
    
    // Audio
    JSON audio = JSON::object();
    audio.set("deviceId", JSON(audioDeviceId));
    audio.set("sampleRate", JSON(static_cast<double>(sampleRate)));
    audio.set("bufferSize", JSON(static_cast<double>(bufferSize)));
    audio.set("exclusiveMode", JSON(exclusiveMode));
    root.set("audio", audio);
    
    // UI
    JSON ui = JSON::object();
    ui.set("theme", JSON(theme));
    ui.set("showGrid", JSON(showGrid));
    ui.set("snapToGrid", JSON(snapToGrid));
    ui.set("gridSize", JSON(static_cast<double>(gridSize)));
    root.set("ui", ui);
    
    // Auto-save
    JSON autoSave = JSON::object();
    autoSave.set("enabled", JSON(autoSaveEnabled));
    autoSave.set("intervalSeconds", JSON(static_cast<double>(autoSaveIntervalSeconds)));
    root.set("autoSave", autoSave);
    
    return root;
}

void Preferences::fromJson(const JSON& json) {
    // Audio
    if (json.has("audio") && json["audio"].isObject()) {
        const JSON& audio = json["audio"];
        if (audio.has("deviceId") && audio["deviceId"].isString()) {
            audioDeviceId = audio["deviceId"].asString();
        }
        if (audio.has("sampleRate") && audio["sampleRate"].isNumber()) {
            sampleRate = static_cast<int>(audio["sampleRate"].asNumber());
        }
        if (audio.has("bufferSize") && audio["bufferSize"].isNumber()) {
            bufferSize = static_cast<int>(audio["bufferSize"].asNumber());
        }
        if (audio.has("exclusiveMode") && audio["exclusiveMode"].isBool()) {
            exclusiveMode = audio["exclusiveMode"].asBool();
        }
    }
    
    // UI
    if (json.has("ui") && json["ui"].isObject()) {
        const JSON& ui = json["ui"];
        if (ui.has("theme") && ui["theme"].isString()) {
            theme = ui["theme"].asString();
        }
        if (ui.has("showGrid") && ui["showGrid"].isBool()) {
            showGrid = ui["showGrid"].asBool();
        }
        if (ui.has("snapToGrid") && ui["snapToGrid"].isBool()) {
            snapToGrid = ui["snapToGrid"].asBool();
        }
        if (ui.has("gridSize") && ui["gridSize"].isNumber()) {
            gridSize = static_cast<float>(ui["gridSize"].asNumber());
        }
    }
    
    // Auto-save
    if (json.has("autoSave") && json["autoSave"].isObject()) {
        const JSON& autoSave = json["autoSave"];
        if (autoSave.has("enabled") && autoSave["enabled"].isBool()) {
            autoSaveEnabled = autoSave["enabled"].asBool();
        }
        if (autoSave.has("intervalSeconds") && autoSave["intervalSeconds"].isNumber()) {
            autoSaveIntervalSeconds = static_cast<int>(autoSave["intervalSeconds"].asNumber());
        }
    }
}

} // namespace Aestra
