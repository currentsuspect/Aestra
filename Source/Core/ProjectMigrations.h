// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraCore/include/AestraJSON.h"
#include <string>
#include <functional>
#include <vector>

namespace Aestra {

/**
 * @brief Project schema migration system
 *
 * Handles version-to-version migrations when loading older project files.
 *
 * Version History:
 * - v1: Initial schema (Feb 2026)
 *
 * When adding breaking changes:
 * 1. Increment PROJECT_VERSION_CURRENT in ProjectSerializer.cpp
 * 2. Add migration function below
 * 3. Register in getMigrations()
 */

using MigrationFn = std::function<bool(JSON& root)>;

struct Migration {
    int fromVersion;
    int toVersion;
    MigrationFn migrate;
    std::string description;
};

class ProjectMigrations {
public:
    /**
     * @brief Get all registered migrations
     */
    static const std::vector<Migration>& getMigrations() {
        static std::vector<Migration> migrations = {
            // Example migration (uncomment when needed):
            // {
            //     1, 2,
            //     migrateV1ToV2,
            //     "Convert old clip format to new ClipInstance schema"
            // },
        };
        return migrations;
    }

    /**
     * @brief Run all migrations to bring a project to current version
     * @param root The parsed JSON root of the project file
     * @param fromVersion The version in the file
     * @param toVersion Target version (typically PROJECT_VERSION_CURRENT)
     * @return True if all migrations succeeded
     */
    static bool runMigrations(JSON& root, int fromVersion, int toVersion) {
        auto& migrations = getMigrations();

        int currentVersion = fromVersion;
        while (currentVersion < toVersion) {
            bool found = false;
            for (const auto& m : migrations) {
                if (m.fromVersion == currentVersion && m.toVersion == currentVersion + 1) {
                    if (!m.migrate(root)) {
                        return false;
                    }
                    currentVersion = m.toVersion;
                    found = true;
                    break;
                }
            }
            if (!found) {
                // No migration available for this version gap
                return false;
            }
        }

        // Update version in root
        root.set("version", JSON(static_cast<double>(toVersion)));
        return true;
    }

private:
    // =========================================================================
    // Migration Functions
    // =========================================================================

    // Example: Migrate from v1 to v2
    // static bool migrateV1ToV2(JSON& root) {
    //     // Rename old field to new field
    //     if (root.has("clips")) {
    //         JSON newClips = JSON::array();
    //         for (size_t i = 0; i < root["clips"].size(); ++i) {
    //             JSON clip = root["clips"][i];
    //             // Transform clip...
    //             newClips.push(clip);
    //         }
    //         root.set("clips", newClips);
    //     }
    //     return true;
    // }
};

} // namespace Aestra
