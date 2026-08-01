// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// End-to-end proof that the destructive clip operations change what a clip
// actually plays, and that undo puts the original audio back. The clip is built
// the same way the import path builds one (source -> audio pattern -> clip), so
// a break in that contract fails here rather than only in the app.

#include "Commands/RenderAudioClipCommand.h"
#include "Models/ClipRenderService.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void require(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

/** Front-loaded ramp: frame 0 is loudest, the tail is near silence. */
std::shared_ptr<AudioBufferData> makeDecayBuffer(uint64_t frames) {
    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = 48000;
    buffer->numChannels = 2;
    buffer->numFrames = frames;
    buffer->interleavedData.resize(static_cast<size_t>(frames) * 2);
    for (uint64_t i = 0; i < frames; ++i) {
        const float v = 1.0f - static_cast<float>(i) / static_cast<float>(frames);
        buffer->interleavedData[i * 2] = v;
        buffer->interleavedData[i * 2 + 1] = v;
    }
    return buffer;
}

/** Sum of squares over the first or last half of a clip's current audio. */
double halfEnergy(const AudioBufferData& buffer, bool front) {
    const uint64_t half = buffer.numFrames / 2;
    const uint64_t begin = front ? 0 : half;
    const uint64_t end = front ? half : buffer.numFrames;
    double energy = 0.0;
    for (uint64_t f = begin; f < end; ++f) {
        const double s = buffer.interleavedData[f * 2];
        energy += s * s;
    }
    return energy;
}

/** Resolve the audio a clip currently points at, via the production seam. */
std::shared_ptr<const AudioBufferData> currentAudio(TrackManager& tm, ClipInstanceID clipId) {
    const ClipInstance* clip = tm.getPlaylistModel().getClip(clipId);
    if (!clip) {
        return nullptr;
    }
    ClipRenderService service(tm.getSourceManager(), tm.getPatternManager());
    return service.resolveClipRegion(*clip, tm.getPlaylistModel().getProjectSampleRate()).buffer;
}

struct Fixture {
    std::shared_ptr<TrackManager> tm;
    ClipInstanceID clipId;
    std::string renderDir;
};

/** Build source -> audio pattern -> clip exactly as the import path does. */
Fixture makeClipFixture(const std::string& renderDir) {
    Fixture fx;
    fx.tm = std::make_shared<TrackManager>();
    fx.renderDir = renderDir;
    // Keeps rendered files inside the test's own directory.
    fx.tm->setRecordingProjectPath(renderDir);

    auto buffer = makeDecayBuffer(2000);
    const double durationSeconds = buffer->durationSeconds();

    const ClipSourceID sourceId =
        fx.tm->getSourceManager().createRecordedSource(renderDir + "/source.wav", "source", buffer);

    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = durationSeconds;
    AudioSlice fullSlice;
    fullSlice.startSamples = 0.0;
    fullSlice.lengthSamples = static_cast<double>(buffer->numFrames);
    payload.slices.push_back(fullSlice);

    const double durationBeats = durationSeconds * fx.tm->getPlaylistModel().getBPM() / 60.0;
    const PatternID patternId = fx.tm->getPatternManager().createAudioPattern("source", durationBeats, payload);

    const PlaylistLaneID laneId = fx.tm->getPlaylistModel().createLane("Lane");

    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.name = "source";
    clip.startBeat = 0.0;
    clip.durationBeats = durationBeats;
    clip.durationSeconds = durationSeconds;
    clip.patternId = patternId;
    clip.edits = ClipEdits::forNewAudioClip();
    fx.tm->getPlaylistModel().addClip(laneId, clip);
    fx.clipId = clip.id;
    return fx;
}

