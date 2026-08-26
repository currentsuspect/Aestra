// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

// Fit-to-bars persistence regression (#863): the fit transaction writes the
// canonical durationSeconds (serializer-preferred field). This test pins that
// a fitted pitch+rate clip round-trips with the #746 invariant intact —
// durationSeconds == beatToSeconds(durationBeats) / effectiveVarispeed —
// so the stale-varispeed defect can never silently land in a saved project.

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Commands/CommandTransaction.h"
#include "Commands/SetClipEditsCommand.h"
#include "Commands/TrimClipCommand.h"
#include "Models/ClipFit.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

// Minimal PCM 16-bit mono WAV writer (enough to satisfy SourceManager file loading).
bool writeMinimalWavMono16(const std::filesystem::path& path, int sampleRate, int numSamples) {
    if (sampleRate <= 0 || numSamples <= 0)
        return false;

    const int numChannels = 1;
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;
    const int blockAlign = numChannels * bytesPerSample;
    const int byteRate = sampleRate * blockAlign;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(numSamples * blockAlign);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    auto writeU32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

    out.write("RIFF", 4);
    writeU32(36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32(16);
    writeU16(1);
    writeU16(numChannels);
    writeU32(static_cast<std::uint32_t>(sampleRate));
    writeU32(static_cast<std::uint32_t>(byteRate));
    writeU16(static_cast<std::uint16_t>(blockAlign));
    writeU16(static_cast<std::uint16_t>(bitsPerSample));
    out.write("data", 4);
    writeU32(dataSize);
    for (int i = 0; i < numSamples; ++i) {
        std::int16_t s = static_cast<std::int16_t>((i % 200) * 100);
        out.write(reinterpret_cast<const char*>(&s), sizeof(s));
    }
    return true;
}

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace Aestra::Audio;

    const Aestra::Tests::ScopedTempDirectory tempDirScope{"ClipFitPersistence"};
    const auto& tempDir = tempDirScope.path();
    const auto wavPath = tempDir / "fit-src.wav";
    const auto projectPath = tempDir / "fitted.aes";

    require(writeMinimalWavMono16(wavPath, 48000, 48000), "failed to write 1s test WAV");

    // --- Arrange: one 1s audio clip at default [0, 2 beats) @120 BPM.
    auto tm1 = std::make_shared<TrackManager>();
    tm1->getPlaylistModel().setPatternManager(&tm1->getPatternManager());

    auto& sm1 = tm1->getSourceManager();
    const ClipSourceID srcId = sm1.getOrCreateSource(wavPath.string());
    require(srcId.isValid(), "source failed to load");

    AudioSlicePayload payload;
    payload.audioSourceId = srcId;
    payload.slices.push_back({0.0, 48000.0});
    const PatternID patId = tm1->getPatternManager().createAudioPattern("FitSrc", 2.0, payload);
    require(patId.isValid(), "pattern failed to create");

    auto& playlist1 = tm1->getPlaylistModel();
    playlist1.setBPM(120.0);
    const PlaylistLaneID laneId = playlist1.createLane("Fit Lane");
    require(laneId.isValid(), "lane failed to create");

    const ClipInstanceID clipId = playlist1.addClipFromPattern(laneId, patId, 0.0, 2.0);
    require(clipId.isValid(), "clip failed to add");

    // --- Act: the panel's fit transaction — pitch +12, edits FIRST, trim second
    // (1 s content, 1 bar @120 BPM = 2 s span -> effective 0.5x, base 0.25x).
    auto* clip = playlist1.getClip(clipId);
    const auto fit = computeFitToBars(1.0, playlist1.getBPM(), 1, 12.0f);
    require(fit.has_value(), "fit math failed");
    require(fit && !fit->rateClamped, "expected an unclamped fit for this scenario");

    ClipEdits edits = clip->edits;
    edits.pitchSemitones = 12.0f;
    edits.playbackRate = fit->playbackRate;
    auto& history = tm1->getCommandHistory();
    history.beginTransaction(std::make_shared<CommandTransaction>("Fit clip to bars"));
    history.pushAndExecute(std::make_shared<SetClipEditsCommand>(playlist1, clipId, edits));
    history.pushAndExecute(std::make_shared<TrimClipCommand>(playlist1, clipId, -1.0,
                                                             clip->startBeat + fit->durationBeats));
    history.commitTransaction();

    clip = playlist1.getClip(clipId);
    require(clip != nullptr, "clip missing after fit");
    require(std::abs(clip->durationBeats - 4.0) < 1e-9, "pre-save beat span wrong");
    require(std::abs(clip->edits.playbackRate - 0.25f) < 1e-6f, "pre-save base rate wrong");
    const double preSaveExpectedSeconds =
        playlist1.beatToSeconds(clip->durationBeats) / static_cast<double>(clip->edits.effectiveVarispeed());
    require(std::abs(clip->durationSeconds - preSaveExpectedSeconds) < 1e-9,
            "pre-save canonical durationSeconds violates the #746 invariant");

    // --- Save canonical project, load into a fresh TrackManager.
    require(ProjectSerializer::save(projectPath.string(), tm1, 120.0, 0.0), "save failed");

    auto tm2 = std::make_shared<TrackManager>();
    tm2->getPlaylistModel().setPatternManager(&tm2->getPatternManager());
    const ProjectSerializer::LoadResult loadResult = ProjectSerializer::load(projectPath.string(), tm2);
    require(loadResult.ok, "load failed");

    // --- Assert: the fitted state survives with the canonical invariant intact.
    require(std::abs(loadResult.tempo - 120.0) < 1e-9, "tempo did not roundtrip");
    auto& playlist2 = tm2->getPlaylistModel();
    require(playlist2.getLaneCount() == 1, "lane count mismatch after load");
    const auto* loadedLane = playlist2.getLane(playlist2.getLaneId(0));
    require(loadedLane != nullptr && loadedLane->clips.size() == 1, "fitted clip missing after load");
    if (!loadedLane || loadedLane->clips.empty()) {
        return 1;
    }
    const auto& loaded = loadedLane->clips[0];
    require(std::abs(loaded.durationBeats - 4.0) < 1e-9, "fitted beat span did not roundtrip");
    require(std::abs(loaded.edits.playbackRate - 0.25f) < 1e-6f, "fitted base rate did not roundtrip");
    require(std::abs(loaded.edits.pitchSemitones - 12.0f) < 1e-6f, "fitted pitch did not roundtrip");
    const double loadedExpectedSeconds =
        playlist2.beatToSeconds(loaded.durationBeats) / static_cast<double>(loaded.edits.effectiveVarispeed());
    require(std::abs(loaded.durationSeconds - loadedExpectedSeconds) < 1e-9,
            "canonical durationSeconds did not roundtrip with the #746 invariant");
    require(std::abs(loaded.edits.effectiveVarispeed() - 0.5f) < 1e-6f,
            "effective varispeed did not roundtrip");

    // --- Root-fix leg: a plain rate+pitch edit (the clip-editor slider path)
    // must re-derive the canonical at edit time and round-trip the span.
    // Before the fix, setClipEdits left durationSeconds at the pre-edit
    // varispeed and the loader flat-converted it — the loaded span shrank or
    // doubled on every save cycle.
    const auto* loadedLane2 = playlist2.getLane(playlist2.getLaneId(0));
    require(loadedLane2 != nullptr && !loadedLane2->clips.empty(), "loaded lane missing for edit leg");
    if (!loadedLane2 || loadedLane2->clips.empty()) {
        return 1;
    }
    const ClipInstanceID loadedClipId = loadedLane2->clips[0].id;
    auto* loadedClip = playlist2.getClip(loadedClipId);
    require(loadedClip != nullptr, "loaded clip missing for edit leg");
    ClipEdits edited = loadedClip->edits;
    edited.playbackRate = 1.5f;
    require(playlist2.setClipEdits(loadedClipId, edited), "setClipEdits failed");
    loadedClip = playlist2.getClip(loadedClipId);
    {
        const double invariantSeconds =
            playlist2.beatToSeconds(loadedClip->durationBeats) /
            static_cast<double>(loadedClip->edits.effectiveVarispeed());
        require(std::abs(loadedClip->durationSeconds - invariantSeconds) < 1e-9,
                "rate edit did not re-derive the canonical durationSeconds");
    }

    const auto projectPath2 = tempDir / "rated.aes";
    require(ProjectSerializer::save(projectPath2.string(), tm2, 120.0, 0.0), "second save failed");
    auto tm3 = std::make_shared<TrackManager>();
    tm3->getPlaylistModel().setPatternManager(&tm3->getPatternManager());
    const ProjectSerializer::LoadResult load2 = ProjectSerializer::load(projectPath2.string(), tm3);
    require(load2.ok, "second load failed");
    auto& playlist3 = tm3->getPlaylistModel();
    require(playlist3.getLaneCount() == 1, "lane count mismatch after edit roundtrip");
    const auto* lane3 = playlist3.getLane(playlist3.getLaneId(0));
    require(lane3 != nullptr && lane3->clips.size() == 1, "edited clip missing after roundtrip");
    if (!lane3 || lane3->clips.empty()) {
        return 1;
    }
    const auto& reloaded = lane3->clips[0];
    require(std::abs(reloaded.durationBeats - 4.0) < 1e-9, "edited clip span shifted on roundtrip");
    require(std::abs(reloaded.edits.playbackRate - 1.5f) < 1e-6f, "edited clip rate did not roundtrip");
    require(std::abs(reloaded.edits.pitchSemitones - 12.0f) < 1e-6f, "edited clip pitch did not roundtrip");
    const double reloadedInvariant =
        playlist3.beatToSeconds(reloaded.durationBeats) /
        static_cast<double>(reloaded.edits.effectiveVarispeed());
    require(std::abs(reloaded.durationSeconds - reloadedInvariant) < 1e-9,
            "edited clip canonical invariant did not survive roundtrip");

    std::cout << "[PASS] ClipFitPersistenceTest\n";
    return 0;
}