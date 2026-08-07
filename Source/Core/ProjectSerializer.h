// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraCore/include/AestraJSON.h"
#include "ProjectMigrations.h"
#include "TrackManager.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {

enum class LoadIssueSeverity {
    Warning,
    Error
};

struct LoadIssue {
    LoadIssueSeverity severity;
    std::string category;
    std::string message;
    uint64_t objectId{0};
    std::string referenceId;
    std::string context;
};

struct ProjectLoadReport {
    std::vector<LoadIssue> issues;
    bool hasErrors() const {
        for (const auto& i : issues) {
            if (i.severity == LoadIssueSeverity::Error) return true;
        }
        return false;
    }
    bool hasWarnings() const {
        for (const auto& i : issues) {
            if (i.severity == LoadIssueSeverity::Warning) return true;
        }
        return false;
    }
};

}

class ProjectSerializer {
public:
    static constexpr int PROJECT_VERSION_CURRENT = 3;
    static constexpr int PROJECT_VERSION_MIN_SUPPORTED = 1;

    struct PanelState {
        std::string title;
        double x{0.0};
        double y{0.0};
        double width{0.0};
        double height{0.0};
        double expandedHeight{0.0};
        bool minimized{false};
        bool maximized{false};
        bool userPositioned{false};
    };

    struct UIState {
        bool settingsDialogVisible{false};
        std::string settingsDialogActivePage;
        std::vector<PanelState> panels;

        // Phase-3 workspace state. All three are OPTIONAL in project files:
        // files saved before they existed load with the historical defaults
        // (Timeline workspace, overlays closed) — see the loader's has() guards.
        // `viewFocus` matches WorkspaceFocusModel::workspaceFocusName
        // ("arsenal|timeline|audition|routingMap"); empty means the loader
        // keeps the default Timeline focus. The piano roll is a contextual
        // editor, not a workspace: its remembered-open flag is `pianoRollOpen`,
        // and the legacy "pianoRoll" focus value (phase-3 builds) is rejected
        // by the loader and degrades to Timeline.
        std::string viewFocus;
        bool pianoRollOpen{false};
        bool sequencerOpen{false};
    };

    /**
     * @brief Content-integrity verdict for a loaded project file (#263).
     *
     * Corruption detection, NOT tamper-proofing: the checksum is keyless, so
     * anyone can recompute it. It exists to catch disk corruption, truncated
     * writes, and accidental edits — never to authenticate a file.
     * - Unchecked: file predates the integrity field (or unknown algo) — no warning.
     * - Verified:  stored checksum matches the recomputed one.
     * - Mismatch:  content changed since save; load proceeds non-destructively
     *              with a loud warning (structural corruption still hard-fails
     *              through the existing validators).
     */
    enum class LoadIntegrity { Unchecked, Verified, Mismatch };

    /**
     * @brief A plugin referenced by the project that could not be instantiated (#647).
     *
     * Deliberately NOT folded into `missingAssets`, which carries filesystem
     * paths for samples. A plugin id is not a path, cannot be relinked by
     * browsing for a file, and its slot state is retained in the project rather
     * than dropped — so giving both the same collection would make the API lie
     * about what a caller can do with the entry.
     */
    struct MissingPlugin {
        std::string pluginId; ///< The id as stored in the project file.
        std::string location; ///< Human-readable owner: mixer channel or lane name.
    };

    struct LoadResult {
        bool ok{false};
        double tempo{120.0};
        double playhead{0.0};
        int sourceSchemaVersion{0};
        int resultingSchemaVersion{0};
        /// Combined verdict: migration functions AND loader-side interpretation.
        /// See Aestra::combineMigrationOutcome.
        Aestra::MigrationOutcome migrationOutcome{Aestra::MigrationOutcome::None};

        /// Pre-v3 audio patterns the loader had to split into one pattern per
        /// destination channel. Non-zero means patterns exist in memory that are
        /// not in the file, which is why such a load reports `Transformed`.
        size_t legacyAudioPatternsSplit{0};

        std::string errorMessage;
        std::vector<std::string> missingAssets;
        std::vector<MissingPlugin> missingPlugins;
        LoadIntegrity integrity{LoadIntegrity::Unchecked};

        std::optional<UIState> ui;
        std::unique_ptr<::Aestra::ProjectLoadReport> report;

        bool schemaVersionAdvanced() const noexcept {
            return sourceSchemaVersion > 0 && resultingSchemaVersion > sourceSchemaVersion;
        }

        /// True only when an actual transformation occurred. A schema version
        /// bump alone must never make this true — see schemaVersionAdvanced().
        bool requiresSaveAfterLoad() const noexcept {
            return migrationOutcome == Aestra::MigrationOutcome::Transformed;
        }
    };

    struct SerializeResult {
        bool ok{false};
        std::string contents;
    };

    struct CandidateLoadResult {
        LoadResult result;
        std::string loadedPath;
    };

    struct HistoryEntry {
        std::string path;
        std::string label;
        uint64_t sizeBytes{0};
        std::chrono::system_clock::time_point timestamp{};
    };

    static bool save(const std::string& path,
                     const std::shared_ptr<Aestra::Audio::TrackManager>& trackManager,
                     double tempo,
                     double playheadSeconds,
                     const UIState* uiState = nullptr);

    // Serialize project state into a JSON string (used for async autosave).
    // indentSpaces=0 produces compact output (faster + smaller).
    static SerializeResult serialize(const std::shared_ptr<Aestra::Audio::TrackManager>& trackManager,
                                    double tempo,
                                    double playheadSeconds,
                                    int indentSpaces = 2,
                                    const UIState* uiState = nullptr);

    // Write file using a temp file + replace to reduce corruption risk.
    static bool writeAtomically(const std::string& path, const std::string& contents);

    static LoadResult load(const std::string& path,
                           const std::shared_ptr<Aestra::Audio::TrackManager>& trackManager);

    // Load with explicit asset base path (used for rollback loads where the
    // file is in a temp directory but assets are relative to the original project).
    static LoadResult load(const std::string& path,
                           const std::shared_ptr<Aestra::Audio::TrackManager>& trackManager,
                           const std::string& assetBasePath);

    // Recovery files are ordered newest-first. Try them without requiring the
    // application layer to replicate serializer failure handling.
    static CandidateLoadResult
    loadFirstValid(const std::vector<std::string>& candidatePaths,
                   const std::shared_ptr<Aestra::Audio::TrackManager>& trackManager,
                   const std::string& assetBasePath);

    static std::string getHistoryDirectory(const std::string& projectPath);
    static std::vector<HistoryEntry> listHistory(const std::string& projectPath);

    /**
     * @brief Configure the .history directory caps (issue #274).
     *
     * History snapshots are pruned so the directory keeps at most @p maxEntries
     * snapshots AND stays within @p maxTotalBytes on disk, whichever is hit
     * first; the newest snapshot is always retained. Defaults are 50 entries /
     * 512 MB. Applies to subsequent saves. Thread-safe.
     */
    static void setHistoryLimits(size_t maxEntries, uintmax_t maxTotalBytes);
};
