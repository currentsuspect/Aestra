// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "TrackManager.h"
#include <memory>

namespace Aestra {
namespace Audio {

/**
 * @brief Visual git-log-style command history viewer with clickable undo/redo
 *
 * Shows a vertical list of commands:
 *   - Redo stack entries at top (greyed out, clickable to redo)
 *   - Current position marker
 *   - Undo stack entries below (most recent at top, clickable to undo)
 */
class AestraHistoryPanel : public WindowPanel {
public:
    AestraHistoryPanel(std::shared_ptr<TrackManager> trackManager);
    ~AestraHistoryPanel() override = default;

    void onRender(AestraUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    void onResize(int width, int height) override;

    /** @brief Rebuild the displayed history list from CommandHistory state. */
    void refreshHistory();

    /** @brief Set callback for when user clicks to undo/redo (so UI panels refresh). */
    void setOnHistoryChanged(std::function<void()> cb) { m_onHistoryChanged = std::move(cb); }

    // --- Introspection (used by tests and tooling) ---------------------------
    /** @brief Number of displayed history entries (redo + undo). */
    int getEntryCount() const { return static_cast<int>(m_entries.size()); }
    /** @brief Display name of an entry (top row first). */
    const std::string& getEntryName(int index) const { return m_entries[static_cast<size_t>(index)].name; }
    /** @brief True if the entry sits on the redo stack (above HEAD). */
    bool isEntryRedo(int index) const { return m_entries[static_cast<size_t>(index)].isRedo; }
    /** @brief Navigate to an entry exactly like a mouse click on its row. */
    void activateEntry(int index);

private:
    static constexpr float ROW_HEIGHT = 26.0f;
    static constexpr float INDICATOR_SIZE = 6.0f;
    static constexpr float LEFT_PAD = 12.0f;
    static constexpr float TEXT_LEFT_PAD = 28.0f;

    std::shared_ptr<TrackManager> m_trackManager;
    std::function<void()> m_onHistoryChanged;

    struct HistoryEntry {
        std::string name;
        bool isRedo;        // true = redo stack entry, false = undo stack entry
        int stackIndex;     // index within the respective stack
    };

    std::vector<HistoryEntry> m_entries;
    float m_scrollY = 0.0f;
    float m_contentHeight = 0.0f;
    int m_hoveredEntry = -1;

    void rebuildEntries();
    void drawEntry(AestraUI::NUIRenderer& renderer, const HistoryEntry& entry, int globalIndex);
};

} // namespace Audio
} // namespace Aestra
