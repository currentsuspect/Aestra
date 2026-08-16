// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Clip Commands Unit Tests
// Tests: TrimClipCommand, DuplicateClipCommand, RemoveClipCommand

#include "Commands/AddClipCommand.h"
#include "Commands/DuplicateClipCommand.h"
#include "Commands/RemoveClipCommand.h"
#include "Commands/TrimClipCommand.h"
#include "Models/ClipInstance.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>

using namespace Aestra::Audio;

namespace {

/** Build source -> audio pattern -> clip exactly as the import path does. */
ClipInstanceID seedAudioClip(TrackManager& tracks, PlaylistLaneID& laneOut, const std::string& renderDir) {
    auto& playlist = tracks.getPlaylistModel();
    playlist.setPatternManager(&tracks.getPatternManager());
    tracks.setRecordingProjectPath(renderDir);

    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = 48000;
    buffer->numChannels = 2;
    buffer->numFrames = 4800;
    buffer->interleavedData.assign(4800 * 2, 0.0f);

    const ClipSourceID sourceId =
        tracks.getSourceManager().createRecordedSource(renderDir + "/source.wav", "source", buffer);

    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    AudioSlice slice;
    slice.startSamples = 0.0;
    slice.lengthSamples = static_cast<double>(buffer->numFrames);
    payload.slices.push_back(slice);

    const PatternID patternId = tracks.getPatternManager().createAudioPattern("source", 4.0, payload);

    laneOut = playlist.createLane("Lane");
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.startBeat = 4.0;
    clip.durationBeats = 4.0;
    clip.durationSeconds = playlist.beatToSeconds(4.0);
    clip.sourceOffsetSeconds = 0.0;
    clip.patternId = patternId;
    clip.edits = ClipEdits::forNewAudioClip();
    playlist.addClip(laneOut, clip);
    return clip.id;
}

} // namespace

void testTrimClipCommand() {
    std::cout << "TEST: TrimClipCommand... ";

    PlaylistModel model;

    // Add a lane
    PlaylistLaneID laneId = model.createLane();

    // Add a clip
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.startBeat = 4.0;
    clip.durationBeats = 8.0;
    model.addClip(laneId, clip);

    // Trim: move start from 4 to 6, end from 12 to 10
    TrimClipCommand cmd(model, clip.id, 6.0, 10.0);
    cmd.execute();

    ClipInstance* trimmed = model.getClip(clip.id);
    assert(trimmed != nullptr);
    assert(trimmed->startBeat == 6.0);
    assert(trimmed->durationBeats == 4.0); // 10 - 6 = 4

    cmd.undo();
    assert(trimmed->startBeat == 4.0);
    assert(trimmed->durationBeats == 8.0);

    cmd.redo();
    assert(trimmed->startBeat == 6.0);
    assert(trimmed->durationBeats == 4.0);

    std::cout << "✅ PASS\n";
}

void testDuplicateClipCommand() {
    std::cout << "TEST: DuplicateClipCommand... ";

    PlaylistModel model;

    // Add a lane
    PlaylistLaneID laneId = model.createLane();

    // Add a source clip
    ClipInstance sourceClip;
    sourceClip.id = ClipInstanceID::generate();
    sourceClip.startBeat = 0.0;
    sourceClip.durationBeats = 4.0;
    model.addClip(laneId, sourceClip);

    // Duplicate at beat 8
    DuplicateClipCommand cmd(model, sourceClip.id, 8.0);
    cmd.execute();

    ClipInstanceID duplicateId = cmd.getDuplicateId();
    assert(duplicateId.isValid());

    ClipInstance* duplicate = model.getClip(duplicateId);
    assert(duplicate != nullptr);
    assert(duplicate->startBeat == 8.0);
    assert(duplicate->durationBeats == 4.0);
    assert(duplicate->id != sourceClip.id);

    // Undo - duplicate should be removed
    cmd.undo();
    assert(model.getClip(duplicateId) == nullptr);

    // Redo - duplicate should be back
    cmd.redo();
    assert(model.getClip(duplicateId) != nullptr);

    std::cout << "✅ PASS\n";
}

// =============================================================================
// RemoveClipCommand tests
// =============================================================================

void testRemoveClipCommand() {
    std::cout << "TEST: RemoveClipCommand execute/undo/redo... ";

    PlaylistModel model;
    PlaylistLaneID laneId = model.createLane();

    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.startBeat = 2.0;
    clip.durationBeats = 4.0;
    model.addClip(laneId, clip);

    assert(model.getClip(clip.id) != nullptr);

    RemoveClipCommand cmd(model, clip.id);
    cmd.execute();

    // Clip should be removed
    assert(model.getClip(clip.id) == nullptr);

    // Undo should restore the clip
    cmd.undo();
    ClipInstance* restored = model.getClip(clip.id);
    assert(restored != nullptr);
    assert(restored->startBeat == 2.0);
    assert(restored->durationBeats == 4.0);

    // Redo should remove it again
    cmd.redo();
    assert(model.getClip(clip.id) == nullptr);

    std::cout << "✅ PASS\n";
}

