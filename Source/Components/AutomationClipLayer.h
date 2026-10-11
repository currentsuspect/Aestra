// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/EditAutomationCurvesCommand.h"
#include "Core/AutomationRegions.h"
#include "Models/PlaylistModel.h"
#include "NUIComponent.h"
#include "NUIContextMenu.h"
#include "NUITypes.h"

#include <functional>
#include <memory>
#include <vector>

namespace AestraUI {
class NUIRenderer;
}

namespace Aestra {
namespace Audio {

class TrackManager;

/**
 * @brief The automation clips of one lane: drawn as headed bands, dragged by their grip to move,
 *        Ctrl-dragged to copy, right-clicked on the header for Duplicate / Split Here / Delete (V8-A7).
 *
 * Only the grip (a small tab at the clip's start) and a right-click on the header are the layer's.
 * Every other press, including one on the rest of the header, is point editing's, and a point
 * under the pointer always wins, so adding or grabbing points near the top of the lane works as before.
 *
 * The operations are AutomationRegions.h; this is only the gesture on top of them. Every edit is
 * one undo step: the lane's curves are snapshotted at the press, edited live while dragging, and
 * pushed as an EditAutomationCurvesCommand on release. A curve that has points but no stored
 * clips shows one implicit clip over all its points.
 */
class AutomationClipLayer {
public:
    struct Geometry {
        AestraUI::NUIRect bounds; ///< the lane row
        float gridStartX = 0.0f;
        float pixelsPerBeat = 40.0f;
        float scrollOffset = 0.0f;
    };
    struct Hooks {
        std::function<double(double)> snapBeat;      ///< timeline snap
        std::function<void(bool)> captureMouse;      ///< keep the drag when the pointer leaves the lane
        std::function<void()> changed;               ///< repaint + invalidate
    };

    explicit AutomationClipLayer(AestraUI::NUIComponent& owner) : m_owner(owner) {}
    ~AutomationClipLayer();

    static constexpr float kHeaderHeight = 12.0f;

    void render(AestraUI::NUIRenderer& renderer, const PlaylistLane& lane, int editedCurve, const Geometry& g) const;

    /// True when the layer took the event. @p editedCurve is set to the curve the user touched, else left alone.
    bool onMouse(const AestraUI::NUIMouseEvent& event, TrackManager& tm, PlaylistLaneID laneId, const Geometry& g,
                 const Hooks& hooks, int& editedCurve);

    bool isDragging() const { return m_dragging; }

private:
    struct Hit {
        int curve = -1;
        uint64_t region = 0;
    };
    Hit headerAt(const PlaylistLane& lane, const AestraUI::NUIPoint& p, const Geometry& g, bool gripOnly) const;
    void commit(TrackManager& tm, PlaylistLaneID laneId, std::vector<AutomationCurve> before, const char* name,
                const Hooks& hooks);
    void showMenu(TrackManager& tm, PlaylistLaneID laneId, Hit hit, double beat, const AestraUI::NUIPoint& at,
                  const Hooks& hooks);
    void closeMenu();

    AestraUI::NUIComponent& m_owner;
    int m_selectedCurve = -1;
    uint64_t m_selectedRegion = 0;
    bool m_dragging = false;
    bool m_copy = false;
    double m_grabBeat = 0.0;
    double m_origStart = 0.0;
    Hit m_dragHit;
    std::vector<AutomationCurve> m_before;
    std::shared_ptr<AestraUI::NUIContextMenu> m_menu;
};

} // namespace Audio
} // namespace Aestra
