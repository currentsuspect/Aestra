// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V82-AR1 (FD-52): right-click a pattern in the Arsenal's strip for Rename, Duplicate and Delete. Until now renaming
// or deleting a pattern meant going to the browser. Each verb is one undo step (PatternMenuCommands.h).
//
// The rules (what may be deleted, what to select afterwards) are pure and static so a test can pin them; the rest is
// the menu and the inline rename box, which sit on the panel the way UnitNameLabel's do.

#include "ArsenalSceneStrip.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace AestraUI {
struct NUIMouseEvent;
struct NUIKeyEvent;
class NUIRenderer;
class NUIComponent;
class NUIContextMenu;
class NUITextInput;
} // namespace AestraUI

namespace Aestra {
namespace Audio {
class TrackManager;
}

class ArsenalSceneMenu {
public:
    struct Request {
        Audio::TrackManager* tracks{nullptr};
        AestraUI::NUIComponent* owner{nullptr};    ///< the panel: the menu hangs off its root, the rename box under it
        uint64_t id{0};                            ///< the pattern that was right-clicked
        uint64_t activeId{0};                      ///< the pattern the Arsenal is showing
        AestraUI::NUIPoint at{};                   ///< where the menu opens
        AestraUI::NUIRect chip{};                  ///< where that pattern's chip is (the rename box covers it)
        std::vector<ArsenalSceneStrip::Scene> scenes; ///< what the strip shows now, in order
        /// After a verb changed the pattern list: select this pattern (0 = leave the selection alone) and refresh.
        std::function<void(uint64_t select)> changed;
    };

    ~ArsenalSceneMenu();

    void show(const Request& request);
    /// Drop an open menu or rename without applying it (the panel is going away or switching away).
    void cancel();
    bool renaming() const { return static_cast<bool>(m_input); }
    /// The panel paints its children before the strip, which would cover the rename box; the panel calls this after.
    void draw(AestraUI::NUIRenderer& renderer);
    /// Pointer events while renaming: inside the box they edit it, a press anywhere else commits (and goes on to
    /// whatever was clicked). Returns true when the box took the event.
    bool mouse(const AestraUI::NUIMouseEvent& event);
    /// While renaming every key press belongs to the box (Space must not start the transport). True when it took it.
    bool key(const AestraUI::NUIKeyEvent& event);

    /// A pattern placed on the timeline stays (as in the browser), and the last pattern stays: the Arsenal always
    /// has one to play. @p why (optional) gets the reason, worded for the menu item.
    static bool canDelete(bool usedOnTimeline, size_t sceneCount, std::string* why = nullptr) {
        if (usedOnTimeline) {
            if (why) *why = "Delete (it plays in the timeline)";
            return false;
        }
        if (sceneCount <= 1) {
            if (why) *why = "Delete (the only pattern)";
            return false;
        }
        return true;
    }
    /// What to select after @p deleted goes: the next scene, or the one before when it was last; 0 when none is left.
    static uint64_t selectAfterDelete(const std::vector<ArsenalSceneStrip::Scene>& scenes, uint64_t deleted) {
        const auto it = std::find_if(scenes.begin(), scenes.end(), [&](const auto& s) { return s.id == deleted; });
        if (it == scenes.end()) return 0;
        if (std::next(it) != scenes.end()) return std::next(it)->id;
        return it != scenes.begin() ? std::prev(it)->id : 0;
    }

private:
    void beginRename(const Request& request);
    void finishRename(bool commit);

    std::shared_ptr<AestraUI::NUIContextMenu> m_menu;
    std::shared_ptr<AestraUI::NUITextInput> m_input;
    std::shared_ptr<AestraUI::NUITextInput> m_retired; ///< a finished box lives until the next one: it may still be inside its own key handler
    AestraUI::NUIComponent* m_inputOwner{nullptr};
    Audio::TrackManager* m_tracks{nullptr};
    uint64_t m_renameId{0};
    std::function<void(uint64_t)> m_changed;
};

} // namespace Aestra
