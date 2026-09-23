// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// SPEC 3 §2.1: during timeline playback the piano roll's playhead follows the clip playing
// its pattern. It used to park at 0 because the panel resolved no clip, so it never moved
// while the song played ("It's set to a fixed position").
//
// patternLocalBeatAt() is the mapping the panel now uses: the first clip of the pattern
// under the arrangement playhead, measured from that clip's start, which is the origin the
// pattern scheduler emits its notes from. Real clips on a real playlist.

#include "Models/PatternManager.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <iostream>
#include <optional>
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

PatternID makeMidiPattern(TrackManager& tm, const char* name) {
    auto& patterns = tm.getPatternManager();
    const PatternID id = patterns.createPattern();
    auto* p = patterns.getPattern(id);
    p->type = PatternSource::Type::Midi;
    p->name = name;
    p->lengthBeats = 8.0;
    p->payload = MidiPayload{};
    return id;
}

ClipInstanceID place(TrackManager& tm, PlaylistLaneID lane, PatternID pattern, double start, double length) {
    ClipInstance clip;
    clip.patternId = pattern;
    clip.sourceId = pattern.value;
    clip.startBeat = start;
    clip.durationBeats = length;
    return tm.getPlaylistModel().addClip(lane, clip);
}

bool near(const std::optional<double>& got, double want) { return got && std::abs(*got - want) < 1e-9; }

} // namespace

int main() {
    TrackManager tm;
    const PatternID drums = makeMidiPattern(tm, "Drums");
    const PatternID bass = makeMidiPattern(tm, "Bass");
    const PlaylistLaneID laneA = tm.getPlaylistModel().createLane("A");
    const PlaylistLaneID laneB = tm.getPlaylistModel().createLane("B");
    // Drums at 8..16 and 24..32; Bass at 0..32 on another lane.
    place(tm, laneA, drums, 8.0, 8.0);
    place(tm, laneA, drums, 24.0, 8.0);
    place(tm, laneB, bass, 0.0, 32.0);
    const auto instances = tm.getPlaylistModel().collectMidiClipInstances(tm.getPatternManager());

    check(near(patternLocalBeatAt(instances, drums, 10.5), 2.5),
          "inside the first drums clip, the playhead is 2.5 beats into the pattern");
    check(near(patternLocalBeatAt(instances, drums, 27.0), 3.0),
          "inside the second drums clip, it measures from THAT clip's start");
    check(!patternLocalBeatAt(instances, drums, 20.0).has_value(),
          "between drums clips there is no position (the panel parks at 0)");
    check(!patternLocalBeatAt(instances, drums, 16.0).has_value(), "a clip's end is exclusive");
    check(near(patternLocalBeatAt(instances, drums, 8.0), 0.0), "a clip's start is inclusive");
    check(near(patternLocalBeatAt(instances, bass, 20.0), 20.0),
          "another pattern's clips never answer for this one");

    if (g_failures == 0) {
        std::cout << "Piano roll playhead mapping tests passed\n";
        return 0;
    }
    std::cout << g_failures << " piano roll playhead mapping test(s) failed\n";
    return 1;
}
