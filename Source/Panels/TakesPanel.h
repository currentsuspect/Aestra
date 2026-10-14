// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "../Core/TakeManager.h"
#include "../Core/VersionBounce.h"
#include "BringOverCard.h"
#include "VersionsTimeline.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
    /** @brief Save the current state to a new branch (a branch of its own, starting at the open version). */
    void setOnSaveToNewBranch(std::function<bool()> cb) { m_onSaveToNewBranch = std::move(cb); }

    /// One step of the open version, newest first: what the dock lists, and what a click goes back to.
    struct StepEntry {
        std::string label;
        bool undone{false};   ///< Sits on the redo side: the version has been taken back past it.
        int stackIndex{0};    ///< Index in the command history's undo or redo stack.
    };
    using StepsProvider = std::function<std::vector<StepEntry>()>;
    /// The open version's live steps (the command history), for the dock.
    void setStepsProvider(StepsProvider provider) { m_stepsProvider = std::move(provider); }
    /// A step in the dock was clicked: go back (or forward) to it.
    void setOnStepActivated(std::function<void(const StepEntry&)> cb) { m_onStepActivated = std::move(cb); }
    /// How many steps have been made since the open version was saved (the timeline's dashed tail).
    void setUnsavedProvider(std::function<int()> provider) { m_unsavedProvider = std::move(provider); }
    /// A version's bounce, when it has one: its file and what the card shows. Read once per refresh, not per frame.
    struct BounceCard {
        std::string path;
        VersionBounceInfo info;
    };
    void setBounceProvider(std::function<std::optional<BounceCard>(const TakeManager::TakeEntry&)> provider) { m_bounceProvider = std::move(provider); }
    /// Play the bounce, or stop it when it is the one playing; returns whether it is playing afterwards.
    void setOnToggleBounce(std::function<bool(const std::string&)> cb) { m_onToggleBounce = std::move(cb); }
    /// Render the open version's mix to its bounce. Slow (a few seconds per minute of music), so the panel says so first.
    void setOnBounce(std::function<bool(const std::string&)> cb) { m_onBounce = std::move(cb); }
    bool requestBounce();
    void setBouncePlayingProvider(std::function<bool(const std::string&)> provider) { m_bouncePlaying = std::move(provider); }
    /// The bounce card shown in the dock for the version in view, when it has one.
    const BounceCard* getDockBounce() const;
    /// Where the card's play button is, for the version in view (false when it has no bounce).
    bool getBouncePlayCenter(AestraUI::NUIPoint& out) const;

    /// Bring over: the card for an older version (what it has that could come across), or nothing when it cannot be read.
    void setBringOverProvider(std::function<std::optional<BringOverCard>(const std::string& takeId)> provider) { m_bringProvider = std::move(provider); }
    /// The chosen rows were confirmed: bring them into the open version (one undo step).
    void setOnBringOver(std::function<bool(const std::string& takeId, const std::vector<BringOverCard::Row>& chosen)> cb) { m_onBringOver = std::move(cb); }
    bool requestBringOver();       ///< open the card for the version in view
    bool confirmBringOver();       ///< bring over what is checked
    void closeBringOver() { m_bring.reset(); setDirty(true); }
    bool isBringOverOpen() const { return m_bring.has_value(); }
    BringOverCard* getBringOver() { return m_bring ? &*m_bring : nullptr; }
    /// Where a row's check box and a card button are, for tests and hit-testing (false when the card is closed).
    bool getBringOverRowCenter(int row, AestraUI::NUIPoint& out) const;
    bool getBringOverButtonCenter(const std::string& id, AestraUI::NUIPoint& out) const;

    /// The project's name, shown beside the branch chip.
    void setProjectNameProvider(std::function<std::string()> provider) { m_projectNameProvider = std::move(provider); }

    /** @brief Restore a recovery snapshot after safely saving the current take. */
    void setOnRestoreSnapshot(std::function<bool(const std::string& path)> cb) {
        m_onRestoreSnapshot = std::move(cb);
    }

    /** @brief Rebuild rows from the providers. Call after any take operation. */
    void refreshTakes();

    // --- Introspection + programmatic actions -------------------------------
    // The mouse handlers route through these; tests drive them directly.

    /** @brief Number of version rows currently held (manifest order). */
    int getTakeCount() const { return static_cast<int>(m_takeRows.size()); }
    /** @brief Version entry for a row (row order = manifest order). */
    const TakeManager::TakeEntry& getTakeAt(int index) const { return m_takeRows[static_cast<size_t>(index)].take; }
    /** @brief Lineage depth of a version. */
    int getTakeDepthAt(int index) const { return m_takeRows[static_cast<size_t>(index)].depth; }
    /** @brief Number of recovery snapshot rows currently held. */
    int getSnapshotCount() const { return static_cast<int>(m_snapshotRows.size()); }
    /** @brief Selected version row index, or -1. */
    int getSelectedTake() const { return m_selectedTake; }
    /** @brief Selected snapshot row index, or -1. */
    int getSelectedSnapshot() const { return m_selectedSnapshot; }
    /** @brief The version the dock shows: the selected one, else the open one, or -1. */
    int getDockVersion() const;
    /** @brief Status line shown at the bottom of the panel (empty when clear). */
    const std::string& getStatusText() const { return m_statusText; }
    /** @brief True while an inline rename edit is active. */
    bool isRenameActive() const { return m_renameActive; }
    /** @brief Current rename edit buffer. */
    const std::string& getRenameBuffer() const { return m_renameBuffer; }
    /** @brief Window-space centre of a version's node on the timeline; false when it is not laid out. */
    bool getTimelineNodeCenter(int row, AestraUI::NUIPoint& out) const;

    /** @brief Select a version (clears any snapshot selection). -1 clears. */
    void selectTake(int index);
    /** @brief Select a snapshot row (clears any version selection). -1 clears. */
    void selectSnapshot(int index);

    /** @brief Create a new version from the current state. */
    bool requestCreateTake();
    /** @brief Save the current state to a new branch. */
    bool requestSaveToNewBranch();
    /** @brief Save the current state into the open version. */
    bool requestSaveActiveTake();
    /** @brief Open the selected version. */
    bool requestOpenSelected();
    /** @brief Duplicate the selected version (stays on the current one). */
    bool requestDuplicateSelected();
    /** @brief Branch from the selected version and switch to the branch. */
    bool requestBranchSelected();
    /** @brief Delete the selected version (refused for the open one). */
    bool requestDeleteSelected();
    /** @brief Restore the selected recovery snapshot. */
    bool requestRestoreSelectedSnapshot();
    /** @brief Start inline rename for the selected version. */
    void beginRenameSelected();
    /** @brief Commit the inline rename buffer. */
    bool commitRename();
    /** @brief Abandon the inline rename. */
    void cancelRename();