void testReverseChangesAudioAndUndoRestores(const std::string& dir) {
    std::printf("reverse: audio flips, undo restores...\n");
    Fixture fx = makeClipFixture(dir);

    auto before = currentAudio(*fx.tm, fx.clipId);
    require(before != nullptr, "clip resolves to audio before reverse");
    if (!before) {
        return;
    }
    const double frontBefore = halfEnergy(*before, true);
    const double backBefore = halfEnergy(*before, false);
    require(frontBefore > backBefore * 2.0, "fixture really is front-loaded");

    const PatternID patternBefore = fx.tm->getPlaylistModel().getClip(fx.clipId)->patternId;

    ReverseAudioClipCommand command(*fx.tm, fx.clipId);
    command.execute();

    require(command.isUndoable(), "reverse reports that it changed something");

    // commit() returning invalid makes execute() bail, which would otherwise
    // look identical to "reverse did nothing"; pin the write path itself.
    bool wroteWav = false;
    std::error_code renderEc;
    for (const auto& entry : std::filesystem::directory_iterator(fx.tm->renderRootDirectory(), renderEc)) {
        if (entry.path().extension() == ".wav") {
            wroteWav = true;
            break;
        }
    }
    require(wroteWav, "reverse writes its rendered audio to the render directory");
    const PatternID patternAfter = fx.tm->getPlaylistModel().getClip(fx.clipId)->patternId;
    require(!(patternAfter == patternBefore), "reverse repoints the clip at a new pattern");

    auto after = currentAudio(*fx.tm, fx.clipId);
    require(after != nullptr, "clip still resolves to audio after reverse");
    if (after) {
        // The whole point: the energy must move to the back half.
        const double frontAfter = halfEnergy(*after, true);
        const double backAfter = halfEnergy(*after, false);
        require(backAfter > frontAfter * 2.0, "reversed clip is back-loaded");
        require(after->numFrames == before->numFrames, "reverse preserves length");
    }

    command.undo();
    const PatternID patternUndone = fx.tm->getPlaylistModel().getClip(fx.clipId)->patternId;
    require(patternUndone == patternBefore, "undo restores the original pattern");
    auto undone = currentAudio(*fx.tm, fx.clipId);
    require(undone != nullptr, "clip resolves to audio after undo");
    if (undone) {
        require(halfEnergy(*undone, true) > halfEnergy(*undone, false) * 2.0, "undo restores front-loaded audio");
    }

    // Redo must not render a second file; it reuses the detached pattern.
    command.redo();
    require(fx.tm->getPlaylistModel().getClip(fx.clipId)->patternId == patternAfter,
            "redo returns the same rendered pattern");
}

void testCommitBakesGainAndResetsEdits(const std::string& dir) {
    std::printf("commit edits: gain is baked and edits reset...\n");
    Fixture fx = makeClipFixture(dir);

    ClipEdits edits = fx.tm->getPlaylistModel().getClip(fx.clipId)->edits;
    edits.gainLinear = 0.25f;
    edits.fadeInBeats = 0.0f;
    edits.fadeOutBeats = 0.0f;
    fx.tm->getPlaylistModel().setClipEdits(fx.clipId, edits);

    auto before = currentAudio(*fx.tm, fx.clipId);
    require(before != nullptr, "clip resolves before commit");
    if (!before) {
        return;
    }
    const float peakBefore = ClipRenderService::peakMagnitude(*before);

    CommitAudioClipEditsCommand command(*fx.tm, fx.clipId);
    command.execute();
    require(command.isUndoable(), "commit reports that it changed something");

    auto after = currentAudio(*fx.tm, fx.clipId);
    require(after != nullptr, "clip resolves after commit");
    if (after) {
        const float peakAfter = ClipRenderService::peakMagnitude(*after);
        // 0.25 gain baked in: the committed audio is a quarter as loud.
        require(std::fabs(peakAfter - peakBefore * 0.25f) < 0.01f, "commit bakes the clip gain into the audio");
    }

    const ClipEdits afterEdits = fx.tm->getPlaylistModel().getClip(fx.clipId)->edits;
    require(std::fabs(afterEdits.gainLinear - 1.0f) < 1.0e-6f, "commit resets gain to unity so it is not applied twice");

    command.undo();
    const ClipEdits undoneEdits = fx.tm->getPlaylistModel().getClip(fx.clipId)->edits;
    require(std::fabs(undoneEdits.gainLinear - 0.25f) < 1.0e-6f, "undo restores the original gain");
    auto undone = currentAudio(*fx.tm, fx.clipId);
    if (undone) {
        require(std::fabs(ClipRenderService::peakMagnitude(*undone) - peakBefore) < 1.0e-4f,
                "undo restores the original audio");
    }
}

