// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A clip-drag release that changed nothing must do nothing.
//
// Owner report (2026-09-23, live): grabbing a pattern clip during playback and
// letting go, without moving it, was "late" and "plays the midi as if I did a
// cut, when I did nothing". The session log showed every release rebuilding all
// 50 track rows and rescheduling all 11 MIDI instances. The reschedule clears the
// scheduler, which drops sounding notes' gates and queued note-offs, and re-queues
// from the UI's lagging playhead.
//
// A release now takes that path only if the clip actually landed somewhere new.
// "Nothing" includes dragging away and back: the model moves live during the
// drag, so the end state is what counts, not whether the pointer moved.
//
// Observables: TrackManager's reschedule counter, the lane component's identity
// (refreshTracks() rebuilds every row), and the undo stack.

#include "../Support/NullRenderer.h"
#include "MixerChannel.h"
#include "NUIComponent.h"
#include "PatternManager.h"
#include "PlaylistModel.h"
#include "TrackManager.h"
#include "TrackManagerUI.h"
#include "TrackUIComponent.h"

#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

std::shared_ptr<TrackUIComponent> findLane(const AestraUI::NUIComponent& node) {
    for (const auto& child : node.getChildren()) {
        if (auto lane = std::dynamic_pointer_cast<TrackUIComponent>(child)) {
            return lane;
        }
        if (child) {
            if (auto nested = findLane(*child)) {
                return nested;
            }
        }
    }
    return nullptr;
}

/** @brief A playing timeline with one MIDI clip on one lane, under a real TrackManagerUI. */
struct Fixture {
    std::shared_ptr<TrackManager> trackManager;
    std::shared_ptr<AestraUI::NUIComponent> root;
    std::shared_ptr<TrackManagerUI> manager;
    std::shared_ptr<TrackUIComponent> lane;
    PlaylistLaneID laneId;
    ClipInstanceID clipId;

    bool build() {
        trackManager = std::make_shared<TrackManager>();
        trackManager->setCommandSink([](const AudioQueueCommand&) { return true; });
        auto& patterns = trackManager->getPatternManager();
        auto& playlist = trackManager->getPlaylistModel();

        const PatternID patternId = patterns.createMidiPattern("Drag", 4.0, MidiPayload{});
        laneId = playlist.createLane("Drag Lane");
        clipId = playlist.addClipFromPattern(laneId, patternId, 0.0, 4.0);
        if (!clipId.isValid()) {
            check(false, "fixture could not place a clip");
            return false;
        }

        manager = std::make_shared<TrackManagerUI>(trackManager);
        manager->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 600.0f));
        root = std::make_shared<AestraUI::NUIComponent>();
        root->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 600.0f));
        root->addChild(manager);
        manager->refreshTracks();
        lane = findLane(*manager);
        if (!lane) {
            check(false, "TrackManagerUI built no lane component");
            return false;
        }

        // Reschedules only happen while the timeline plays.
        trackManager->play();
        if (!trackManager->isPlaying()) {
            check(false, "the timeline did not start playing");
            return false;
        }
        return true;
    }

    AestraUI::NUIPoint grabPoint() const {
        const auto b = lane->getBounds();
        return AestraUI::NUIPoint(b.x + b.width * 0.5f, b.y + b.height * 0.5f);
    }
    double clipStart() const { return trackManager->getPlaylistModel().getClip(clipId)->startBeat; }
    size_t undoDepth() const { return trackManager->getCommandHistory().getUndoStack().size(); }
};

void expectNothingHappened(Fixture& f, uint64_t reschedulesBefore, size_t undoBefore,
                           const std::shared_ptr<TrackUIComponent>& laneBefore, const std::string& gesture) {
    check(f.trackManager->getTimelineRescheduleCount() == reschedulesBefore,
          gesture + ": playing MIDI was not rescheduled (no cut, no late re-entry)");
    check(findLane(*f.manager) == laneBefore, gesture + ": the track rows were not rebuilt");
    check(f.undoDepth() == undoBefore, gesture + ": no undo step for a move that did not happen");
    check(f.clipStart() == 0.0, gesture + ": the clip is where it started");
}

