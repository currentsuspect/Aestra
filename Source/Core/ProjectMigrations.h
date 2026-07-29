// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraCore/include/AestraJSON.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief Project schema migration system
 *
 * Handles version-to-version migrations when loading older project files.
 *
 * Version History:
 * - v1: Initial schema (Feb 2026)
 * - v2: Stable identity baseline
 * - v3: Playlist lanes and mixer channels persist independently
 *
 * When adding breaking changes:
 * 1. Increment PROJECT_VERSION_CURRENT in ProjectSerializer.h
 * 2. Add migration function below
 * 3. Register in getMigrations()
 */

enum class MigrationStepResult { Unchanged, Transformed, Failed };

enum class MigrationOutcome { None, Transformed, Failed };

using MigrationFn = std::function<MigrationStepResult(JSON& root)>;

struct Migration {
    int fromVersion;
    int toVersion;
    MigrationFn migrate;
    std::string description;
};

struct MigrationResult {
    MigrationOutcome outcome{MigrationOutcome::None};
    int resultingVersion{0};

    bool ok() const noexcept { return outcome != MigrationOutcome::Failed; }
};

class ProjectMigrations {
public:
    /**
     * @brief Get all registered migrations
     */
    static const std::vector<Migration>& getMigrations() {
        static const std::vector<Migration> migrations = {
            {1, 2, migrateV1ToV2, "No-op migration marker for v2 schema baseline"},
            {2, 3, migrateV2ToV3, "Separate Playlist placement from source-level mixer routing"},
        };
        return migrations;
    }

    /**
     * @brief Run all migrations to bring a project to current version
     * @param root The parsed JSON root of the project file
     * @param fromVersion The version in the file
     * @param toVersion Target version (typically PROJECT_VERSION_CURRENT)
     * @return Migration result, including whether any edge transformed data
     */
    static MigrationResult runMigrations(JSON& root, int fromVersion, int toVersion) {
        return runMigrationsWith(root, fromVersion, toVersion, getMigrations());
    }

    /**
     * @brief Validate that a registry has exactly one adjacent edge per supported version.
     */
    static bool validateRegistry(const std::vector<Migration>& migrations, int minSupportedVersion, int currentVersion,
                                 std::string* error = nullptr) {
        if (minSupportedVersion < 1 || currentVersion < minSupportedVersion) {
            if (error) {
                *error = "invalid supported project-version range";
            }
            return false;
        }

        std::vector<int> edgeCounts(static_cast<size_t>(currentVersion - minSupportedVersion), 0);
        for (const auto& migration : migrations) {
            if (migration.fromVersion < minSupportedVersion || migration.fromVersion >= currentVersion ||
                migration.toVersion != migration.fromVersion + 1) {
                if (error) {
                    *error = "migration edge must be adjacent and inside the supported range";
                }
                return false;
            }
            ++edgeCounts[static_cast<size_t>(migration.fromVersion - minSupportedVersion)];
        }

        for (size_t index = 0; index < edgeCounts.size(); ++index) {
            if (edgeCounts[index] != 1) {
                if (error) {
                    const int fromVersion = minSupportedVersion + static_cast<int>(index);
                    *error = edgeCounts[index] == 0 ? "missing migration edge from v" + std::to_string(fromVersion)
                                                    : "duplicate migration edge from v" + std::to_string(fromVersion);
                }
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Run a supplied registry through the production migration runner.
     *
     * This overload keeps completeness and transformation behavior executable
     * without inventing a production schema bump solely for a regression test.
     */
    static MigrationResult runMigrationsWith(JSON& root, int fromVersion, int toVersion,
                                             const std::vector<Migration>& migrations) {
        if (fromVersion > toVersion) {
            return {MigrationOutcome::Failed, fromVersion};
        }

        MigrationOutcome outcome = MigrationOutcome::None;
        int currentVersion = fromVersion;
        while (currentVersion < toVersion) {
            const Migration* edge = nullptr;
            for (const auto& m : migrations) {
                if (m.fromVersion == currentVersion && m.toVersion == currentVersion + 1) {
                    if (edge != nullptr) {
                        return {MigrationOutcome::Failed, currentVersion};
                    }
                    edge = &m;
                }
            }
            if (edge == nullptr) {
                return {MigrationOutcome::Failed, currentVersion};
            }

            const MigrationStepResult stepResult = edge->migrate(root);
            if (stepResult == MigrationStepResult::Failed) {
                return {MigrationOutcome::Failed, currentVersion};
            }
            if (stepResult == MigrationStepResult::Transformed) {
                outcome = MigrationOutcome::Transformed;
            }

            currentVersion = edge->toVersion;
            root.set("version", JSON(static_cast<double>(currentVersion)));
        }

        return {outcome, currentVersion};
    }

private:
    static MigrationStepResult migrateV1ToV2(JSON&) { return MigrationStepResult::Unchanged; }

    // The v3 loader understands v2 lane-local mixer state and performs the
    // source-route migration after stable channel identities are restored.
    static MigrationStepResult migrateV2ToV3(JSON&) { return MigrationStepResult::Unchanged; }
};

} // namespace Aestra
