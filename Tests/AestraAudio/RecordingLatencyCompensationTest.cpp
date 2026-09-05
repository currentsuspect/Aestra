// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// T-6 (v0.7.1 trust sprint): the record path placed takes with no latency
// compensation and timestamped capture from the UI-cached transport position.
// Two gaps, both pinned here headlessly:
//
// 1. Device latency: AudioDeviceManager::getLatencyCompensationValues had zero
//    consumers, so takes landed late by input+output latency. Now wired
//    through TrackManager::setRecordLatencyCompensationMs into take placement
//    (shift earlier, samples intact, clamped at zero).
// 2. Placement jitter: capture.startBeat came from getCurrentTransportBeat,
//    which lags the audio thread by buffers. processInput now accepts the
//    engine's authoritative frame; the UI position is only the fallback.

#include "Models/PatternManager.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kBpm = 120.0;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kBlock = 512;

int failures = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "PASS: " << what << "\n";
    } else {
        std::cout << "FAIL: " << what << "\n";
        ++failures;
    }
}

bool nearEqual(double a, double b, double tol = 1e-6) {
    return std::fabs(a - b) <= tol;
}

std::shared_ptr<TrackManager> makeRecorder() {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(kSampleRate);
    tm->setInputSampleRate(kSampleRate);
    tm->getPlaylistModel().setBPM(kBpm);
    tm->setInputChannelCount(static_cast<int>(kChannels));
    tm->setMaxRecordingSeconds(10.0);
    return tm;
}

uint64_t armTrack(TrackManager& tm) {
    const PlaylistLaneID laneId = tm.getPlaylistModel().createLane("Guitar");
    MixerChannel* ch = tm.addChannel("Guitar");
    if (!ch) {
        return 0;
    }
    ch->setInputChannelIndex(0);
    const uint64_t trackId = tm.createTrack(laneId, "Guitar", ch->getChannelId());
    if (trackId != 0) {
        tm.setTrackArmed(trackId, true);
    }
    return trackId;
}

void feedInput(TrackManager& tm, double seconds, uint64_t firstFrame, bool passFrame) {
    const uint32_t frames = static_cast<uint32_t>(seconds * kSampleRate);
    std::vector<float> input(static_cast<size_t>(frames) * kChannels, 0.25f);
    for (size_t off = 0; off < frames; off += kBlock) {
        const size_t n = std::min<size_t>(kBlock, frames - off);
        if (passFrame) {
            tm.processInput(input.data() + off * kChannels, static_cast<uint32_t>(n), nullptr,
                            firstFrame + off);
        } else {
            tm.processInput(input.data() + off * kChannels, static_cast<uint32_t>(n));
        }
    }
}

const ClipInstance* takeClipOf(TrackManager& tm, uint64_t trackId) {
    auto* track = tm.getTrack(trackId);
    if (!track) {
        return nullptr;
    }
    for (const auto& laneId : track->laneIds) {
        auto* lane = tm.getPlaylistModel().getLane(laneId);
        if (lane && !lane->clips.empty()) {
            return &lane->clips[0];
        }
    }
    return nullptr;
}

// Full take cycle at a fixed transport position: arm, capture, finalize.
void recordTakeAt(TrackManager& tm, double positionSeconds, double captureSeconds, uint64_t firstFrame,
                  bool passFrame) {
    tm.setPosition(positionSeconds);
    tm.onTransportStateApplied(true, static_cast<uint64_t>(positionSeconds * kSampleRate), kSampleRate);
    feedInput(tm, captureSeconds, firstFrame, passFrame);
    tm.onTransportStateApplied(false, static_cast<uint64_t>((positionSeconds + captureSeconds) * kSampleRate),
                               kSampleRate);
}

} // namespace

