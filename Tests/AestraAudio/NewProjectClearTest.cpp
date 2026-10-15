// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

// File > New Project regression: the reset cleared clips, sources and channels
// but kept the Arsenal's units and patterns, so a "new" project opened with the
// last project's rack and patterns. It also kept the track table, so every New
// Project stacked fifty more tracks on the old ones. clearProjectContent() must
// leave nothing behind, and rebuilding the defaults must start every id at 1.

#include "Models/ProjectReset.h"

#include <iostream>
#include <string>

namespace {

using namespace Aestra;

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

// The default-project shape in miniature: lanes, each owned by a track with a channel.
void seedTracks(Audio::TrackManager& tm, int count) {
    for (int i = 1; i <= count; ++i) {
        const auto laneId = tm.getPlaylistModel().createLane("Track " + std::to_string(i));
        auto* channel = tm.addChannel("Channel " + std::to_string(i));
        tm.createTrack(laneId, "Track " + std::to_string(i), channel ? channel->getChannelId() : 0);
    }
}

} // namespace

int main() {
    Audio::TrackManager tm;
    seedTracks(tm, 4);

    // The old project: a rack of units and a few patterns.
    auto& units = tm.getUnitManager();
    units.createUnit("Kick", Audio::UnitType::Sampler);
    units.createUnit("Clap", Audio::UnitType::Sampler);
    units.createUnit("Hats", Audio::UnitType::Sampler);
    auto& patterns = tm.getPatternManager();
    Audio::MidiPayload empty;
    patterns.createMidiPattern("Pattern 1", 8.0, empty);
    patterns.createMidiPattern("Pattern 2", 8.0, empty);
    expect(units.getUnitCount() == 3, "old project has three units");
    expect(patterns.getAllPatterns().size() == 2, "old project has two patterns");

    Audio::clearProjectContent(tm);

    expect(units.getUnitCount() == 0, "no units survive a new project");
    expect(patterns.getAllPatterns().empty(), "no patterns survive a new project");
    expect(tm.getTracks().empty(), "no tracks survive a new project");
    expect(tm.getChannelCount() == 0, "no channels survive a new project");
    expect(tm.getPlaylistModel().getLaneCount() == 0, "no lanes survive a new project");

    // Rebuild the defaults, as resetToDefaultProject() and the Arsenal do.
    seedTracks(tm, 4);
    const auto unitId = units.createUnit("Sampler 1", Audio::UnitType::Sampler);
    const auto patternId = patterns.createMidiPattern("Pattern 1", 8.0, empty);
    expect(tm.getTracks().size() == 4, "the track table holds the defaults once, not stacked on the old ones");
    expect(units.getUnitCount() == 1 && unitId == 1, "the starter unit is Sampler 1 with id 1");
    expect(patterns.getAllPatterns().size() == 1, "only Pattern 1 exists");
    expect(patternId.value == 1, "pattern ids start again at 1");

    if (g_failures == 0) {
        std::cout << "NewProjectClearTest: all checks passed\n";
        return 0;
    }
    return 1;
}
