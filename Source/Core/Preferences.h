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
    
    // WHICH OF THESE ACTUALLY GOVERN ANYTHING
    //
    // A persisted field that nothing reads is worse than a missing one: it makes
    // a setting look configurable, and any code written against it validates,
    // persists, reports success and changes nothing. Each field below says which
    // it is. Keep this accurate — the audit that produced it is in
    // docs/technical/settings_view_state_map.md (B4).

    // --- Audio settings: NOT WIRED -------------------------------------------
    // Shadowed by AudioDeviceManager, which is the live authority and never
    // consults these. Device choice therefore does not survive a restart.
    // Wiring them means restoring a device by id at startup and handling the
    // case where it is gone — a feature with real failure modes, not a
    // one-liner. Do not write a settings path against these until that is done.
    std::string audioDeviceId = "default";
    int sampleRate = 48000;
    int bufferSize = 512;
    bool exclusiveMode = false;

    // --- UI settings ----------------------------------------------------------
    // showGrid / snapToGrid / gridSize: NOT WIRED. Shadowed by TrackManagerUI's
    // own m_snapEnabled / m_snapSetting, which are the live authority. Snap
    // choice does not survive a restart.
    bool showGrid = true;
    bool snapToGrid = true;
    float gridSize = 0.25f;  // beats
    // theme: LIVE. Written by AppearanceSettingsPage::applyChanges, read at
    // startup by AestraWindowManager. The live authority is NUIThemeManager, so
    // anything changing the theme must write both.
    std::string theme = "Aestra-dark";

    // --- Auto-save: LIVE ------------------------------------------------------
    // Both are read at startup by AestraApp and applied to AutosaveManager.
    bool autoSaveEnabled = true;
    int autoSaveIntervalSeconds = 300;  // clamped to [10, 3600] on load

    // --- Recent files: NOT WIRED ---------------------------------------------
    // The storage, cap, and add/clear helpers below are implemented and correct;
    // nothing calls them and no UI surfaces the list. Kept rather than deleted
    // because a Recent Files menu is a plausible near-term feature and this is
    // the plumbing it would need — but until something calls addRecentFile(),
    // this list is always empty.
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
