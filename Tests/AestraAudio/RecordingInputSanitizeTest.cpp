// © 2026 Aestra Studios — All Rights Reserved.
// RecordingInputSanitizeTest — #855: hardware-input samples reach the capture
// rings with no finite-value check, so one driver-glitch NaN poisons the ring
// (live preview, peaks) and — via the DC-bias mean in the commit path — the
// entire committed take. processInput must substitute silence at append time.

#include "../Support/TestTempDirectory.h"
#include "Core/MixerChannel.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr double kBpm = 120.0;
constexpr double kSeconds = 0.5;
constexpr uint32_t kFrames = static_cast<uint32_t>(kSeconds * kSampleRate);

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    } else {
        std::cout << "PASS: " << label << "\n";
    }
}

bool allFinite(const std::vector<float>& samples) {
    for (float v : samples) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<TrackManager> makeRecorder() {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);
    tm->setInputChannelCount(2);
    tm->setMaxRecordingSeconds(5.0);
    return tm;
}

uint64_t armTrack(TrackManager& tm, const std::string& name, int inputIndex) {
    const PlaylistLaneID laneId = tm.getPlaylistModel().createLane(name);
    MixerChannel* ch = tm.addChannel(name);
    if (!ch) {
        return 0;
    }
    if (inputIndex != -2) {
        ch->setInputChannelIndex(inputIndex);
    }
    const uint64_t trackId = tm.createTrack(laneId, name, ch->getChannelId());
    if (trackId != 0) {
        tm.setTrackArmed(trackId, true);
    }
    return trackId;
}

PlaylistLaneID takeLaneOf(TrackManager& tm, uint64_t trackId) {
    auto* track = tm.getTrack(trackId);
    if (!track) {
        return {};
    }
    for (const auto& laneId : track->laneIds) {
        auto* lane = tm.getPlaylistModel().getLane(laneId);
        if (lane && !lane->clips.empty()) {
            return laneId;
        }
    }
    return {};
}

const AudioBufferData* takeBuffer(TrackManager& tm, uint64_t trackId) {
    const PlaylistLaneID takeLane = takeLaneOf(tm, trackId);
    if (!takeLane.isValid()) {
        return nullptr;
    }
    auto* lane = tm.getPlaylistModel().getLane(takeLane);
    if (!lane || lane->clips.empty()) {
        return nullptr;
    }
    const PatternSource* pattern = tm.getPatternManager().getPattern(lane->clips[0].patternId);
    if (!pattern) {
        return nullptr;
    }
    const auto* audio = std::get_if<AudioSlicePayload>(&pattern->payload);
    if (!audio) {
        return nullptr;
    }
    const ClipSource* source = tm.getSourceManager().getSource(audio->audioSourceId);
    if (!source) {
        return nullptr;
    }
    return source->getRawBuffer();
}

} // namespace

int main() {
    std::cout << "RecordingInputSanitizeTest (#855)\n";
    auto tm = makeRecorder();
    Aestra::Tests::ScopedTempDirectory dir{"RecInputSanitize"};
    tm->setRecordingProjectPath((dir.path() / "sanitize.aes").string());

    const uint64_t directTrack = armTrack(*tm, "Direct", 0);
    const uint64_t mixTrack = armTrack(*tm, "Mixdown", -2);
    check(directTrack != 0 && mixTrack != 0, "direct and mixdown tracks armed");
    if (directTrack == 0 || mixTrack == 0) {
        return 1;
    }

    tm->record();
    tm->onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));

    std::vector<float> input(static_cast<size_t>(kFrames) * 2);
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        const float s =
            0.25f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(frame) / kSampleRate);
        input[static_cast<size_t>(frame) * 2] = s;
        input[static_cast<size_t>(frame) * 2 + 1] = s;
    }
    for (uint32_t frame = 1000; frame < 1100; ++frame) {
        input[static_cast<size_t>(frame) * 2] = std::numeric_limits<float>::quiet_NaN();
    }
    input[static_cast<size_t>(2000) * 2 + 1] = std::numeric_limits<float>::infinity();
    input[static_cast<size_t>(3000) * 2] = -std::numeric_limits<float>::infinity();

    constexpr uint32_t kBlock = 512;
    for (size_t off = 0; off < kFrames; off += kBlock) {
        const size_t n = std::min<size_t>(kBlock, kFrames - off);
        tm->processInput(input.data() + off * 2, static_cast<uint32_t>(n));
    }

    for (const auto& [trackId, name] : {std::pair<uint64_t, const char*>{directTrack, "direct"},
                                        {mixTrack, "mixdown"}}) {
        std::vector<float> ring;
        double startBeat = 0.0;
        check(tm->getRecordingDataSnapshot(trackId, ring, startBeat), std::string(name) + " ring snapshot reads");
        check(ring.size() == kFrames, std::string(name) + " ring holds every frame (substitution, not drop)");
        check(allFinite(ring), std::string(name) + " ring contains zero non-finite samples");
    }

    std::vector<float> directRing;
    double ignored = 0.0;
    if (tm->getRecordingDataSnapshot(directTrack, directRing, ignored) && directRing.size() == kFrames) {
        check(directRing[1000] == 0.0f && directRing[1099] == 0.0f,
              "poisoned direct frames read back as silence");
        check(directRing[500] != 0.0f, "clean direct frames pass through untouched");
    } else {
        check(false, "direct ring snapshot available for sample assertions");
    }

    tm->onTransportStateApplied(false, kFrames, static_cast<double>(kSampleRate));

    for (const auto& [trackId, name] : {std::pair<uint64_t, const char*>{directTrack, "direct"},
                                        {mixTrack, "mixdown"}}) {
        const AudioBufferData* buffer = takeBuffer(*tm, trackId);
        check(buffer != nullptr, std::string(name) + " take committed with audio");
        if (!buffer) {
            continue;
        }
        check(buffer->numFrames == kFrames, std::string(name) + " take keeps every frame");
        check(allFinite(buffer->interleavedData), std::string(name) + " take contains zero non-finite samples");
    }

    if (g_failures == 0) {
        std::cout << "All input-sanitization checks passed\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
