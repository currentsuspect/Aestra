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

/**
 * @brief Outcome of one migration edge.
 *
 * WHAT COUNTS AS A TRANSFORMATION — this is a contract, not a hint. See
 * Aestra-Internals: aestra-docs/architecture/project-format-compatibility.md.
 *
 * A transformation is any load-time work producing an in-memory project whose
 * faithful re-serialization would differ SEMANTICALLY from the bytes on disk,
 * in a way the older schema cannot express.
 *
 * - `Unchanged`  — the step only inspected the document. Advancing the version
 *                  number is NOT a transformation: it happens on every edge and
 *                  says nothing about whether the session changed. Reporting
 *                  `Transformed` "to be safe" is equally wrong; it prompts the
 *                  user to save a document that did not change.
 * - `Transformed` — the step rewrote, defaulted, split, merged or reinterpreted
 *                  data. The application must mark the project modified so the
 *                  upgraded representation gets saved.
 * - `Failed`      — the document could not be brought forward. The caller must
 *                  leave the existing in-memory project untouched.
 *
 * The same standard binds the LOADER. Aestra allows the loader to read an older
 * shape natively instead of routing it through a migration function (see
 * migrateV2ToV3 below), but only when that interpretation is
 * representation-equivalent and round-trip stable: re-saving must yield a
 * document the loader reads back with identical semantics, dropping and
 * inventing nothing. A version-conditional loader branch that upgrades meaning
 * is a transformation and must be reported as one — it may not be hidden inside
 * the branch. The modified marker is the user's only signal that their project
 * was upgraded.
 */
enum class MigrationStepResult { Unchanged, Transformed, Failed };

enum class MigrationOutcome { None, Transformed, Failed };

using MigrationFn = std::function<MigrationStepResult(JSON& root)>;

/**
 * @brief Combine a migration-function outcome with a loader-side one.
 *
 * A load has two independent sources of transformation: the migration registry,
 * and version-conditional interpretation inside the loader itself. Both are
 * bound by the same contract (see MigrationStepResult), so the reported outcome
 * must be their combination — reporting only the registry's verdict is how a
 * loader-side upgrade goes silent.
 *
 * `Failed` dominates: a failed load must not be reported as merely transformed.
 * Otherwise `Transformed` dominates `None`, because any transformation anywhere
 * means the in-memory project no longer matches the bytes on disk.
 */
inline MigrationOutcome combineMigrationOutcome(MigrationOutcome a, MigrationOutcome b) noexcept {
    if (a == MigrationOutcome::Failed || b == MigrationOutcome::Failed) {
        return MigrationOutcome::Failed;
    }
    if (a == MigrationOutcome::Transformed || b == MigrationOutcome::Transformed) {
        return MigrationOutcome::Transformed;
    }
    return MigrationOutcome::None;
}

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
    // v1 and v2 differ only in the identity guarantees the writer made; every
    // v1 field means in v2 exactly what it meant in v1. Nothing to transform.
    static MigrationStepResult migrateV1ToV2(JSON&) { return MigrationStepResult::Unchanged; }

    // Deliberately a no-op. The v3 loader reads v2's lane-local mixer state
    // natively and derives channel records from it, rather than rewriting the
    // JSON here.
    //
    // That is permitted only because the interpretation is
    // representation-equivalent and round-trip stable, which is not asserted by
    // this comment but PROVEN by the historical-fixture tests: an authentic v2
    // document with a topology that would expose merging or positional
    // collision loads, re-saves as v3, and reloads with identical semantics.
    // If that ever stops holding, this function must start reporting
    // `Transformed` (and do the rewrite) rather than the loader quietly
    // upgrading meaning.
    static MigrationStepResult migrateV2ToV3(JSON&) { return MigrationStepResult::Unchanged; }
};

} // namespace Aestra
