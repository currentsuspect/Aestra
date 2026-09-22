// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-A1 (FD-20): automation mutations go through the command history.
//
// FD-20's ordering is forced, not preferred — "building multi-curve editing on
// top of direct model mutation multiplies the surface that has no undo, and
// retrofitting commands afterwards is strictly more work than starting with
// them". Before this, automation was the one authoring surface outside undo:
// no automation command existed among 59 command headers.
//
// Also pins V8-A9, which FD-20 places in scope of the same entry: a Volume
// curve's neutral value is unity, not silence. That defect and this one are the
// same bug seen twice — the model default was wrong and exactly one UI path
// hid it, so adding a second construction path (a command) without fixing the
// model would have turned a latent bug into a live one.

#include "Commands/EditAutomationCurvesCommand.h"
#include "Core/AutomationCurve.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

void checkNear(float actual, float expected, float tolerance, const std::string& message) {
    if (!(actual >= expected - tolerance && actual <= expected + tolerance)) {
        std::cout << "[FAIL] " << message << " (expected " << expected << ", got " << actual << ")\n";
        ++g_failures;
    }
}

AutomationCurve makeVolumeCurve() {
    AutomationCurve curve("Volume", AutomationTarget::Volume);
    curve.mixerChannelId = 1;
    return curve;
}

// ---------------------------------------------------------------- V8-A9 ----

// The defect: defaultValue was 0.0f at construction, so a Volume curve that
// reached the model by any route other than the UI's create-on-first-click path
// was neutral-at-silence. Only reachable when a curve has no points —
// getValueAtBeat returns a point value in every other branch.
void testVolumeCurveNeutralIsUnity() {
    const AutomationCurve volume("Volume", AutomationTarget::Volume);
    checkNear(volume.getDefaultValue(), 1.0f, 0.0f, "a Volume curve's neutral is unity, not silence");

    const AutomationCurve pan("Pan", AutomationTarget::Pan);
    checkNear(pan.getDefaultValue(), 0.0f, 0.0f, "a Pan curve's neutral is centre");

    const AutomationCurve custom("Param", AutomationTarget::Custom);
    checkNear(custom.getDefaultValue(), 0.0f, 0.0f, "a Custom curve's neutral is 0, the only safe assumption");

    // The audible consequence, which is the whole point of the fix.
    check(volume.getPoints().empty(), "the curve under test really has no points");
    checkNear(volume.getValueAtBeat(0.0), 1.0f, 0.0f, "an empty Volume curve evaluates to unity at beat 0");
    checkNear(volume.getValueAtBeat(64.0), 1.0f, 0.0f, "an empty Volume curve stays unity later in the timeline");
}

// ------------------------------------------------------- the no-op guard ----

void testCurveEqualityIgnoresUiStateOnly() {
    AutomationCurve a = makeVolumeCurve();
    a.points.push_back(AutomationPoint{0, 0.5f, 4.0, 0.0f, false});
    AutomationCurve b = a;

    check(automationCurvesEqual({a}, {b}), "identical curves compare equal");

    // Selection is UI state: a click that only selects must not become an undo
    // step. Same for `sample`, a documented stale tempo cache.
    b.points[0].selected = true;
    b.points[0].sample = 99999;
    check(automationCurvesEqual({a}, {b}), "selection and the stale sample cache are not undoable content");

    // Everything that IS content must register.
    AutomationCurve movedBeat = a;
    movedBeat.points[0].beat = 8.0;
    check(!automationCurvesEqual({a}, {movedBeat}), "a moved beat is a real edit");

    AutomationCurve movedValue = a;
    movedValue.points[0].value = 0.25f;
    check(!automationCurvesEqual({a}, {movedValue}), "a changed value is a real edit");

    AutomationCurve extraPoint = a;
    extraPoint.points.push_back(AutomationPoint{0, 0.75f, 12.0, 0.0f, false});
    check(!automationCurvesEqual({a}, {extraPoint}), "an added point is a real edit");

    check(!automationCurvesEqual({a}, {}), "losing the whole curve is a real edit");
}

// ------------------------------------------------------------- the command --

struct Fixture {
    TrackManager manager;
    PlaylistLaneID laneId;

    Fixture() { laneId = manager.getPlaylistModel().createLane("Automation Lane"); }

    std::vector<AutomationCurve>& curves() { return manager.getPlaylistModel().getLane(laneId)->automationCurves; }
};

