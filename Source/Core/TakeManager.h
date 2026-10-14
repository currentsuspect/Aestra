// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Manages project take snapshots — versioned copies of a project state.
 *
 * Each take is a self-contained snapshot stored under a takes/ directory
 * alongside the project file. A manifest tracks which take is active and
 * provides lookup by ID.
 */
class TakeManager {
public:
    /** @brief Metadata for a single take snapshot. */
    struct TakeEntry {
        std::string id;           ///< Unique take identifier.
        std::string name;         ///< Human-readable take name.
        std::string parentId;     ///< Parent take ID (empty for top-level).
        std::string snapshotPath; ///< Relative path to the snapshot file.
        uint64_t createdAtMs{0};  ///< Creation timestamp (epoch ms).
        uint64_t updatedAtMs{0};  ///< Last-modified timestamp (epoch ms).
        bool active{false};       ///< True if this is the currently active take.

        // --- Versions (SPEC-017, V81-V1). Additive: an older manifest has none of these. ---
        /** @brief What kind of version this is. */
        enum class Kind { Saved, Autosave };
        /** @brief One change made inside a version: the label, time and command type, written when it is saved. */
        struct Step {
            std::string label;        ///< e.g. "Move clip · Pad".
            std::string commandType;  ///< The command class, for grouping and colour.
            uint64_t atMs{0};         ///< When the step was made (epoch ms).
        };
        /** @brief A timestamped note on a version. */
        struct Note {
            uint64_t atMs{0};
            std::string text;
        };
        std::string branch;        ///< Name of the branch this version sits on ("Main" for the first).
        Kind kind{Kind::Saved};    ///< Saved by the user, or kept by the app on its own.
        std::string bounce;        ///< Relative path to a bounce of this version; empty when it has none.
        std::vector<Note> notes;   ///< Notes on this version.
        std::vector<Step> steps;   ///< Read-only log of the steps made in this version.
    };

    /** @brief The on-disk manifest listing all takes and the active selection. */
    struct Manifest {
        bool ok{false};                  ///< True if the manifest loaded successfully.
        std::string errorMessage;        ///< Error description when ok is false.
        std::string projectPath;         ///< Absolute path to the project directory.
        std::string activeTakeId;        ///< ID of the currently active take.
        std::vector<TakeEntry> takes;    ///< All takes in this project.

        /** @brief Look up a take by its unique ID. Returns nullptr if not found. */
        const TakeEntry* findTake(const std::string& id) const;
        /** @brief Returns the currently active take, or nullptr if none. */
        const TakeEntry* activeTake() const;
        /** @brief The distinct branch names, in the order their first version appears. */
        std::vector<std::string> branches() const;
    };

    /** @brief Result of a take operation — carries the affected take and manifest. */
    struct Result {
        bool ok{false};            ///< True if the operation succeeded.
        std::string errorMessage;  ///< Error description when ok is false.
        TakeEntry take;            ///< The take that was created/modified.
        Manifest manifest;         ///< The manifest state after the operation.
    };

    /** @brief Returns the takes/ directory path for a given project. */
    static std::string getTakesDirectory(const std::string& projectPath);
    /** @brief Returns the manifest.json path for a given project. */
    static std::string getManifestPath(const std::string& projectPath);
    /** @brief Returns the full snapshot file path for a specific take. */
    static std::string resolveSnapshotPath(const std::string& projectPath, const TakeEntry& take);

    /** @brief Loads the manifest from disk. Returns ok=false if missing or corrupt. */
    static Manifest loadManifest(const std::string& projectPath);

    /**
     * @brief Ensures a manifest exists, creating one with an initial take if needed.
     * @param projectPath          Path to the project directory.
     * @param currentProjectContents Serialized project data for the initial snapshot.
     * @param initialTakeName      Name for the auto-created take (default "Main").
     */
    static Result ensureManifest(const std::string& projectPath, const std::string& currentProjectContents,
                                 const std::string& initialTakeName = "Main");

    /**
     * @brief Overwrites the active take's snapshot with the current project state.
     * @param projectPath              Path to the project directory.
     * @param currentProjectContents   Serialized project data to save.
     */
    static Result saveActiveTake(const std::string& projectPath, const std::string& currentProjectContents);

    /**
     * @brief Creates a new take snapshot.
     * @param projectPath              Path to the project directory.
     * @param currentProjectContents   Serialized project data for the snapshot.
     * @param name                     Human-readable take name.
     * @param parentId                 Parent take ID (empty for top-level).
     */
    static Result createTake(const std::string& projectPath, const std::string& currentProjectContents,
                             const std::string& name, const std::string& parentId = "");

    /**
     * @brief Saves the state as a version on a branch (SPEC-017). An empty branch continues the parent's branch;
     *        a name starts a new branch from the parent. `steps` becomes the version's read-only step log.
     */
    static Result createVersion(const std::string& projectPath, const std::string& currentProjectContents,
                                const std::string& name, const std::string& parentId, const std::string& branch,
                                TakeEntry::Kind kind, const std::vector<TakeEntry::Step>& steps);

    /** @brief Replaces a version's step log (written when the version is saved). */
    static Result setVersionSteps(const std::string& projectPath, const std::string& takeId,
                                  const std::vector<TakeEntry::Step>& steps);

    /** @brief Records the bounce file (relative to the takes folder) on a version. */
    static Result setVersionBounce(const std::string& projectPath, const std::string& takeId, const std::string& relativePath);
    /** @brief Absolute path of a version's bounce, or empty when it has none or the path leaves the takes folder. */
    static std::string resolveBouncePath(const std::string& projectPath, const TakeEntry& take);

    /** @brief Appends a note to a version. */
    static Result addVersionNote(const std::string& projectPath, const std::string& takeId, const std::string& text);

    /**
     * @brief Switches the active take. Does not load the snapshot — caller must do that.
     * @param projectPath  Path to the project directory.
     * @param takeId       ID of the take to activate.
     */
    static Result setActiveTake(const std::string& projectPath, const std::string& takeId);

    /**
     * @brief Renames an existing take. Snapshot contents are untouched.
     * @param projectPath  Path to the project directory.
     * @param takeId       ID of the take to rename.
     * @param newName      New human-readable name (must be non-empty).
     */
    static Result renameTake(const std::string& projectPath, const std::string& takeId, const std::string& newName);

    /**
     * @brief Duplicates a take by copying its snapshot into a new take entry.
     *
     * The duplicate records the source take as its parent and does NOT become
     * active — the caller's working state is never touched.
     * @param projectPath  Path to the project directory.
     * @param takeId       ID of the take to duplicate.
     * @param newName      Name for the duplicate (empty derives "<source> Copy").
     * @param branch       Branch the copy starts; empty keeps the source's branch.
     */
    static Result duplicateTake(const std::string& projectPath, const std::string& takeId,
                                const std::string& newName = "", const std::string& branch = "");

    /**
     * @brief Deletes a take from the manifest and removes its snapshot.
     *
     * The active take can never be deleted (switch away first), so the working
     * state is always safe. Children of the deleted take are re-parented to
     * its parent to keep lineage intact. Success means the manifest was
     * updated; snapshot-file removal is best-effort and may leave an orphaned
     * (harmless, unreferenced) file behind.
     * @param projectPath  Path to the project directory.
     * @param takeId       ID of the take to delete.
     */
    static Result deleteTake(const std::string& projectPath, const std::string& takeId);
};
