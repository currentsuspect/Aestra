// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RecordingBaselineTest — Phase 2 of the Track/Lane/Channel migration (vault:
// "Track Lane Channel Ownership Contract").
//
// This file captures CURRENT behavior as a regression baseline. Two sections:
//
//   SECTION A — surviving invariants: behavior that MUST hold after the
//               migration (recorded audio routes to the armed channel; one
//               undoable transaction; dirty state; multi-arm independence;
//               one lane per take).
//
//   SECTION B — expected-to-change behavior: the current reality the
//               migration removes (channel-based recording identity; lanes
//               without ownership; flat ungrouped lanes; no Track entity).
//               These assertions are intentionally RED against the future
//               architecture — when the migration lands they are replaced by
//               the new acceptance, they are NOT "fixed" by weakening them.
//
// Nothing here may be changed to make the baseline pass. If a Section A
// assertion fails after the migration, the migration broke an invariant.

#include "Models/TrackManager.h"
#include "Core/MixerChannel.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr double kBpm = 120.0;

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    } else {
        std::cout << "  PASS: " << label << "\n";
    }
}

std::shared_ptr<TrackManager> makeRecorder() {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);
    tm->setInputChannelCount(1);
    tm->setMaxRecordingSeconds(5.0);
    return tm;
}

void feedInput(TrackManager& tm, double seconds) {
    const uint32_t frames = static_cast<uint32_t>(seconds * kSampleRate);
    std::vector<float> input(static_cast<size_t>(frames));
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = 0.25f * std::sin(2.0 * 3.14159265 * 440.0 * static_cast<double>(i) / kSampleRate);
    }
    constexpr uint32_t kBlock = 512;
    for (size_t off = 0; off < input.size(); off += kBlock) {
        const size_t n = std::min<size_t>(kBlock, input.size() - off);
        tm.processInput(input.data() + off, static_cast<uint32_t>(n));
    }
}

size_t laneCount(TrackManager& tm) {
    return tm.getPlaylistModel().getLaneIDs().size();
}

// ===========================================================================
// SECTION A — surviving invariants (must stay green after the migration)
// ===========================================================================

void testTakeRoutesToArmedChannel() {
    std::cout << "[A] take routes to the armed channel\n";
    auto tm = makeRecorder();
    MixerChannel* ch = tm->addChannel("Ch 5");
    ch->setArmed(true);
    ch->setInputChannelIndex(0);

    tm->record();
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    feedInput(*tm, 1.0);
    tm->onTransportStateApplied(false, static_cast<uint64_t>(1.0 * kSampleRate),
                                static_cast<double>(kSampleRate));

    const auto laneIds = tm->getPlaylistModel().getLaneIDs();
    check(laneIds.size() == 1, "one take creates one lane");
    if (laneIds.empty()) {
        return;
    }
    auto* lane = tm->getPlaylistModel().getLane(laneIds[0]);
    check(lane && !lane->clips.empty(), "take clip is on the lane");
    if (!lane || lane->clips.empty()) {
        return;
    }
    auto& patternManager = tm->getPatternManager();
    auto* pattern = patternManager.getPattern(lane->clips[0].patternId);
    check(pattern != nullptr, "take clip references a pattern");
    if (pattern) {
        check(pattern->getMixerChannelId() == ch->getChannelId(),
              "take pattern routes to the armed channel (id " + std::to_string(ch->getChannelId()) + ")");
    }
}

void testRecordingIsOneUndoableTransaction() {
    std::cout << "[A] recording is one undoable transaction\n";
    auto tm = makeRecorder();
    MixerChannel* ch = tm->addChannel("Ch 1");
    ch->setArmed(true);
    ch->setInputChannelIndex(0);

    tm->record();
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    feedInput(*tm, 1.0);
    tm->onTransportStateApplied(false, static_cast<uint64_t>(1.0 * kSampleRate),
                                static_cast<double>(kSampleRate));

    check(laneCount(*tm) == 1, "take committed before undo");
    check(tm->getCommandHistory().undo(), "undo of the take succeeds");
    check(laneCount(*tm) == 0, "undo removes the take's lane");
    check(tm->getCommandHistory().redo(), "redo restores the take");
    check(laneCount(*tm) == 1, "redo restores the take's lane");
}

void testDirtyStateUpdated() {
    std::cout << "[A] dirty state updated by recording\n";
    auto tm = makeRecorder();
    MixerChannel* ch = tm->addChannel("Ch 1");
    ch->setArmed(true);
    ch->setInputChannelIndex(0);

    tm->setModified(false);
    tm->record();
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    feedInput(*tm, 1.0);
    tm->onTransportStateApplied(false, static_cast<uint64_t>(1.0 * kSampleRate),
                                static_cast<double>(kSampleRate));

    check(tm->isModified(), "project is dirty after a recorded take");
}

