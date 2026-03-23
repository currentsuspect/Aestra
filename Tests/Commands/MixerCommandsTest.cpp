// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Mixer Commands Unit Tests
// Tests: SetVolumeCommand, SetPanCommand, SetMuteCommand, SetSoloCommand

#include "Commands/SetVolumeCommand.h"
#include "Commands/SetPanCommand.h"
#include "Commands/SetMuteCommand.h"
#include "Commands/SetSoloCommand.h"
#include "Commands/CommandHistory.h"
#include "Core/MixerChannel.h"

#include <cassert>
#include <iostream>
#include <cmath>

using namespace Aestra::Audio;

bool approxEqual(float a, float b, float eps = 0.001f) {
    return std::abs(a - b) < eps;
}

// =============================================================================
// SetVolumeCommand tests
// =============================================================================

void testSetVolumeCommand() {
    std::cout << "TEST: SetVolumeCommand... ";

    MixerChannel channel("Test Channel", 0);
    channel.setVolume(0.5f);

    SetVolumeCommand cmd(channel, 0.8f);
    cmd.execute();

    assert(approxEqual(channel.getVolume(), 0.8f));
    assert(cmd.getName() == "Set Volume");

    cmd.undo();
    assert(approxEqual(channel.getVolume(), 0.5f));

    cmd.redo();
    assert(approxEqual(channel.getVolume(), 0.8f));

    std::cout << "✅ PASS\n";
}

void testSetVolumeDoubleExecuteNoOp() {
    std::cout << "TEST: SetVolumeCommand double execute no-op... ";

    MixerChannel channel("Test Channel", 0);
    channel.setVolume(0.3f);

    SetVolumeCommand cmd(channel, 0.9f);
    cmd.execute();
    assert(approxEqual(channel.getVolume(), 0.9f));

    // Second execute should be no-op (idempotent)
    cmd.execute();
    assert(approxEqual(channel.getVolume(), 0.9f));

    // Undo should still restore original
    cmd.undo();
    assert(approxEqual(channel.getVolume(), 0.3f));

    std::cout << "✅ PASS\n";
}

void testSetVolumeUndoBeforeExecuteNoOp() {
    std::cout << "TEST: SetVolumeCommand undo before execute no-op... ";

    MixerChannel channel("Test Channel", 0);
    channel.setVolume(0.5f);

    SetVolumeCommand cmd(channel, 0.8f);
    // Undo before execute should be no-op
    cmd.undo();

    // Volume should be unchanged
    assert(approxEqual(channel.getVolume(), 0.5f));

    std::cout << "✅ PASS\n";
}

void testSetVolumeCommandMetadata() {
    std::cout << "TEST: SetVolumeCommand metadata... ";

    MixerChannel channel("Test Channel", 0);
    SetVolumeCommand cmd(channel, 0.7f);

    assert(cmd.changesProjectState() == true);
    assert(cmd.getSizeInBytes() > 0);

    std::cout << "✅ PASS\n";
}

// =============================================================================
// SetPanCommand tests
// =============================================================================

void testSetPanCommand() {
    std::cout << "TEST: SetPanCommand... ";

    MixerChannel channel("Test Channel", 0);
    channel.setPan(0.0f);  // Center

    SetPanCommand cmd(channel, -0.5f);  // Pan left
    cmd.execute();

    assert(approxEqual(channel.getPan(), -0.5f));
    assert(cmd.getName() == "Set Pan");

    cmd.undo();
    assert(approxEqual(channel.getPan(), 0.0f));

    cmd.redo();
    assert(approxEqual(channel.getPan(), -0.5f));

    std::cout << "✅ PASS\n";
}

void testSetPanBoundaryValues() {
    std::cout << "TEST: SetPanCommand boundary values... ";

    MixerChannel channel("Test Channel", 0);

    // Full left
    channel.setPan(0.0f);
    SetPanCommand leftCmd(channel, -1.0f);
    leftCmd.execute();
    assert(approxEqual(channel.getPan(), -1.0f));
    leftCmd.undo();
    assert(approxEqual(channel.getPan(), 0.0f));

    // Full right
    SetPanCommand rightCmd(channel, 1.0f);
    rightCmd.execute();
    assert(approxEqual(channel.getPan(), 1.0f));
    rightCmd.undo();
    assert(approxEqual(channel.getPan(), 0.0f));

    std::cout << "✅ PASS\n";
}

