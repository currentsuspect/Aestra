// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "../Core/TakeManager.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Workspace panel for project Takes — named, versioned project states.
 *
 * Answers "which version of the work do I want?" (the History panel answers
 * "what actions did I perform?"). Lists the takes manifest with lineage
 * indentation, lets the user create, open, rename, duplicate and branch takes,
 * and exposes recent automatic save snapshots as a recovery section.
 *
 * The panel owns no project logic: data arrives through providers and every
 * action is delegated through a callback so the app layer keeps its
 * save-before-switch safety rules in one place. Callbacks return false on
 * failure and the panel surfaces a status line.
 */
class TakesPanel : public WindowPanel {
public:
    /** @brief A recovery snapshot row (from the automatic save history). */
    struct SnapshotEntry {
        std::string path;   ///< Absolute snapshot file path.
        std::string label;  ///< Human-readable label (timestamp).
    };

    using TakesProvider = std::function<TakeManager::Manifest()>;
    using SnapshotsProvider = std::function<std::vector<SnapshotEntry>()>;

    TakesPanel();
    ~TakesPanel() override = default;

    void onRender(AestraUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    /** @brief Focused key delivery (text input arrives here while renaming). */
    bool onKeyEvent(const AestraUI::NUIKeyEvent& event) override;

    /** @brief Handle keys while an inline rename is active. Returns true if consumed. */
    bool handleKeyEvent(const AestraUI::NUIKeyEvent& event);

    /** @brief Bind the manifest source (typically TakeManager::loadManifest). */
    void setTakesProvider(TakesProvider provider) { m_takesProvider = std::move(provider); }
    /** @brief Bind the recovery-snapshot source (automatic save history). */
    void setSnapshotsProvider(SnapshotsProvider provider) { m_snapshotsProvider = std::move(provider); }

    /** @brief Capture the current project state as a new take. */
    void setOnCreateTake(std::function<bool()> cb) { m_onCreateTake = std::move(cb); }
    /** @brief Save the current state into the active take's snapshot. */
    void setOnSaveActiveTake(std::function<bool()> cb) { m_onSaveActiveTake = std::move(cb); }
    /** @brief Open (restore) a take after safely saving the current one. */
    void setOnOpenTake(std::function<bool(const std::string& takeId)> cb) { m_onOpenTake = std::move(cb); }
    /** @brief Rename a take. */
    void setOnRenameTake(std::function<bool(const std::string& takeId, const std::string& name)> cb) {
        m_onRenameTake = std::move(cb);
    }
    /** @brief Duplicate a take without activating it. */
    void setOnDuplicateTake(std::function<bool(const std::string& takeId)> cb) { m_onDuplicateTake = std::move(cb); }
    /** @brief Delete a non-active take. */
    void setOnDeleteTake(std::function<bool(const std::string& takeId)> cb) { m_onDeleteTake = std::move(cb); }
    /** @brief Branch: duplicate a take and continue working on the branch. */
    void setOnBranchTake(std::function<bool(const std::string& takeId)> cb) { m_onBranchTake = std::move(cb); }
    /** @brief Restore a recovery snapshot after safely saving the current take. */
    void setOnRestoreSnapshot(std::function<bool(const std::string& path)> cb) {
        m_onRestoreSnapshot = std::move(cb);
    }

    /** @brief Rebuild rows from the providers. Call after any take operation. */
    void refreshTakes();

    // --- Introspection + programmatic actions -------------------------------
    // The mouse handlers route through these; tests drive them directly.

    /** @brief Number of take rows currently displayed. */
    int getTakeCount() const { return static_cast<int>(m_takeRows.size()); }
    /** @brief Take entry for a displayed row (row order = manifest order). */
    const TakeManager::TakeEntry& getTakeAt(int index) const { return m_takeRows[static_cast<size_t>(index)].take; }
    /** @brief Lineage depth used for row indentation. */
    int getTakeDepthAt(int index) const { return m_takeRows[static_cast<size_t>(index)].depth; }
    /** @brief Number of recovery snapshot rows currently displayed. */
    int getSnapshotCount() const { return static_cast<int>(m_snapshotRows.size()); }
    /** @brief Selected take row index, or -1. */
    int getSelectedTake() const { return m_selectedTake; }
    /** @brief Selected snapshot row index, or -1. */
    int getSelectedSnapshot() const { return m_selectedSnapshot; }
    /** @brief Status line shown at the bottom of the panel (empty when clear). */
    const std::string& getStatusText() const { return m_statusText; }
    /** @brief True while an inline rename edit is active. */
    bool isRenameActive() const { return m_renameActive; }
    /** @brief Current rename edit buffer. */
    const std::string& getRenameBuffer() const { return m_renameBuffer; }

    /** @brief Select a take row (clears any snapshot selection). -1 clears. */
    void selectTake(int index);
    /** @brief Select a snapshot row (clears any take selection). -1 clears. */
    void selectSnapshot(int index);

    /** @brief Create a new take from the current state. */
    bool requestCreateTake();
    /** @brief Save the current state into the active take. */
    bool requestSaveActiveTake();
    /** @brief Open the selected take. */
    bool requestOpenSelected();
    /** @brief Duplicate the selected take (stays on the current take). */
    bool requestDuplicateSelected();
    /** @brief Branch from the selected take and switch to the branch. */
    bool requestBranchSelected();
    /** @brief Delete the selected take (refused for the active take). */
    bool requestDeleteSelected();
    /** @brief Restore the selected recovery snapshot. */
    bool requestRestoreSelectedSnapshot();
    /** @brief Start inline rename for the selected take. */
    void beginRenameSelected();
    /** @brief Commit the inline rename buffer. */
    bool commitRename();
    /** @brief Abandon the inline rename. */
    void cancelRename();

private:
    static constexpr float ROW_HEIGHT = 26.0f;
    static constexpr float HEADER_HEIGHT = 36.0f;   // WindowPanel title bar area
    static constexpr float TOOLBAR_HEIGHT = 30.0f;
    static constexpr float ACTION_BAR_HEIGHT = 28.0f;
    static constexpr float SECTION_HEADER_HEIGHT = 22.0f;
    static constexpr float STATUS_HEIGHT = 18.0f;
    static constexpr float LEFT_PAD = 12.0f;
    static constexpr float TEXT_LEFT_PAD = 28.0f;
    static constexpr float DEPTH_INDENT = 12.0f;
    static constexpr size_t MAX_SNAPSHOT_ROWS = 4;

    struct TakeRow {
        TakeManager::TakeEntry take;
        int depth{0};
    };

    struct Layout {
        AestraUI::NUIRect toolbar;
        AestraUI::NUIRect newTakeButton;
        AestraUI::NUIRect saveActiveButton;
        AestraUI::NUIRect takesList;
        AestraUI::NUIRect actionBar;
        AestraUI::NUIRect snapshotHeader;
        AestraUI::NUIRect snapshotList;
        AestraUI::NUIRect statusLine;
        bool actionBarVisible{false};
    };

    Layout computeLayout() const;
    void rebuildRows();
    int takeRowAt(const AestraUI::NUIPoint& point, const Layout& layout) const;
    int snapshotRowAt(const AestraUI::NUIPoint& point, const Layout& layout) const;
    std::vector<AestraUI::NUIRect> actionButtonRects(const Layout& layout) const;
    std::vector<std::string> actionButtonLabels() const;
    bool runRowAction(int actionIndex);
    void setStatus(const std::string& text);
    bool invokeAction(const std::function<bool()>& action, const char* failureMessage);
    static std::string relativeTimeLabel(uint64_t epochMs);

    TakesProvider m_takesProvider;
    SnapshotsProvider m_snapshotsProvider;

    std::function<bool()> m_onCreateTake;
    std::function<bool()> m_onSaveActiveTake;
    std::function<bool(const std::string&)> m_onOpenTake;
    std::function<bool(const std::string&, const std::string&)> m_onRenameTake;
    std::function<bool(const std::string&)> m_onDuplicateTake;
    std::function<bool(const std::string&)> m_onDeleteTake;
    std::function<bool(const std::string&)> m_onBranchTake;
    std::function<bool(const std::string&)> m_onRestoreSnapshot;

    std::vector<TakeRow> m_takeRows;
    std::vector<SnapshotEntry> m_snapshotRows;
    std::string m_manifestError;

    int m_selectedTake{-1};
    int m_selectedSnapshot{-1};
    int m_hoveredTake{-1};
    int m_hoveredSnapshot{-1};
    int m_hoveredAction{-1};
    float m_scrollY{0.0f};
    std::string m_statusText;

    bool m_renameActive{false};
    std::string m_renameBuffer;
    std::string m_renameTakeId;
};

} // namespace Audio
} // namespace Aestra