void testUndoRedoRoundTrip() {
    Fixture fx;

    AutomationCurve curve = makeVolumeCurve();
    curve.points.push_back(AutomationPoint{0, 0.5f, 4.0, 0.0f, false});

    std::vector<AutomationCurve> before = fx.curves();
    fx.curves().push_back(curve);
    std::vector<AutomationCurve> after = fx.curves();

    check(before.empty(), "the lane starts with no automation curves");
    check(after.size() == 1, "the edit added one curve");

    auto command =
        std::make_shared<EditAutomationCurvesCommand>(fx.manager, fx.laneId, before, after, "Add Automation Point");
    fx.manager.getCommandHistory().pushAndExecute(command);

    check(fx.curves().size() == 1, "pushing an already-executed command leaves the edit in place");

    // THE ACCEPTANCE CRITERION: the first click on an empty lane creates a curve
    // AND adds a point, so one undo must leave no curve behind. Snapshotting a
    // single curve's points could not express this.
    fx.manager.getCommandHistory().undo();
    check(fx.curves().empty(), "undo of the first edit removes the created curve entirely");

    fx.manager.getCommandHistory().redo();
    check(fx.curves().size() == 1, "redo restores the curve");
    if (fx.curves().size() == 1) {
        check(fx.curves()[0].points.size() == 1, "redo restores the point too");
        checkNear(fx.curves()[0].points[0].value, 0.5f, 1e-6f, "redo restores the point's value");
        checkNear(fx.curves()[0].getDefaultValue(), 1.0f, 0.0f, "the restored Volume curve is still unity-neutral");
    }
}

void testPointMoveUndoRestoresPosition() {
    Fixture fx;
    AutomationCurve curve = makeVolumeCurve();
    curve.points.push_back(AutomationPoint{0, 0.5f, 4.0, 0.0f, false});
    fx.curves().push_back(curve);

    std::vector<AutomationCurve> before = fx.curves();
    fx.curves()[0].points[0].beat = 12.0;
    fx.curves()[0].points[0].value = 0.9f;
    std::vector<AutomationCurve> after = fx.curves();

    fx.manager.getCommandHistory().pushAndExecute(
        std::make_shared<EditAutomationCurvesCommand>(fx.manager, fx.laneId, before, after, "Move Automation Point"));

    fx.manager.getCommandHistory().undo();
    check(fx.curves().size() == 1, "undo keeps the curve that existed before the move");
    if (fx.curves().size() == 1 && fx.curves()[0].points.size() == 1) {
        checkNear(static_cast<float>(fx.curves()[0].points[0].beat), 4.0f, 1e-6f, "undo restores the point's beat");
        checkNear(fx.curves()[0].points[0].value, 0.5f, 1e-6f, "undo restores the point's value");
    }
}

// An undo that does not reach the engine is inaudible, which would make this
// feature look implemented while changing nothing you can hear. The command
// requests a rebuild in apply(); pushAndExecute does NOT call execute() because
// the command is constructed already-executed, so this specifically pins the
// undo/redo path rather than the initial edit.
void testUndoRequestsGraphRebuild() {
    Fixture fx;
    AutomationCurve curve = makeVolumeCurve();
    curve.points.push_back(AutomationPoint{0, 0.5f, 4.0, 0.0f, false});

    std::vector<AutomationCurve> before = fx.curves();
    fx.curves().push_back(curve);
    std::vector<AutomationCurve> after = fx.curves();

    fx.manager.getCommandHistory().pushAndExecute(
        std::make_shared<EditAutomationCurvesCommand>(fx.manager, fx.laneId, before, after, "Add Automation Point"));

    fx.manager.consumePendingGraphRebuild();
    check(!fx.manager.hasPendingGraphRebuild(), "rebuild flag is clear before the undo");

    fx.manager.getCommandHistory().undo();
    check(fx.manager.hasPendingGraphRebuild(), "undo requests an audio graph rebuild, so the undo is audible");

    fx.manager.consumePendingGraphRebuild();
    fx.manager.getCommandHistory().redo();
    check(fx.manager.hasPendingGraphRebuild(), "redo requests an audio graph rebuild too");
}

} // namespace

int main() {
    testVolumeCurveNeutralIsUnity();
    testCurveEqualityIgnoresUiStateOnly();
    testUndoRedoRoundTrip();
    testPointMoveUndoRestoresPosition();
    testUndoRequestsGraphRebuild();

    if (g_failures == 0) {
        std::cout << "Automation command history tests passed\n";
        return 0;
    }
    std::cout << g_failures << " automation command history test(s) failed\n";
    return 1;
}