private:
    static constexpr float BAR_HEIGHT = 52.0f;
    static constexpr float LEGEND_HEIGHT = 34.0f;
    static constexpr float STATUS_HEIGHT = 18.0f;
    static constexpr float STEP_ROW_HEIGHT = 30.0f;
    static constexpr float SNAPSHOT_ROW_HEIGHT = 24.0f;
    static constexpr size_t MAX_SNAPSHOT_ROWS = 4;

    struct TakeRow {
        TakeManager::TakeEntry take;
        int depth{0};
    };

    struct DockButton {
        std::string id;
        std::string label;
        AestraUI::NUIRect rect;
        bool primary{false};
        bool danger{false};
    };

    struct Layout {
        AestraUI::NUIRect bar;
        AestraUI::NUIRect branchButton;
        AestraUI::NUIRect saveButton;
        AestraUI::NUIRect timeline;
        AestraUI::NUIRect legend;
        AestraUI::NUIRect dock;
        AestraUI::NUIRect bounceCard;     ///< the version's bounce, at the top of the dock (empty when it has none)
        AestraUI::NUIRect bouncePlay;
        AestraUI::NUIRect stepsList;      ///< the open version's steps, inside the dock
        AestraUI::NUIRect snapshotHeader;
        AestraUI::NUIRect snapshotList;
        AestraUI::NUIRect statusLine;
        std::vector<DockButton> buttons;
        VersionsTimeline::Result nodes;
    };

    Layout computeLayout() const;
    void rebuildRows();
    int snapshotRowAt(const AestraUI::NUIPoint& point, const Layout& layout) const;
    int stepRowAt(const AestraUI::NUIPoint& point, const Layout& layout) const;
    bool runDockAction(const std::string& id);
    void setStatus(const std::string& text);
    bool invokeAction(const std::function<bool()>& action, const char* failureMessage);
    static AestraUI::NUIColor laneColor(int lane);

    TakesProvider m_takesProvider;
    SnapshotsProvider m_snapshotsProvider;
    StepsProvider m_stepsProvider;
    std::function<std::optional<BounceCard>(const TakeManager::TakeEntry&)> m_bounceProvider;
    std::function<bool(const std::string&)> m_onToggleBounce;
    std::function<bool(const std::string&)> m_onBounce;
    std::function<std::optional<BringOverCard>(const std::string&)> m_bringProvider;
    std::function<bool(const std::string&, const std::vector<BringOverCard::Row>&)> m_onBringOver;
    std::optional<BringOverCard> m_bring;
    std::string m_bringTakeId;
    AestraUI::NUIRect bringArea() const;
    int m_bouncePending{0};  ///< frames to draw the "bouncing" status before the render blocks the UI
    std::function<bool(const std::string&)> m_bouncePlaying;
    std::unordered_map<std::string, BounceCard> m_bounces;  ///< by version id, rebuilt with the rows
    std::function<int()> m_unsavedProvider;
    std::function<std::string()> m_projectNameProvider;

    std::function<bool()> m_onCreateTake;
    std::function<bool()> m_onSaveToNewBranch;
    std::function<bool()> m_onSaveActiveTake;
    std::function<bool(const std::string&)> m_onOpenTake;
    std::function<bool(const std::string&, const std::string&)> m_onRenameTake;
    std::function<bool(const std::string&)> m_onDuplicateTake;
    std::function<bool(const std::string&)> m_onDeleteTake;
    std::function<bool(const std::string&)> m_onBranchTake;
    std::function<bool(const std::string&)> m_onRestoreSnapshot;
    std::function<void(const StepEntry&)> m_onStepActivated;

    std::vector<TakeRow> m_takeRows;
    std::vector<SnapshotEntry> m_snapshotRows;
    std::vector<StepEntry> m_steps;
    int m_unsaved{0};
    float m_timelineScroll{0.0f};
    bool m_timelineScrollPlaced{false};  ///< the first layout scrolls so the open version is in view
    unsigned m_stepTick{0};
    void updateUnsaved();
    std::string m_manifestError;
    bool m_multipleBranches{false};

    int m_selectedTake{-1};
    int m_selectedSnapshot{-1};
    int m_hoveredStep{-1};
    int m_hoveredSnapshot{-1};
    std::string m_hoveredButton;
    float m_stepScroll{0.0f};
    std::string m_statusText;

    bool m_renameActive{false};
    std::string m_renameBuffer;
    std::string m_renameTakeId;
};

} // namespace Audio
} // namespace Aestra
