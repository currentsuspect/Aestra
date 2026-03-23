// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Clip Commands Unit Tests
// Tests: TrimClipCommand, DuplicateClipCommand, RemoveClipCommand

#include "Commands/TrimClipCommand.h"
#include "Commands/DuplicateClipCommand.h"
#include "Commands/RemoveClipCommand.h"
#include "Commands/AddClipCommand.h"
#include "Models/ClipInstance.h"
#include "Models/PlaylistModel.h"

#include <cassert>
#include <iostream>

using namespace Aestra::Audio;

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
    assert(trimmed->durationBeats == 4.0);  // 10 - 6 = 4

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

int main() {
    std::cout << "\n=== Aestra Clip Commands Test ===\n";

    testTrimClipCommand();
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