// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RecordingPathTest — Validates the recording state machine, arming, and session lifecycle.
// Note: Full capture path (processInput → WAV) requires AudioDeviceManager input channels
// which are hardware-dependent. This test covers the public recording API state machine.

#include "Models/TrackManager.h"
#include "Core/MixerChannel.h"
#include "IO/MiniAudioDecoder.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr double kBpm = 120.0;

// Test 1: Track arming / disarming
bool testTrackArming() {
    std::cout << "  [1/3] Track arming/disarming... ";
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);

    tm->addChannel("Test Track");
    MixerChannel* ch = tm->getChannel(0);
    if (!ch) { std::cerr << "FAILED: channel null\n"; return false; }

    // Arm/disarm via channel
    ch->setArmed(true);
    if (!ch->isArmed()) { std::cerr << "FAILED: not armed\n"; return false; }
    ch->setArmed(false);
    if (ch->isArmed()) { std::cerr << "FAILED: still armed\n"; return false; }

    // Arm/disarm via TrackManager record() toggle
    tm->record();
    if (!tm->isRecordArmed()) { std::cerr << "FAILED: TM not armed\n"; return false; }
    tm->record();
    if (tm->isRecordArmed()) { std::cerr << "FAILED: TM still armed\n"; return false; }

    std::cout << "PASSED\n";
    return true;
}

// Test 2: Recording session lifecycle (transport → begin/finalize capture)
bool testRecordingSessionLifecycle() {
    std::cout << "  [2/3] Recording session lifecycle... ";
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);
    tm->setMaxRecordingSeconds(5.0);

    tm->addChannel("Armed Track");
    MixerChannel* ch = tm->getChannel(0);
    ch->setArmed(true);

    // Record arm
    tm->record();
    if (!tm->isRecordArmed()) { std::cerr << "FAILED: not armed\n"; return false; }

    // Transport starts playing → begins capture session
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    if (!tm->isRecording()) {
        // isRecording checks m_isCapturing which should be set by beginCaptureSession
        std::cerr << "FAILED: not recording after transport play\n";
        return false;
    }

    // Transport stops → finalizes capture session (position 2.0s)
    tm->onTransportStateApplied(false, static_cast<uint64_t>(2.0 * kSampleRate),
                                static_cast<double>(kSampleRate));
    if (tm->isRecording()) {
        std::cerr << "FAILED: still recording after transport stop\n";
        return false;
    }

    std::cout << "PASSED\n";
    return true;
}

// Test 3: Unarmed tracks don't start capture session
bool testUnarmedNoCapture() {
    std::cout << "  [3/3] Unarmed track → no capture session... ";
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);

    tm->addChannel("Unarmed Track");
    MixerChannel* ch = tm->getChannel(0);
    ch->setArmed(false);

    // Even if we toggle record arm and start transport, no armed tracks = no capture
    tm->record();
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));

    // Since no tracks are armed, isRecording should be false
    if (tm->isRecording()) {
        std::cerr << "FAILED: recording started with no armed tracks\n";
        return false;
    }

    tm->onTransportStateApplied(false, static_cast<uint64_t>(1.0 * kSampleRate),
                                static_cast<double>(kSampleRate));
    tm->record(); // disarm

    std::cout << "PASSED\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=== Recording Path Tests ===\n";
    std::cout << "(State machine coverage — full capture path requires audio hardware)\n\n";

    bool allPassed = true;
    allPassed &= testTrackArming();
    allPassed &= testRecordingSessionLifecycle();
    allPassed &= testUnarmedNoCapture();

    std::cout << "\n";
    if (allPassed) {
        std::cout << "All recording path tests passed.\n";
        return 0;
    } else {
        std::cout << "FAILED: one or more recording path tests failed.\n";
        return 1;
    }
}