void testRemoveClipCommandDoubleExecuteNoOp() {
    std::cout << "TEST: RemoveClipCommand double execute no-op... ";

    PlaylistModel model;
    PlaylistLaneID laneId = model.createLane();

    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.startBeat = 0.0;
    clip.durationBeats = 2.0;
    model.addClip(laneId, clip);

    RemoveClipCommand cmd(model, clip.id);
    cmd.execute();
    assert(model.getClip(clip.id) == nullptr);

    // Second execute should be no-op (clip already removed)
    cmd.execute();
    assert(model.getClip(clip.id) == nullptr);

    // Undo should still work
    cmd.undo();
    assert(model.getClip(clip.id) != nullptr);

    std::cout << "✅ PASS\n";
}

void testRemoveClipCommandUndoBeforeExecuteNoOp() {
    std::cout << "TEST: RemoveClipCommand undo before execute no-op... ";

    PlaylistModel model;
    PlaylistLaneID laneId = model.createLane();

    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.startBeat = 0.0;
    clip.durationBeats = 2.0;
    model.addClip(laneId, clip);

    RemoveClipCommand cmd(model, clip.id);
    // Undo before execute - should be no-op
    cmd.undo();

    // Clip should still be present
    assert(model.getClip(clip.id) != nullptr);

    std::cout << "✅ PASS\n";
}

void testRemoveClipCommandInvalidId() {
    std::cout << "TEST: RemoveClipCommand invalid clip ID... ";

    PlaylistModel model;
    model.createLane();

    // Attempt to remove a clip that doesn't exist - should not crash
    ClipInstanceID nonExistentId = ClipInstanceID::generate();
    RemoveClipCommand cmd(model, nonExistentId);
    cmd.execute(); // Should handle gracefully

    // Nothing to undo since execute was a no-op
    cmd.undo(); // Should not crash

    std::cout << "✅ PASS\n";
}

void testRemoveClipCommandMetadata() {
    std::cout << "TEST: RemoveClipCommand metadata... ";

    PlaylistModel model;
    PlaylistLaneID laneId = model.createLane();

    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    model.addClip(laneId, clip);

    RemoveClipCommand cmd(model, clip.id);

    assert(cmd.getName() == "Remove Clip");
    assert(cmd.changesProjectState() == true);
    assert(cmd.getSizeInBytes() > 0);

    std::cout << "✅ PASS\n";
}

void testRemoveClipCommandPreservesClipData() {
    std::cout << "TEST: RemoveClipCommand preserves clip data on undo... ";

    PlaylistModel model;
    PlaylistLaneID laneId = model.createLane();

    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.startBeat = 8.0;
    clip.durationBeats = 16.0;
    model.addClip(laneId, clip);

    RemoveClipCommand cmd(model, clip.id);
    cmd.execute();
    assert(model.getClip(clip.id) == nullptr);

    cmd.undo();
    ClipInstance* restored = model.getClip(clip.id);
    assert(restored != nullptr);
    // Verify exact data is preserved
    assert(restored->id == clip.id);
    assert(restored->startBeat == 8.0);
    assert(restored->durationBeats == 16.0);

    std::cout << "✅ PASS\n";
}

