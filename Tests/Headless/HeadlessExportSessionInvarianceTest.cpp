// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// HeadlessExportSessionInvarianceTest
//
// Encodes the invariant established in #556: HeadlessMusicGenerator::exportTo()
// is a synchronous offline render that must not mutate the caller's live
// session. It schedules the timeline into the pattern-playback engine via
// TrackManager::scheduleTimelineForOfflineRender() — NOT play() — so the
// transport flags and position are left untouched, and its RenderStateGuard
// flushes the pattern engine on every exit path so the render's scheduled
// instances do not leak.
//
// This test snapshots (isPlaying, isPaused, position) before exportTo() and
// asserts they are unchanged afterward, on BOTH the success path and a forced
// failure path (empty output path — the exporter rejects it after the engine
// wiring is already in place, exercising the guard's cleanup on failure).

#include "Core/AudioEngine.h"
#include "Headless/HeadlessMusicGenerator.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

using namespace Aestra::Audio;

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

struct TransportSnapshot {
    bool playing;
    bool paused;
    double position;
};

TransportSnapshot snapshot(const TrackManager& tm) {
    return {tm.isPlaying(), tm.isPaused(), tm.getPosition()};
}

void requireUnchanged(const TrackManager& tm, const TransportSnapshot& before, const char* path) {
    const TransportSnapshot after = snapshot(tm);
    require(after.playing == before.playing,
            (std::string("export must not change isPlaying (") + path + ")").c_str());
    require(after.paused == before.paused,
            (std::string("export must not change isPaused (") + path + ")").c_str());
    require(std::abs(after.position - before.position) < 1e-9,
            (std::string("export must not change transport position (") + path + ")").c_str());
}

// Builds a small but genuinely audible project on the given engine/manager.
void buildMinimalProject(HeadlessMusicGenerator& gen) {
    gen.createProject("invariance")
        .setTempo(120.0)
        .setSampleRate(48000)
        .createPattern("Kick", 16)
        .addNote(0, 36, 100, 1.0)
        .addNote(8, 36, 100, 1.0)
        .addClipToPlaylist("Kick", 0.0, 4.0);
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path outPath = fs::temp_directory_path() / "aestra_export_invariance.wav";
    fs::remove(outPath);

    // --- Success path: a real render leaves transport state untouched ---------
    {
        AudioEngine engine;
        TrackManager trackManager;

        HeadlessMusicGenerator gen(engine, trackManager);
        buildMinimalProject(gen);

        // Put the session in a known, non-default transport state. isPlaying
        // starts false; if exportTo() ever reverts to play() this flips to true.
        trackManager.setPosition(3.5);
        const TransportSnapshot before = snapshot(trackManager);
        require(!before.playing, "precondition: session not playing");
        require(std::abs(before.position - 3.5) < 1e-9, "precondition: position is 3.5");

        const bool ok = gen.exportTo(outPath.string(), 48000, AudioExporter::BitDepth::PCM_16);
        require(ok, "export of a valid project should succeed");
        require(fs::exists(outPath) && fs::file_size(outPath) > 1024,
                "successful export should write a non-trivial WAV");

        requireUnchanged(trackManager, before, "success");

        // Re-export the same project: the commit layer is idempotent, so it must
        // not append duplicate channels/units/clips to the TrackManager.
        const size_t channelsAfterFirst = trackManager.getChannelCount();
        fs::remove(outPath);
        const bool ok2 = gen.exportTo(outPath.string(), 48000, AudioExporter::BitDepth::PCM_16);
        require(ok2, "re-export of the same project should also succeed");
        require(trackManager.getChannelCount() == channelsAfterFirst,
                "re-export must not duplicate committed channels (idempotent commit)");
        requireUnchanged(trackManager, before, "re-export");
        std::cout << "[INFO] success path: transport state preserved, commit idempotent.\n";
    }
    fs::remove(outPath);

    // --- Forced failure path: the guard restores state even when render fails --
    {
        AudioEngine engine;
        TrackManager trackManager;

        HeadlessMusicGenerator gen(engine, trackManager);
        buildMinimalProject(gen);

        trackManager.setPosition(2.0);
        const TransportSnapshot before = snapshot(trackManager);

        // Empty output path: validate() passes (it checks project/patterns/clips),
        // so the commit layer runs and the engine is fully wired before the
        // exporter rejects the path. That exercises RenderStateGuard on failure.
        const bool ok = gen.exportTo(std::string{}, 48000, AudioExporter::BitDepth::PCM_16);
        require(!ok, "export with an empty output path must fail");

        requireUnchanged(trackManager, before, "forced-failure");
        std::cout << "[INFO] forced-failure path: transport state preserved.\n";
    }

    std::cout << "[PASS] HeadlessExportSessionInvarianceTest\n";
    return 0;
}