// The report itself: press on a clip and let go.
void testClickReleaseIsANoOp() {
    Fixture f;
    if (!f.build()) {
        return;
    }
    const uint64_t reschedules = f.trackManager->getTimelineRescheduleCount();
    const size_t undo = f.undoDepth();
    const auto laneBefore = findLane(*f.manager);

    f.manager->startInstantClipDrag(f.lane.get(), f.clipId, f.grabPoint());
    f.manager->finishInstantClipDrag();

    expectNothingHappened(f, reschedules, undo, laneBefore, "click-release");
}

// The drag moves the model live, so away-and-back must still count as nothing.
void testDragAwayAndBackIsANoOp() {
    Fixture f;
    if (!f.build()) {
        return;
    }
    const uint64_t reschedules = f.trackManager->getTimelineRescheduleCount();
    const size_t undo = f.undoDepth();
    const auto laneBefore = findLane(*f.manager);

    f.manager->startInstantClipDrag(f.lane.get(), f.clipId, f.grabPoint());
    // What updateInstantClipDrag does per motion event.
    f.trackManager->getPlaylistModel().moveClip(f.clipId, 8.0, f.laneId);
    f.trackManager->getPlaylistModel().moveClip(f.clipId, 0.0, f.laneId);
    f.manager->finishInstantClipDrag();

    expectNothingHappened(f, reschedules, undo, laneBefore, "drag away and back");
}

// A real move must still reschedule, or the MIDI keeps playing from the old spot.
// This also proves the observables can see a reschedule and a rebuild at all.
void testRealMoveStillReschedules() {
    Fixture f;
    if (!f.build()) {
        return;
    }
    const uint64_t reschedules = f.trackManager->getTimelineRescheduleCount();
    const size_t undo = f.undoDepth();
    const auto laneBefore = findLane(*f.manager);

    f.manager->startInstantClipDrag(f.lane.get(), f.clipId, f.grabPoint());
    f.trackManager->getPlaylistModel().moveClip(f.clipId, 8.0, f.laneId);
    f.manager->finishInstantClipDrag();

    check(f.clipStart() == 8.0, "real move: the clip landed at beat 8");
    check(f.trackManager->getTimelineRescheduleCount() == reschedules + 1,
          "real move: playing MIDI is rescheduled so it plays from the new position");
    check(f.undoDepth() == undo + 1, "real move: one undo step");
    check(findLane(*f.manager) != laneBefore, "real move: the rows are rebuilt (the observable works)");
}

// ---- Mute tool (owner, 2026-10-20): one clip goes quiet, its track and its neighbours do not. -------------------------

// Muting one pattern clip: that clip only, one named undo step, playback refreshed while playing, fully undoable.
void testMutingOneClipLeavesTheTrackAndNeighboursAlone() {
    Fixture f;
    if (!f.build()) {
        return;
    }
    auto& playlist = f.trackManager->getPlaylistModel();
    const PatternID other = f.trackManager->getPatternManager().createMidiPattern("Other", 4.0, MidiPayload{});
    const ClipInstanceID neighbour = playlist.addClipFromPattern(f.laneId, other, 8.0, 4.0);
    check(neighbour.isValid(), "a second clip on the same track");

    check(!f.manager->isClipMuted(f.clipId), "a new clip is not muted");
    const uint64_t reschedules = f.trackManager->getTimelineRescheduleCount();
    const size_t undo = f.undoDepth();

    check(f.manager->toggleClipMute(f.clipId) == true, "toggle mutes it and says so");
    check(f.manager->isClipMuted(f.clipId), "the clip is muted");
    check(!f.manager->isClipMuted(neighbour), "the neighbour on the same track is not");
    check(!playlist.getLane(f.laneId)->muted, "the track is not muted: only the clip is");
    check(f.undoDepth() == undo + 1, "one undo step");
    check(f.trackManager->getTimelineRescheduleCount() > reschedules, "playing MIDI is refreshed so the muted clip goes quiet at once");
    check(f.trackManager->getCommandHistory().getUndoStack().back()->getName() == "Mute Clip", "the history says what happened");

    check(f.manager->toggleClipMute(f.clipId) == false, "a second toggle unmutes");
    check(f.trackManager->getCommandHistory().getUndoStack().back()->getName() == "Unmute Clip", "and says so");

    f.trackManager->getCommandHistory().undo();
    check(f.manager->isClipMuted(f.clipId), "undo of the unmute: muted again");
    f.trackManager->getCommandHistory().undo();
    check(!f.manager->isClipMuted(f.clipId), "undo of the mute: audible again");
}

