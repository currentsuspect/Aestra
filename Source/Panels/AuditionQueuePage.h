// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "AuditionEngine.h"
#include "NUIContextMenu.h"

#include <functional>
#include <memory>
#include <string>

namespace Aestra {

/**
 * @brief The queue, as a page of the Audition hub (FD-26, V81-A): what will play, in order. It has no player of its
 * own: the bar along the bottom of the hub is the one player, here as everywhere. Files dropped on it (from the Files
 * column) join the queue; rows play on a click, reorder by dragging, and go with a click on their cross.
 */
class AuditionQueuePage : public AestraUI::NUIComponent, public AestraUI::IDropTarget {
public:
    explicit AuditionQueuePage(std::shared_ptr<Audio::AuditionEngine> engine);
    ~AuditionQueuePage() override;

    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;

    /// A row was chosen to play: the hub forgets the item it chose itself and follows the queue.
    void setOnPlayIndex(std::function<void(size_t)> cb) { m_onPlayIndex = std::move(cb); }

    AestraUI::DropFeedback onDragEnter(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) override;
    AestraUI::DropFeedback onDragOver(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) override;
    void onDragLeave() override { m_dropHover = false; setDirty(true); }
    AestraUI::DropResult onDrop(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) override;
    AestraUI::NUIRect getDropBounds() const override { return getBounds(); }

    // For tests and hit-testing.
    bool getRowCenter(size_t row, AestraUI::NUIPoint& out) const;

private:
    static constexpr float kRowH = 52.0f;
    struct Layout { AestraUI::NUIRect header, clear, list; };
    Layout layout() const;
    int rowAt(const AestraUI::NUIPoint& p, const Layout& l) const;
    AestraUI::NUIRect removeButton(const AestraUI::NUIRect& row) const;
    void openMenu(size_t index, const AestraUI::NUIPoint& at);
    void closeMenu();

    std::shared_ptr<Audio::AuditionEngine> m_engine;
    std::function<void(size_t)> m_onPlayIndex;
    float m_scroll{0.0f};
    int m_hover{-1}, m_pressed{-1}, m_insert{-1};
    bool m_reordering{false};
    AestraUI::NUIPoint m_pressPos;
    bool m_hoverClear{false};
    bool m_dropHover{false};
    bool m_registered{false};
    std::shared_ptr<AestraUI::NUIContextMenu> m_menu;
};

} // namespace Aestra
