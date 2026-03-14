// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Mixer Commands Unit Tests
// Tests: SetVolumeCommand, SetPanCommand, SetMuteCommand, SetSoloCommand

#include "Commands/SetVolumeCommand.h"
#include "Commands/SetPanCommand.h"
#include "Commands/SetMuteCommand.h"
#include "Commands/SetSoloCommand.h"
#include "Core/MixerChannel.h"

#include <cassert>
#include <iostream>
#include <cmath>

using namespace Aestra::Audio;

bool approxEqual(float a, float b, float eps = 0.001f) {
    return std::abs(a - b) < eps;
}

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

int main() {
    std::cout << "\n=== Aestra Mixer Commands Test ===\n";

    testSetVolumeCommand();
    testSetPanCommand();
    testSetMuteCommand();
    testSetSoloCommand();

    std::cout << "\nAll mixer commands tests passed.\n";
    return 0;
}