void testSetPanDoubleExecuteNoOp() {
    std::cout << "TEST: SetPanCommand double execute no-op... ";

    MixerChannel channel("Test Channel", 0);
    channel.setPan(0.0f);

    SetPanCommand cmd(channel, 0.75f);
    cmd.execute();
    assert(approxEqual(channel.getPan(), 0.75f));

    cmd.execute(); // no-op
    assert(approxEqual(channel.getPan(), 0.75f));

    cmd.undo();
    assert(approxEqual(channel.getPan(), 0.0f));

    std::cout << "✅ PASS\n";
}

void testSetPanUndoBeforeExecuteNoOp() {
    std::cout << "TEST: SetPanCommand undo before execute no-op... ";

    MixerChannel channel("Test Channel", 0);
    channel.setPan(0.3f);

    SetPanCommand cmd(channel, -0.8f);
    cmd.undo(); // Before execute - no-op

    assert(approxEqual(channel.getPan(), 0.3f));

    std::cout << "✅ PASS\n";
}

// =============================================================================
// SetMuteCommand tests
// =============================================================================

void testSetMuteCommand() {
    std::cout << "TEST: SetMuteCommand... ";

    MixerChannel channel("Test Channel", 0);
    channel.setMute(false);

    SetMuteCommand cmd(channel, true);
    cmd.execute();

    assert(channel.isMuted() == true);
    assert(cmd.getName() == "Mute");

    cmd.undo();
    assert(channel.isMuted() == false);

    SetMuteCommand unmuteCmd(channel, false);
    unmuteCmd.execute();
    assert(unmuteCmd.getName() == "Unmute");

    std::cout << "✅ PASS\n";
}

void testSetMuteDoubleExecuteNoOp() {
    std::cout << "TEST: SetMuteCommand double execute no-op... ";

    MixerChannel channel("Test Channel", 0);
    channel.setMute(false);

    SetMuteCommand cmd(channel, true);
    cmd.execute();
    assert(channel.isMuted() == true);

    cmd.execute(); // no-op
    assert(channel.isMuted() == true);

    cmd.undo();
    assert(channel.isMuted() == false);

    std::cout << "✅ PASS\n";
}

void testSetMuteUndoBeforeExecuteNoOp() {
    std::cout << "TEST: SetMuteCommand undo before execute no-op... ";

    MixerChannel channel("Test Channel", 0);
    channel.setMute(true);

    SetMuteCommand cmd(channel, false);
    cmd.undo(); // Before execute - no-op

    assert(channel.isMuted() == true); // Still muted

    std::cout << "✅ PASS\n";
}

void testSetMuteRedoCycle() {
    std::cout << "TEST: SetMuteCommand redo cycle... ";

    MixerChannel channel("Test Channel", 0);
    channel.setMute(false);

    SetMuteCommand cmd(channel, true);
    cmd.execute();
    assert(channel.isMuted() == true);

    cmd.undo();
    assert(channel.isMuted() == false);

    cmd.redo();
    assert(channel.isMuted() == true);

    std::cout << "✅ PASS\n";
}

// =============================================================================
// SetSoloCommand tests
// =============================================================================

void testSetSoloCommand() {
    std::cout << "TEST: SetSoloCommand... ";

    MixerChannel channel("Test Channel", 0);
    channel.setSolo(false);

    SetSoloCommand cmd(channel, true);
    cmd.execute();

    assert(channel.isSoloed() == true);
    assert(cmd.getName() == "Solo");

    cmd.undo();
    assert(channel.isSoloed() == false);

    SetSoloCommand unsoloCmd(channel, false);
    unsoloCmd.execute();
    assert(unsoloCmd.getName() == "Unsolo");

    std::cout << "✅ PASS\n";
}