void testTrimClipCommandAudioContract(const std::string& dir) {
    std::cout << "TEST: TrimClipCommand audio contract (duration + source offset + undo)... ";

    auto tracks = std::make_unique<TrackManager>();
    auto& playlist = tracks->getPlaylistModel();
    PlaylistLaneID laneId;
    const ClipInstanceID clipId = seedAudioClip(*tracks, laneId, dir);

    const double secondsPerBeat = playlist.beatToSeconds(1.0);

    // Left-edge trim: start 4 -> 6, end 8. Right edge stays pinned, so the
    // source read offset advances by the trimmed musical amount.
    {
        TrimClipCommand cmd(playlist, clipId, 4.0, 8.0, 0.0, playlist.beatToSeconds(4.0), 6.0, 8.0);
        cmd.execute();

        ClipInstance* trimmed = playlist.getClip(clipId);
        assert(trimmed->startBeat == 6.0);
        assert(trimmed->durationBeats == 2.0);
        assert(trimmed->durationSeconds == playlist.beatToSeconds(2.0));
        assert(trimmed->sourceOffsetSeconds == 2.0 * secondsPerBeat);

        cmd.undo();
        assert(trimmed->startBeat == 4.0);
        assert(trimmed->durationBeats == 4.0);
        assert(trimmed->durationSeconds == playlist.beatToSeconds(4.0));
        assert(trimmed->sourceOffsetSeconds == 0.0);

        cmd.redo();
        assert(trimmed->startBeat == 6.0);
        assert(trimmed->durationBeats == 2.0);
        assert(trimmed->durationSeconds == playlist.beatToSeconds(2.0));
        assert(trimmed->sourceOffsetSeconds == 2.0 * secondsPerBeat);
    }

    // Right-edge trim: end 8 -> 6. Visible end only; source offset untouched.
    {
        ClipInstance* fresh = playlist.getClip(clipId);
        fresh->startBeat = 4.0;
        fresh->durationBeats = 4.0;
        fresh->durationSeconds = playlist.beatToSeconds(4.0);
        fresh->sourceOffsetSeconds = 0.0;

        TrimClipCommand cmd(playlist, clipId, 4.0, 8.0, 0.0, playlist.beatToSeconds(4.0), 4.0, 6.0);
        cmd.execute();

        ClipInstance* trimmed = playlist.getClip(clipId);
        assert(trimmed->startBeat == 4.0);
        assert(trimmed->durationBeats == 2.0);
        assert(trimmed->durationSeconds == playlist.beatToSeconds(2.0));
        assert(trimmed->sourceOffsetSeconds == 0.0);
    }

    // Left-edge trim under varispeed: the source advance scales with the
    // playback rate so the right edge stays pinned.
    {
        ClipInstance* fresh = playlist.getClip(clipId);
        fresh->startBeat = 4.0;
        fresh->durationBeats = 4.0;
        fresh->durationSeconds = playlist.beatToSeconds(4.0);
        fresh->sourceOffsetSeconds = 0.0;
        fresh->edits.playbackRate = 1.0f;

        TrimClipCommand cmd(playlist, clipId, 4.0, 8.0, 0.0, playlist.beatToSeconds(4.0), 5.0, 8.0);
        cmd.execute();

        ClipInstance* trimmed = playlist.getClip(clipId);
        assert(trimmed->sourceOffsetSeconds == 1.0 * secondsPerBeat); // rate 1.0

        trimmed->edits.playbackRate = 2.0f;
        TrimClipCommand fastCmd(playlist, clipId, 4.0, 8.0, 0.0, playlist.beatToSeconds(4.0), 5.0, 8.0);
        fastCmd.execute();
        assert(trimmed->sourceOffsetSeconds == 2.0 * secondsPerBeat); // scaled by rate
    }

    // Left-edge extension must never write a negative source offset: dragging
    // earlier than the source start clamps to the source-start beat instead.
    {
        ClipInstance* fresh = playlist.getClip(clipId);
        fresh->startBeat = 4.0;
        fresh->durationBeats = 4.0;
        fresh->durationSeconds = playlist.beatToSeconds(4.0);
        fresh->sourceOffsetSeconds = 0.0;
        fresh->edits.playbackRate = 1.0f;

        // Source starts at beat 4.0; an extension to beat 2.0 is illegal.
        TrimClipCommand cmd(playlist, clipId, 4.0, 8.0, 0.0, playlist.beatToSeconds(4.0), 2.0, 8.0);
        cmd.execute();

        ClipInstance* trimmed = playlist.getClip(clipId);
        assert(trimmed->startBeat == 4.0);
        assert(trimmed->durationBeats == 4.0);
        assert(trimmed->durationSeconds == playlist.beatToSeconds(4.0));
        assert(trimmed->sourceOffsetSeconds == 0.0);
    }

    // The clamp respects an existing source offset: a clip already trimmed in
    // by one beat can extend back to its source start, but no further.
    {
        ClipInstance* fresh = playlist.getClip(clipId);
        fresh->startBeat = 4.0;
        fresh->durationBeats = 4.0;
        fresh->durationSeconds = playlist.beatToSeconds(4.0);
        fresh->sourceOffsetSeconds = 1.0 * secondsPerBeat; // source start at beat 3.0
        fresh->edits.playbackRate = 1.0f;

        // Extend to beat 3.0: legal, source offset falls to zero.
        TrimClipCommand cmd(playlist, clipId, 4.0, 8.0, 1.0 * secondsPerBeat,
                            playlist.beatToSeconds(4.0), 3.0, 8.0);
        cmd.execute();
        ClipInstance* trimmed = playlist.getClip(clipId);
        assert(trimmed->startBeat == 3.0);
        assert(trimmed->sourceOffsetSeconds == 0.0);

        // Extend past the source start to beat 2.0: clamped to 3.0, offset 0.
        TrimClipCommand past(playlist, clipId, 4.0, 8.0, 1.0 * secondsPerBeat,
                             playlist.beatToSeconds(4.0), 2.0, 8.0);
        past.execute();
        assert(trimmed->startBeat == 3.0);
        assert(trimmed->sourceOffsetSeconds == 0.0);
    }

    std::cout << "✅ PASS\n";
}

int main() {
    std::cout << "\n=== Aestra Clip Commands Test ===\n";

    const std::string renderDir = std::filesystem::temp_directory_path().string() + "/aestra-clipcmd-trim";

    testTrimClipCommand();
    testTrimClipCommandAudioContract(renderDir);
    testDuplicateClipCommand();

    testRemoveClipCommand();
    testRemoveClipCommandDoubleExecuteNoOp();
    testRemoveClipCommandUndoBeforeExecuteNoOp();
    testRemoveClipCommandInvalidId();
    testRemoveClipCommandMetadata();
    testRemoveClipCommandPreservesClipData();

    std::cout << "\nAll clip commands tests passed.\n";
    return 0;
}