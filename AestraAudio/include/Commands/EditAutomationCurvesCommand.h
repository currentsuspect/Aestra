// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Core/AutomationCurve.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Undoable whole-vector replacement of a lane's automation curves
 *
 * V8-A1. FD-20 puts this before multi-curve access and the target picker:
 * "Automation mutations go through the command history first. Building
 * multi-curve editing on top of direct model mutation multiplies the surface
 * that has no undo, and retrofitting commands afterwards is strictly more work
 * than starting with them."
 *
 * One command per editing gesture. The editor mutates the model directly,
 * snapshots the lane's curves before and after, and pushes this as already
 * executed — so a point drag coalesces into a single undo step, matching
 * EditPatternNotesCommand and TrimClipCommand.
 *
 * WHY THE WHOLE CURVE VECTOR, not one curve's points: the first click on an
 * empty lane both creates a curve and adds a point to it. Snapshotting only a
 * curve's points could not undo the creation, so a single Ctrl+Z would leave a
 * curve behind that the user never deliberately made. Taking the vector also
 * means the command layer is already multi-curve-correct for V8-A2, without
 * doing A2's surface work.
 */
class EditAutomationCurvesCommand final : public ICommand {
public:
    EditAutomationCurvesCommand(TrackManager& manager, PlaylistLaneID laneId, std::vector<AutomationCurve> curvesBefore,
                                std::vector<AutomationCurve> curvesAfter, std::string name = "Edit Automation")
        : m_manager(manager), m_laneId(laneId), m_before(std::move(curvesBefore)), m_after(std::move(curvesAfter)),
          m_name(std::move(name)), m_executed(true) {}

    void execute() override {
        if (m_executed)
            return;
        apply(m_after);
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;
        apply(m_before);
        m_executed = false;
    }

    void redo() override {
        if (m_executed)
            return;
        apply(m_after);
        m_executed = true;
    }

    std::string getName() const override { return m_name; }
    bool changesProjectState() const override { return true; }

    size_t getSizeInBytes() const override {
        // Counted honestly for CommandHistory's LRU budget: the curves carry
        // their own point vectors and names, so sizeof() alone under-reports.
        return sizeof(*this) + m_name.capacity() + curvesBytes(m_before) + curvesBytes(m_after);
    }

private:
    static size_t curvesBytes(const std::vector<AutomationCurve>& curves) {
        size_t bytes = curves.capacity() * sizeof(AutomationCurve);
        for (const auto& curve : curves) {
            bytes += curve.points.capacity() * sizeof(AutomationPoint);
            bytes += curve.name.capacity();
        }
        return bytes;
    }

    void apply(const std::vector<AutomationCurve>& curves) {
        auto* lane = m_manager.getPlaylistModel().getLane(m_laneId);
        if (!lane)
            return;
        lane->automationCurves = curves;

        // Automation reaches the engine only through the runtime snapshot, so
        // an undo that does not request a rebuild stays inaudible until some
        // later edit happens to trigger one.
        m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
        m_manager.markModified();
    }

    TrackManager& m_manager;
    PlaylistLaneID m_laneId;
    std::vector<AutomationCurve> m_before;
    std::vector<AutomationCurve> m_after;
    std::string m_name;
    bool m_executed;
};

} // namespace Audio
} // namespace Aestra
