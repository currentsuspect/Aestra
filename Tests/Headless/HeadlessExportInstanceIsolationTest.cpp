// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// HeadlessExportInstanceIsolationTest
//
// HeadlessMusicGenerator::exportTo() claims, in both its own comments and
// TrackManager::scheduleTimelineForOfflineRender()'s docs, that the pattern engine is
// cleared before the render and again on exit "so the render's scheduled instances do not
// leak". It implemented that with rewindScheduledInstances() — which only REWINDS active
// instances (scheduledThroughFrame = 0, re-emit from the top) and never removes them.
//
// Consequence: anything already scheduled when exportTo() is called — an Arsenal preview,
// clip instances from a previous play() — was still live during the render and got mixed
// into the exported file alongside the timeline the caller asked for. The export contained
// audio that is not in the arrangement.
//
// SCOPE: this test owns the INPUT half of the contract — prior scheduler state cannot
// contaminate the render. The OUTPUT half (the caller's session is unchanged afterwards,
// including its scheduled-instance count) is owned by HeadlessExportSessionInvarianceTest.
//
// That sibling existed throughout and stayed green, because its header asserted the
// no-leak guarantee in prose while every check only compared the transport triple —
// nothing in it COULD fail when instances leaked. Coverage exists only where an
// assertion can fail for the behaviour being claimed.
//
// This test renders the same project twice — once clean, once with a deliberately audible
// foreign instance pre-scheduled — and requires the two exports to be byte-identical.
// Against the pre-fix implementation the second render contains the foreign pattern and
// the files differ.

#include "Core/AudioEngine.h"
#include "Headless/HeadlessMusicGenerator.h"
#include "Models/TrackManager.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int failures = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "PASS: " << what << "\n";
    } else {
        std::cout << "FAIL: " << what << "\n";
        ++failures;
    }
}

std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A loud, dense pattern on a REAL unit — a contaminant that cannot be missed if it leaks.
// Using an existing unit matters: notes routed to a nonexistent unit would be silent and
// the test would pass without proving anything.
PatternID makeForeignPattern(TrackManager& tm, UnitID unitId) {
    MidiPayload payload;
    for (int i = 0; i < 8; ++i) {
        MidiNote n;
        n.pitch = 72; // distinct from the project's kick
        n.startBeat = i * 0.5;
        n.durationBeats = 0.5;
        n.velocity = 1.0f;
        n.unitId = unitId;
        payload.notes.push_back(n);
    }
    return tm.getPatternManager().createMidiPattern("foreign", 4.0, payload);
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path cleanPath = fs::temp_directory_path() / "aestra_export_isolation_clean.wav";
    const fs::path dirtyPath = fs::temp_directory_path() / "aestra_export_isolation_dirty.wav";
    fs::remove(cleanPath);
    fs::remove(dirtyPath);

    AudioEngine engine;
    TrackManager trackManager;
    HeadlessMusicGenerator gen(engine, trackManager);

    gen.createProject("isolation")
        .setTempo(120.0)
        .setSampleRate(48000)
        .createPattern("Kick", 16)
        .addNote(0, 36, 100, 1.0)
        .addNote(8, 36, 100, 1.0)
        .addClipToPlaylist("Kick", 0.0, 4.0);

    // First export also commits the project, which is what creates the units.
    check(gen.exportTo(cleanPath.string(), 48000, AudioExporter::BitDepth::PCM_16),
          "clean export succeeds");
    const std::vector<uint8_t> cleanBytes = readAll(cleanPath);
    check(cleanBytes.size() > 1024, "clean export wrote a non-trivial WAV");

    // NOTE: the OUTPUT half — that the export leaves no instances behind in the caller's
    // session — is asserted by HeadlessExportSessionInvarianceTest, which owns
    // "the caller's session is unchanged afterwards". It is deliberately not duplicated
    // here so the two tests have non-overlapping ownership. Both failed on the red run;
    // one lifecycle bug, two observable consequences.

    // Now contaminate: schedule a foreign, audible instance the way a leftover Arsenal
    // preview or a previous play() would have, then export the SAME project again.
    const auto unitIds = trackManager.getUnitManager().getAllUnitIDs();
    check(!unitIds.empty(), "committed project created at least one unit to route to");
    if (!unitIds.empty()) {
        const PatternID foreign = makeForeignPattern(trackManager, unitIds.front());
        // Relative, not absolute: whether the previous export left instances behind is the
        // SIBLING test's concern. This precondition must hold either way so that a failure
        // here can only mean contamination, never leakage.
        const size_t beforeSchedule = trackManager.getPatternPlaybackEngine().getActiveInstanceCount();
        trackManager.getPatternPlaybackEngine().schedulePatternInstance(foreign, 0.0, 7);
        check(trackManager.getPatternPlaybackEngine().getActiveInstanceCount() == beforeSchedule + 1,
              "foreign instance is scheduled before the export");

        check(gen.exportTo(dirtyPath.string(), 48000, AudioExporter::BitDepth::PCM_16),
              "export with a foreign instance pending succeeds");
        const std::vector<uint8_t> dirtyBytes = readAll(dirtyPath);

        // THE ASSERTION: the export contains the timeline's content and nothing else.
        // Re-export is idempotent (HeadlessExportSessionInvarianceTest covers that), so
        // any difference here is the foreign pattern bleeding into the render.
        check(dirtyBytes.size() == cleanBytes.size(), "exports have identical length");
        check(dirtyBytes == cleanBytes,
              "export contains ONLY the timeline's content — a pre-scheduled foreign "
              "instance must not reach the rendered file");
    }

    fs::remove(cleanPath);
    fs::remove(dirtyPath);

    if (failures == 0) {
        std::cout << "All headless export isolation checks passed\n";
        return 0;
    }
    std::cout << failures << " check(s) failed\n";
    return 1;
}
