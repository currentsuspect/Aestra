// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraJSON.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace Aestra {

/**
 * @brief Mixer layout preferences that belong to the *application*, not a project.
 *
 * Switching projects must never rearrange the mixer, so this deliberately does
 * not go through ProjectSerializer. Mirrors browser_settings.json, which already
 * persists FileBrowser layout the same way.
 *
 * Split out of MixerPanel so the save/load round trip is reachable from a test
 * without constructing a panel, a TrackManager or a renderer — the same reason
 * InspectorCollapseState is its own header.
 */
struct MixerUIPreferences {
    /// The user's explicit choice. Never written from a width-derived collapse.
    bool inspectorExpanded{true};

    /// Directory holding Aestra's per-user config, or empty if it cannot be found.
    ///
    /// HOME alone is wrong on Windows, where it is normally unset — the mixer
    /// preferences then silently never persisted at all.
    static std::string configDirectory()
    {
        std::string home;
        if (const char* h = std::getenv("HOME"); h != nullptr && *h != '\0') {
            home = h;
        }
#if defined(_WIN32)
        if (home.empty()) {
            if (const char* up = std::getenv("USERPROFILE"); up != nullptr && *up != '\0') {
                home = up;
            }
        }
        if (home.empty()) {
            const char* drive = std::getenv("HOMEDRIVE");
            const char* path = std::getenv("HOMEPATH");
            if (drive != nullptr && *drive != '\0' && path != nullptr && *path != '\0') {
                home = std::string(drive) + path;
            }
        }
#endif
        if (home.empty()) {
            return {};
        }

        const std::filesystem::path dir = std::filesystem::path(home) / ".config" / "aestra";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            return {};
        }
        return dir.string();
    }

    /// Full path to mixer_settings.json, or empty when there is nowhere to put it.
    static std::string settingsPath()
    {
        const std::string dir = configDirectory();
        if (dir.empty()) {
            return {};
        }
        return (std::filesystem::path(dir) / "mixer_settings.json").string();
    }

    /**
     * @brief Read preferences from @p path.
     *
     * Anything unreadable, empty or malformed yields defaults rather than an
     * error: a corrupt settings file must not stop the mixer opening, and an
     * absent key must leave its default alone rather than applying false.
     */
    static MixerUIPreferences load(const std::string& path)
    {
        MixerUIPreferences prefs;
        if (path.empty()) {
            return prefs;
        }

        std::ifstream in(path);
        if (!in) {
            return prefs; // First run.
        }

        const std::string contents((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
        if (contents.empty()) {
            return prefs;
        }

        bool consumedAll = false;
        const JSON root = JSON::parseStrict(contents, consumedAll);
        if (!consumedAll) {
            return prefs;
        }
        if (root.has("inspectorExpanded")) {
            prefs.inspectorExpanded = root["inspectorExpanded"].asBool();
        }
        return prefs;
    }

    /// Write preferences to @p path. Returns false if nothing was written.
    bool save(const std::string& path) const
    {
        if (path.empty()) {
            return false;
        }

        JSON root = JSON::object();
        root.set("version", JSON(1.0));
        // Only the explicit preference is written. The width-derived collapse is
        // recomputed per layout and must never reach disk, or a session that
        // happened to end in a narrow window would rewrite the choice.
        root.set("inspectorExpanded", JSON(inspectorExpanded));

        std::ofstream out(path, std::ios::trunc);
        if (!out) {
            return false;
        }
        out << root.toString(2);
        return out.good();
    }
};

} // namespace Aestra