void testSlipOffsetIsHonouredAndBaked(const std::string& dir) {
    std::printf("reverse: a slipped clip renders the region it plays...\n");
    Fixture fx = makeClipFixture(dir);

    // Slip halfway in, the way the editor's "Source start" slider does. The
    // region resolver used to ignore this and render from frame 0, so reverse
    // committed audio the user could not hear.
    auto* clip = fx.tm->getPlaylistModel().getClip(fx.clipId);
    require(clip != nullptr, "fixture clip exists");
    if (!clip) {
        return;
    }
    const uint64_t sourceFrames = 2000;
    const double projectRate = fx.tm->getPlaylistModel().getProjectSampleRate();
    ClipEdits edits = clip->edits;
    edits.gainLinear = 1.0f;
    edits.sourceStart = static_cast<double>(sourceFrames / 2) * (projectRate / 48000.0);
    fx.tm->getPlaylistModel().setClipEdits(fx.clipId, edits);

    {
        ClipRenderService service(fx.tm->getSourceManager(), fx.tm->getPatternManager());
        const auto* live = fx.tm->getPlaylistModel().getClip(fx.clipId);
        const auto region = service.resolveClipRegion(*live, projectRate);
        require(region.isValid(), "a slipped clip still resolves");
        if (region.isValid()) {
            require(region.startFrame >= sourceFrames / 2 - 1 && region.startFrame <= sourceFrames / 2 + 1,
                    "the resolved region starts at the slip, not at frame 0");
            require(region.frameCount <= sourceFrames / 2 + 1, "the resolved region ends with the source");
            // The fixture ramps down from 1.0, so the second half peaks near 0.5.
            auto extracted = ClipRenderService::extractRegion(*region.buffer, region.startFrame, region.frameCount);
            require(extracted != nullptr, "the slipped region extracts");
            if (extracted) {
                require(ClipRenderService::peakMagnitude(*extracted) < 0.6f,
                        "the resolved audio is the second half, not the whole source");
            }
        }
    }

    ReverseAudioClipCommand command(*fx.tm, fx.clipId);
    command.execute();
    require(command.isUndoable(), "reversing a slipped clip succeeds");

    // The new source starts at the region, so the slip is already baked in.
    const ClipEdits after = fx.tm->getPlaylistModel().getClip(fx.clipId)->edits;
    require(after.sourceStart == 0.0, "the baked slip is cleared so it is not applied twice");

    command.undo();
    const ClipEdits undone = fx.tm->getPlaylistModel().getClip(fx.clipId)->edits;
    require(std::fabs(undone.sourceStart - edits.sourceStart) < 1.0e-6, "undo restores the original slip");
}

void testMidiClipIsRefused(const std::string& dir) {
    std::printf("reverse: a non-audio clip is refused, not corrupted...\n");
    Fixture fx = makeClipFixture(dir);

    // Point the clip at a MIDI pattern; reverse has nothing to render.
    const PatternID midiPattern = fx.tm->getPatternManager().createMidiPattern("midi", 4.0, MidiPayload{});
    fx.tm->getPlaylistModel().setClipPattern(fx.clipId, midiPattern);

    ReverseAudioClipCommand command(*fx.tm, fx.clipId);
    command.execute();

    require(!command.isUndoable(), "reversing a MIDI clip reports that nothing changed");
    require(fx.tm->getPlaylistModel().getClip(fx.clipId)->patternId == midiPattern,
            "a refused reverse leaves the clip pointing where it was");
}

} // namespace

int main() {
    std::printf("=== RenderAudioClipCommand ===\n");

    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "aestra_render_clip_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    if (ec) {
        std::printf("FAILED: could not create test directory\n");
        return EXIT_FAILURE;
    }

    testReverseChangesAudioAndUndoRestores(dir.string());
    testCommitBakesGainAndResetsEdits(dir.string());
    testSlipOffsetIsHonouredAndBaked(dir.string());
    testMidiClipIsRefused(dir.string());

    fs::remove_all(dir, ec);

    if (g_failures != 0) {
        std::printf("FAILED: %d check(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("All RenderAudioClipCommand checks passed.\n");
    return EXIT_SUCCESS;
}
