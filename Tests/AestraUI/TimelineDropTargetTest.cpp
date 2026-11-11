// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Dropping into a project with no used tracks lands on Track 1, and the next drop on Track 2.
//
// Owner report (2026-11-08): "dragging in a clip when there is no tracks is flippant, sometimes lands on track 1
// sometimes even track 3". A new project is forty-odd untouched "Track N" tracks, all folded into the "N empty tracks"
// row. A drop on that row appended a brand-new track after all of them, named after the sample, and the number beside
// a track name was read from a trailing digit in the name: "kick 3" showed as track 3, "snare 1" as track 1.

#include "../Support/NullRenderer.h"
#include "NUIComponent.h"
#include "NUIThemeSystem.h"
#include "PatternManager.h"
#include "PlaylistModel.h"
#include "TimelineFold.h"
#include "TrackManager.h"
#include "TrackManagerUI.h"
#include "TrackUIComponent.h"

#include <algorithm>
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

/** @brief A new project's shape: untouched "Track N" tracks, every one folded away. */
struct Fixture {
    std::shared_ptr<TrackManager> trackManager;
    std::shared_ptr<AestraUI::NUIComponent> root;
    std::shared_ptr<TrackManagerUI> manager;
    PatternID pattern;

    void build(int tracks) {
        trackManager = std::make_shared<TrackManager>();
        trackManager->setCommandSink([](const AudioQueueCommand&) { return true; });
        auto& playlist = trackManager->getPlaylistModel();
        for (int i = 1; i <= tracks; ++i) {
            const std::string name = "Track " + std::to_string(i);
            const PlaylistLaneID laneId = playlist.createLane(name);
            auto* channel = trackManager->addChannel("Channel " + std::to_string(i));
            trackManager->createTrack(laneId, name, channel->getChannelId());
        }
        pattern = trackManager->getPatternManager().createMidiPattern("Beat", 4.0, MidiPayload{});

        manager = std::make_shared<TrackManagerUI>(trackManager);
        manager->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 600.0f));
        root = std::make_shared<AestraUI::NUIComponent>();
        root->setBounds(AestraUI::NUIRect(0.0f, 0.0f, 1400.0f, 600.0f));
        root->addChild(manager);
        manager->refreshTracks();
        Aestra::Testing::NullRenderer renderer;
        root->onRender(renderer);
    }

    AestraUI::DragData patternDrag() const {
        AestraUI::DragData data;
        data.type = AestraUI::DragDataType::Pattern;
        data.displayName = "Beat";
        data.customData = pattern;
        return data;
    }

    // A point on the folded row: just under the last track row in view, or with none in view, the first point (top
    // down) where the timeline says a drop would land.
    AestraUI::NUIPoint foldRowPoint() {
        const float controls = AestraUI::NUIThemeManager::getInstance().getLayoutDimensions().trackControlsWidth;
        const float x = controls + 200.0f;
        float below = 0.0f;
        for (const auto& child : manager->getChildren())
            if (auto row = std::dynamic_pointer_cast<TrackUIComponent>(child))
                below = std::max(below, row->getBounds().y + row->getBounds().height);
        for (float y = below; y < 600.0f; y += 2.0f) {
            if (manager->onDragOver(patternDrag(), {x, y}) == AestraUI::DropFeedback::Copy) {
                return {x, y + 4.0f};
            }
        }
        return {x, -1.0f};
    }

    // Which track (by its position, 1-based) holds clips, or 0 if none / several.
    int usedTrackNumber() {
        auto& playlist = trackManager->getPlaylistModel();
        int found = 0;
        for (size_t i = 0; i < playlist.getLaneCount(); ++i) {
            const auto* lane = playlist.getLane(playlist.getLaneId(i));
            if (lane && !lane->clips.empty()) {
                if (found != 0)
                    return -1;
                found = static_cast<int>(i) + 1;
            }
        }
        return found;
    }
    size_t laneCount() { return trackManager->getPlaylistModel().getLaneCount(); }
};

void testFirstDropLandsOnTrackOneAndTheNextOnTrackTwo() {
    Fixture f;
    f.build(5);
    const AestraUI::NUIPoint at = f.foldRowPoint();
    check(at.y >= 0.0f, "the folded row accepts a drop");

    const auto first = f.manager->onDrop(f.patternDrag(), at);
    check(first.accepted, "the first drop is accepted");
    check(f.laneCount() == 5, "no new track is made while untouched ones are folded away");
    check(f.usedTrackNumber() == 1, "the first drop lands on Track 1");

    // Track 1 is now in view; the fold row moved down one. Dropping on it uses the next empty track.
    f.manager->refreshTracks();
    const auto second = f.manager->onDrop(f.patternDrag(), f.foldRowPoint());
    check(second.accepted, "the second drop is accepted");
    check(f.laneCount() == 5, "still no new track");
    auto& playlist = f.trackManager->getPlaylistModel();
    check(!playlist.getLane(playlist.getLaneId(1))->clips.empty(), "the second drop lands on Track 2");

    // One undo takes the clip back off Track 2, and it folds away again.
    f.trackManager->getCommandHistory().undo();
    check(playlist.getLane(playlist.getLaneId(1))->clips.empty(), "undo takes the clip off Track 2");
    check(f.laneCount() == 5, "and leaves no track behind");
}

void testWithNoEmptyTrackLeftADropMakesANewOne() {
    Fixture f;
    f.build(1);
    f.manager->onDrop(f.patternDrag(), f.foldRowPoint()); // fills Track 1
    f.manager->refreshTracks();
    const auto result = f.manager->onDrop(f.patternDrag(), f.foldRowPoint());
    check(result.accepted, "a drop below the last track still works with nothing folded");
    check(f.laneCount() == 2, "and makes a new track");
}

void testTheNumberIsTheTracksPlaceNotADigitInTheSampleName() {
    check(timelineTrackNumber("Track 7", 2) == 7, "a default \"Track N\" keeps its number");
    check(timelineTrackNumber("kick 3", 0) == 1, "a sample-named track on the first row is 1, not 3");
    check(timelineTrackNumber("808 Sub 01", 4) == 5, "a trailing digit in a sample name is not a track number");
    check(timelineTrackNumber("Vocals", 9) == 10, "a named track shows its place");
}

} // namespace

int main() {
    testFirstDropLandsOnTrackOneAndTheNextOnTrackTwo();
    testWithNoEmptyTrackLeftADropMakesANewOne();
    testTheNumberIsTheTracksPlaceNotADigitInTheSampleName();
    if (g_failures == 0) {
        std::cout << "[PASS] drops into an empty project fill Track 1, then 2; numbers are the track's place\n";
        return 0;
    }
    return 1;
}
