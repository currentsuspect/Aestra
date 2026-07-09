// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TrackManager.h"
#include "../AestraCore/include/AestraJSON.h"
#include <string>
#include <memory>
#include <optional>
#include <vector>
#include <chrono>

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

    struct LoadResult {
        bool ok{false};
        double tempo{120.0};
        double playhead{0.0};
        std::string errorMessage;
        std::vector<std::string> missingAssets;
        LoadIntegrity integrity{LoadIntegrity::Unchecked};

        std::optional<UIState> ui;
        std::unique_ptr<::Aestra::ProjectLoadReport> report;
    };

    struct SerializeResult {
        bool ok{false};
        std::string contents;
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

    static std::string getHistoryDirectory(const std::string& projectPath);
    static std::vector<HistoryEntry> listHistory(const std::string& projectPath);
};