void testSetSoloDoubleExecuteNoOp() {
    std::cout << "TEST: SetSoloCommand double execute no-op... ";

    MixerChannel channel("Test Channel", 0);
    channel.setSolo(false);

    SetSoloCommand cmd(channel, true);
    cmd.execute();
    assert(channel.isSoloed() == true);

    cmd.execute(); // no-op
    assert(channel.isSoloed() == true);

    cmd.undo();
    assert(channel.isSoloed() == false);

    std::cout << "✅ PASS\n";
}

void testSetSoloUndoBeforeExecuteNoOp() {
    std::cout << "TEST: SetSoloCommand undo before execute no-op... ";

    MixerChannel channel("Test Channel", 0);
    channel.setSolo(true);

    SetSoloCommand cmd(channel, false);
    cmd.undo(); // Before execute - no-op

    assert(channel.isSoloed() == true); // Still soloed

    std::cout << "✅ PASS\n";
}

void testSetSoloRedoCycle() {
    std::cout << "TEST: SetSoloCommand redo cycle... ";

    MixerChannel channel("Test Channel", 0);
    channel.setSolo(false);

    SetSoloCommand cmd(channel, true);
    cmd.execute();
    assert(channel.isSoloed() == true);

    cmd.undo();
    assert(channel.isSoloed() == false);

    cmd.redo();
    assert(channel.isSoloed() == true);

    std::cout << "✅ PASS\n";
}

// =============================================================================
// Integration test: mixer commands with CommandHistory
// =============================================================================

void testMixerCommandsWithHistory() {
    std::cout << "TEST: Mixer commands with CommandHistory integration... ";

    MixerChannel channel("Test Channel", 0);
    channel.setVolume(0.5f);
    channel.setPan(0.0f);
    channel.setMute(false);
    channel.setSolo(false);

    CommandHistory history;

    // Push all four command types
    history.pushAndExecute(std::make_shared<SetVolumeCommand>(channel, 0.8f));
    history.pushAndExecute(std::make_shared<SetPanCommand>(channel, -0.3f));
    history.pushAndExecute(std::make_shared<SetMuteCommand>(channel, true));
    history.pushAndExecute(std::make_shared<SetSoloCommand>(channel, true));

    assert(approxEqual(channel.getVolume(), 0.8f));
    assert(approxEqual(channel.getPan(), -0.3f));
    assert(channel.isMuted() == true);
    assert(channel.isSoloed() == true);
    assert(history.canUndo() == true);

    // Undo all four
    history.undo();
    assert(channel.isSoloed() == false);
    history.undo();
    assert(channel.isMuted() == false);
    history.undo();
    assert(approxEqual(channel.getPan(), 0.0f));
    history.undo();
    assert(approxEqual(channel.getVolume(), 0.5f));

    assert(history.canUndo() == false);

    // Redo all four
    history.redo();
    assert(approxEqual(channel.getVolume(), 0.8f));
    history.redo();
    assert(approxEqual(channel.getPan(), -0.3f));
    history.redo();
    assert(channel.isMuted() == true);
    history.redo();
    assert(channel.isSoloed() == true);

    std::cout << "✅ PASS\n";
}

int main() {
    std::cout << "\n=== Aestra Mixer Commands Test ===\n";

    testSetVolumeCommand();
    testSetVolumeDoubleExecuteNoOp();
    testSetVolumeUndoBeforeExecuteNoOp();
    testSetVolumeCommandMetadata();

    testSetPanCommand();
    testSetPanBoundaryValues();
    testSetPanDoubleExecuteNoOp();
    testSetPanUndoBeforeExecuteNoOp();

    testSetMuteCommand();
    testSetMuteDoubleExecuteNoOp();
    testSetMuteUndoBeforeExecuteNoOp();
    testSetMuteRedoCycle();

    testSetSoloCommand();
    testSetSoloDoubleExecuteNoOp();
    testSetSoloUndoBeforeExecuteNoOp();
    testSetSoloRedoCycle();

    testMixerCommandsWithHistory();

    std::cout << "\nAll mixer commands tests passed.\n";
    return 0;
}