void testMultipleArmedChannelsCaptureIndependently() {
    std::cout << "[A] multiple armed channels capture independently\n";
    auto tm = makeRecorder();
    tm->setInputChannelCount(2);
    MixerChannel* ch1 = tm->addChannel("Ch 1");
    ch1->setArmed(true);
    ch1->setInputChannelIndex(0);
    MixerChannel* ch2 = tm->addChannel("Ch 2");
    ch2->setArmed(true);
    ch2->setInputChannelIndex(1);

    tm->record();
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    feedInput(*tm, 1.0);
    tm->onTransportStateApplied(false, static_cast<uint64_t>(1.0 * kSampleRate),
                                static_cast<double>(kSampleRate));

    check(laneCount(*tm) == 2, "two armed channels produce two take lanes");
}

void testEachTakeCreatesALane() {
    std::cout << "[A] each take creates a lane\n";
    auto tm = makeRecorder();
    MixerChannel* ch = tm->addChannel("Ch 1");
    ch->setArmed(true);
    ch->setInputChannelIndex(0);

    // Arm ONCE: the app keeps record-armed across takes; each play/stop
    // pass captures and commits its own take.
    tm->record();
    for (int pass = 1; pass <= 2; ++pass) {
        tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
        feedInput(*tm, 1.0);
        tm->onTransportStateApplied(false, static_cast<uint64_t>(1.0 * kSampleRate),
                                    static_cast<double>(kSampleRate));
    }
    check(laneCount(*tm) == 2, "two recording passes create two lanes");
}

// ===========================================================================
// SECTION B — expected-to-change (RED against the future architecture)
// ===========================================================================

void testRecordingIdentityIsChannelBased() {
    std::cout << "[B] recording identity is channel-based (expected to change)\n";
    auto tm = makeRecorder();
    MixerChannel* ch = tm->addChannel("Ch 5");
    ch->setArmed(true);
    ch->setInputChannelIndex(0);

    // The arm state that drives recording lives on the CHANNEL.
    tm->record();
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    feedInput(*tm, 1.0);
    tm->onTransportStateApplied(false, static_cast<uint64_t>(1.0 * kSampleRate),
                                static_cast<double>(kSampleRate));

    const auto laneIds = tm->getPlaylistModel().getLaneIDs();
    check(laneIds.size() == 1, "take landed on a lane");
    if (laneIds.empty()) {
        return;
    }
    auto* lane = tm->getPlaylistModel().getLane(laneIds[0]);
    // Current reality: the lane's only tie to the armed channel is the
    // pattern's OWN routing — the lane itself carries no ownership. Post
    // migration, the lane must be reachable as the track's lane.
    check(lane != nullptr, "lane exists");
    if (!lane) {
        return;
    }
    check(lane->name.find("Ch 5") == std::string::npos,
          "lane name is the take name, not the armed channel/track (no ownership naming today)");
}

void testLanesHaveNoOwnership() {
    std::cout << "[B] lanes have no ownership (expected to change)\n";
    auto tm = makeRecorder();
    MixerChannel* ch = tm->addChannel("Ch 1");
    ch->setArmed(true);
    ch->setInputChannelIndex(0);

    tm->record();
    for (int pass = 1; pass <= 2; ++pass) {
        tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
        feedInput(*tm, 1.0);
        tm->onTransportStateApplied(false, static_cast<uint64_t>(1.0 * kSampleRate),
                                    static_cast<double>(kSampleRate));
    }

    const auto laneIds = tm->getPlaylistModel().getLaneIDs();
    check(laneIds.size() == 2, "two flat lanes after two passes");
    // Current reality: nothing groups these lanes — they are independent
    // playlist rows. Post migration, both must resolve to the SAME track.
}

void testNoTrackEntity() {
    std::cout << "[B] no Track entity exists (expected to change)\n";
    auto tm = makeRecorder();
    MixerChannel* ch = tm->addChannel("Ch 1");
    ch->setArmed(true);
    ch->setInputChannelIndex(0);

    // Current reality: record() is a GLOBAL toggle; there is no per-track
    // arm, no per-track capture, no track id anywhere in the recording path.
    // Post migration, arming and recording are track-scoped operations.
    tm->record();
    check(tm->isRecordArmed(), "record arm is a global transport toggle today");
    check(!tm->getCommandHistory().canUndo(), "arming alone creates no model state");
}

} // namespace

int main() {
    std::cout << "=== Recording Baseline (Phase 2) ===\n";
    std::cout << "Section A: surviving invariants\n";
    testTakeRoutesToArmedChannel();
    testRecordingIsOneUndoableTransaction();
    testDirtyStateUpdated();
    testMultipleArmedChannelsCaptureIndependently();
    testEachTakeCreatesALane();

    std::cout << "Section B: expected-to-change (RED against the future)\n";
    testRecordingIdentityIsChannelBased();
    testLanesHaveNoOwnership();
    testNoTrackEntity();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "Baseline: current behavior captured, all green.\n";
        return 0;
    }
    std::cerr << g_failures << " baseline check(s) failed — investigate before Phase 3.\n";
    return 1;
}