// A sweep sets every clip to one state: asking for the state a clip is already in changes nothing and costs no undo step.
void testSettingTheStateItAlreadyHasIsNotAChange() {
    Fixture f;
    if (!f.build()) {
        return;
    }
    const size_t undo = f.undoDepth();
    check(f.manager->toggleClipMute(f.clipId, true) == true, "set muted");
    check(f.undoDepth() == undo + 1, "one step");
    check(f.manager->toggleClipMute(f.clipId, true) == true, "set muted again");
    check(f.undoDepth() == undo + 1, "no second step for the state it already had");
    check(f.manager->toggleClipMute(ClipInstanceID{}) == false, "no clip, nothing happens");
}

// Placing with Draw: the clip starts in the grid cell the pointer is in, not on the nearest line. Rounding put one placed
// in the right half of a cell on the NEXT cell (the same bug the piano roll had).
void testDrawPlacesInTheCellThePointerIsIn() {
    Fixture f;
    if (!f.build()) {
        return;
    }
    f.manager->setSnapSetting(AestraUI::SnapGrid::Beat);
    f.manager->selectClips({f.clipId}, TrackSelectionIntent::Replace);
    f.manager->copySelectedClip(); // something to stamp
    check(f.manager->canPaintClip(), "a copied clip is something to stamp");

    auto& playlist = f.trackManager->getPlaylistModel();
    const auto startsOf = [&]() {
        std::vector<double> starts;
        for (const auto& clip : playlist.getLane(f.laneId)->clips) starts.push_back(clip.startBeat);
        return starts;
    };
    const auto placedAt = [&](double beat) {
        const size_t before = startsOf().size();
        f.manager->onPaintClip(f.lane.get(), beat);
        const auto after = startsOf();
        return after.size() == before + 1 ? after.back() : -1.0;
    };
    check(placedAt(6.2) == 6.0, "6.2 is in the cell that starts at 6");
    check(placedAt(7.9) == 7.0, "7.9 is still in the cell that starts at 7, not the next one (the reported bug)");
    check(placedAt(9.0) == 9.0, "exactly on a line is that line's cell");
    check(placedAt(11.0 - 1e-12) == 11.0, "a pointer a hair under a line (floating point) stays in that line's cell");
    f.manager->setSnapSetting(AestraUI::SnapGrid::None);
    check(placedAt(13.37) == 13.37, "snap off places exactly where the pointer is");
}

// The tool, its number key, and the default.
void testMuteToolAndShortcuts() {
    Fixture f;
    if (!f.build()) {
        return;
    }
    check(f.manager->getCurrentTool() == PlaylistTool::Paint, "the Track Manager starts on Draw");
    const auto key = [&](AestraUI::NUIKeyCode code) {
        AestraUI::NUIKeyEvent e;
        e.keyCode = code;
        e.pressed = true;
        f.manager->onKeyEvent(e);
    };
    key(AestraUI::NUIKeyCode::Num4);
    check(f.manager->getCurrentTool() == PlaylistTool::Mute, "4 picks Mute");
    key(AestraUI::NUIKeyCode::Num3);
    check(f.manager->getCurrentTool() == PlaylistTool::Split, "3 picks Split");
    key(AestraUI::NUIKeyCode::Num2);
    check(f.manager->getCurrentTool() == PlaylistTool::Paint, "2 picks Draw");
    key(AestraUI::NUIKeyCode::Num1);
    check(f.manager->getCurrentTool() == PlaylistTool::Select, "1 picks Select");
}

} // namespace

int main() {
    testClickReleaseIsANoOp();
    testDragAwayAndBackIsANoOp();
    testRealMoveStillReschedules();
    testMutingOneClipLeavesTheTrackAndNeighboursAlone();
    testSettingTheStateItAlreadyHasIsNotAChange();
    testMuteToolAndShortcuts();
    testDrawPlacesInTheCellThePointerIsIn();

    if (g_failures == 0) {
        std::cout << "Clip drag release tests passed\n";
        return 0;
    }
    std::cout << g_failures << " clip drag release test(s) failed\n";
    return 1;
}