int main() {
    // --- 1. Device compensation shifts placement earlier, samples intact. ---
    // Provider stands in for AudioDeviceManager::getLatencyCompensationValues.
    {
        auto tm = makeRecorder();
        const uint64_t trackId = armTrack(*tm);
        if (trackId == 0) {
            std::cerr << "FAIL: fixture setup\n";
            return 1;
        }
        double providerInMs = 10.0, providerOutMs = 10.0; // 20 ms @120 BPM = 0.04 beats
        tm->setRecordLatencyProvider([&](double& inMs, double& outMs) {
            inMs = providerInMs;
            outMs = providerOutMs;
        });
        tm->record();
        recordTakeAt(*tm, 2.0, 1.0, 0, false);
        tm->record();

        const ClipInstance* take = takeClipOf(*tm, trackId);
        check(take != nullptr, "T-6: compensated take commits a clip");
        if (take) {
            check(nearEqual(take->startBeat, 4.0 - 0.04, 1e-4),
                  "T-6: take placed earlier by input+output latency (4.0 -> 3.96 beats)");
            check(nearEqual(take->durationBeats, 2.0, 1e-4),
                  "T-6: compensation moves placement, never trims samples (duration intact)");
        }

        // --- 1b. Reconfiguration between takes is reflected at commit. ---
        // The provider is queried live, so a buffer/device change from any
        // path (including settings-page calls that bypass stream start)
        // cannot leave stale values behind.
        providerInMs = 25.0;
        providerOutMs = 25.0; // 50 ms @120 BPM = 0.10 beats
        tm->record();
        recordTakeAt(*tm, 6.0, 1.0, 0, false);
        tm->record();

        const ClipInstance* take2 = nullptr;
        if (auto* track = tm->getTrack(trackId)) {
            for (const auto& laneId : track->laneIds) {
                auto* lane = tm->getPlaylistModel().getLane(laneId);
                if (lane) {
                    for (const auto& clip : lane->clips) {
                        if (nearEqual(clip.startBeat, 12.0 - 0.10, 1e-4)) {
                            take2 = &clip;
                        }
                    }
                }
            }
        }
        check(take2 != nullptr, "T-6: take after reconfig uses fresh values (12.0 -> 11.90 beats)");
    }

    // --- 2. Zero compensation (default) preserves legacy placement. ---
    {
        auto tm = makeRecorder();
        const uint64_t trackId = armTrack(*tm);
        if (trackId == 0) {
            std::cerr << "FAIL: fixture setup\n";
            return 1;
        }
        tm->record();
        recordTakeAt(*tm, 2.0, 1.0, 0, false);
        tm->record();

        const ClipInstance* take = takeClipOf(*tm, trackId);
        check(take != nullptr, "T-6: uncompensated take commits a clip");
        if (take) {
            check(nearEqual(take->startBeat, 4.0, 1e-4), "T-6: default zero compensation keeps legacy placement");
        }
    }

    // --- 3. Clamp at zero: song-top takes keep residual lateness, never negative. ---
    {
        auto tm = makeRecorder();
        const uint64_t trackId = armTrack(*tm);
        if (trackId == 0) {
            std::cerr << "FAIL: fixture setup\n";
            return 1;
        }
        tm->setRecordLatencyProvider([](double& inMs, double& outMs) {
            inMs = 50.0;
            outMs = 50.0;
        }); // 100 ms > capture start
        tm->record();
        recordTakeAt(*tm, 0.0, 1.0, 0, false);
        tm->record();

        const ClipInstance* take = takeClipOf(*tm, trackId);
        check(take != nullptr, "T-6: clamped take commits a clip");
        if (take) {
            check(nearEqual(take->startBeat, 0.0, 1e-9), "T-6: placement clamps at zero, never negative");
            check(take->durationBeats > 0.0, "T-6: clamped take keeps its audio");
        }
    }

    // --- 4. Engine frame beats UI-cached position for capture start. ---
    {
        auto tm = makeRecorder();
        const uint64_t trackId = armTrack(*tm);
        if (trackId == 0) {
            std::cerr << "FAIL: fixture setup\n";
            return 1;
        }
        tm->record();
        // Decoy: UI position claims 999 s, but the engine frame says 2.0 s.
        tm->setPosition(999.0);
        tm->onTransportStateApplied(true, 0, kSampleRate);
        feedInput(*tm, 1.0, static_cast<uint64_t>(2.0 * kSampleRate), true);
        tm->onTransportStateApplied(false, static_cast<uint64_t>(3.0 * kSampleRate), kSampleRate);
        tm->record();

        const ClipInstance* take = takeClipOf(*tm, trackId);
        check(take != nullptr, "T-6: frame-stamped take commits a clip");
        if (take) {
            check(nearEqual(take->startBeat, 4.0, 1e-4),
                  "T-6: capture start follows the engine frame, not the stale UI position");
        }
    }

    if (failures == 0) {
        std::cout << "record latency compensation passed\n";
    }
    return failures == 0 ? 0 : 1;
}
