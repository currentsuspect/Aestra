// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TrackManager.h"
#include "AestraJSON.h"
#include <string>
#include <memory>
#include <optional>
#include <vector>

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

    struct LoadResult {
        bool ok{false};
        double tempo{120.0};
        double playhead{0.0};
        std::string errorMessage;  // Populated on failure
        std::vector<std::string> missingAssets;  // Audio files that couldn't be found

        std::optional<UIState> ui;
    };

    struct SerializeResult {
        bool ok{false};
        std::string contents;
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
};
