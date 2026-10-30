// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RecordingTakeLengthTest — "never lose a session": a take keeps every second
// the performer played, with the app's default settings.
//
// Regression: the capture buffer was a 15-second ring that overwrote its
// oldest audio, so a 40-second take came out as its last 15 seconds, placed
// 25 seconds late. Nothing warned the user.

#include "../Support/TestTempDirectory.h"
#include "AestraJSON.h"
#include "Core/MixerChannel.h"
#include "Models/RecordingTakeStream.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
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
    tm->setInputChannelCount(2);
    // Deliberately no setMaxRecordingSeconds(): this test is about the app's
    // defaults, and nothing in Source/ sets a limit.
    return tm;
}

/** Feed @p seconds of a sine at audio-block size. Every half second of audio
 *  the take stream is drained, standing in for the app's 50 ms drain thread
 *  (the test runs far faster than real time, so the thread alone could not
 *  keep up with it). */
void feedInput(TrackManager& tm, double seconds) {
    const uint32_t frames = static_cast<uint32_t>(seconds * kSampleRate);
    const uint32_t channels = std::max(1u, static_cast<uint32_t>(tm.getInputChannelCount()));
    std::vector<float> input(static_cast<size_t>(frames) * channels);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = 0.25f * std::sin(2.0 * 3.14159265 * 440.0 * static_cast<double>(i / channels) / kSampleRate);
    }
    constexpr uint32_t kBlock = 512;
    constexpr size_t kDrainEvery = kSampleRate / 2;
    size_t sinceDrain = 0;
    for (size_t off = 0; off < frames; off += kBlock) {
        const size_t n = std::min<size_t>(kBlock, frames - off);
        tm.processInput(input.data() + off * channels, static_cast<uint32_t>(n));
        sinceDrain += n;
        if (sinceDrain >= kDrainEvery) {
            tm.drainRecordingCaptures();
            sinceDrain = 0;
        }
    }
}

/** Create a lane, a channel, a Track owning the lane and routing to the
 *  channel, and arm the Track (FD-14: the Track's arm is authoritative).
 *  Mirrors the app: a track is born with one lane. */
uint64_t armTrack(TrackManager& tm, const std::string& channelName, int inputIndex) {
    const PlaylistLaneID laneId = tm.getPlaylistModel().createLane(channelName);
    MixerChannel* ch = tm.addChannel(channelName);
    if (!ch) {
        return 0;
    }
    ch->setInputChannelIndex(inputIndex);
    const uint64_t trackId = tm.createTrack(laneId, channelName, ch->getChannelId());
    if (trackId != 0) {
        tm.setTrackArmed(trackId, true);
    }
    return trackId;
}

/** The lane holding a recorded take clip, or an invalid id. */
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

void startTake(TrackManager& tm) {
    tm.onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
}

void stopTake(TrackManager& tm, double seconds) {
    tm.onTransportStateApplied(false, static_cast<uint64_t>(seconds * kSampleRate), static_cast<double>(kSampleRate));
}

void recordTake(TrackManager& tm, double seconds) {
    startTake(tm);
    feedInput(tm, seconds);
    stopTake(tm, seconds);
}

std::vector<std::filesystem::path> filesWithExtension(const std::filesystem::path& dir, const std::string& ext) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (e.is_regular_file() && e.path().extension() == ext) {
            out.push_back(e.path());
        }
    }
    return out;
}

size_t laneCount(TrackManager& tm) {
    return tm.getPlaylistModel().getLaneIDs().size();
}

void testLongTakeKeepsEverySecond() {
    std::cout << "[1] a 40 s take keeps all 40 s at its true start\n";
    auto tm = makeRecorder();
    const uint64_t trackId = armTrack(*tm, "Vocal", 0);
    check(trackId != 0, "track created and armed");
    if (trackId == 0) {
        return;
    }
    constexpr double kTakeSeconds = 40.0;
    tm->record();
    recordTake(*tm, kTakeSeconds);

    auto* lane = tm->getPlaylistModel().getLane(takeLaneOf(*tm, trackId));
    check(lane != nullptr && !lane->clips.empty(), "the take landed on a lane");
    if (!lane || lane->clips.empty()) {
        return;
    }
    const auto& clip = lane->clips.front();
    const double clipSeconds = clip.durationBeats * 60.0 / kBpm;
    check(std::abs(clipSeconds - kTakeSeconds) < 0.05, "clip is 40 s long (got " + std::to_string(clipSeconds) + " s)");
    check(clip.startBeat < 0.05,
          "clip starts where the take started (got beat " + std::to_string(clip.startBeat) + ")");

    auto* pattern = tm->getPatternManager().getPattern(clip.patternId);
    const auto& payload = std::get<AudioSlicePayload>(pattern->payload);
    const auto* source = tm->getSourceManager().getSource(payload.audioSourceId);
    check(source != nullptr, "take has a source file");
    if (source) {
        const uint64_t expectedFrames = static_cast<uint64_t>(kTakeSeconds * kSampleRate);
        check(source->getNumFrames() == expectedFrames, "take file holds every frame (" +
                                                            std::to_string(source->getNumFrames()) + " of " +
                                                            std::to_string(expectedFrames) + ")");
    }
}

void testFinishedTakeLeavesNoPartialFile(const std::filesystem::path& home) {
    std::cout << "[2] a finished take leaves a .wav and no .partial behind\n";
    auto tm = makeRecorder();
    armTrack(*tm, "Vocal", 0);
    const size_t partialsBefore = filesWithExtension(home, RecordingTakeStream::kPartialExtension).size();
    tm->record();
    recordTake(*tm, 3.0);
    check(filesWithExtension(home, RecordingTakeStream::kPartialExtension).size() == partialsBefore,
          "no .partial file remains after stop");
}

void testCrashMidTakeKeepsAudioOnDisk(const std::filesystem::path& home) {
    std::cout << "[3] a take that never stops (crash) is on disk up to the last drain\n";
    for (const auto& p : filesWithExtension(home, RecordingTakeStream::kPartialExtension)) {
        std::filesystem::remove(p);
    }
    constexpr double kSeconds = 20.0;
    {
        auto tm = makeRecorder();
        armTrack(*tm, "Vocal", 0);
        tm->record();
        startTake(*tm);
        feedInput(*tm, kSeconds);
        tm->drainRecordingCaptures();
        // Session boundary while recording: cleanup must never touch the take.
        tm->cleanupOrphanedRecordings();
        // No stop: the TrackManager simply goes away, as in a crash.
    }
    {
        // Relaunch: the next session's cleanup runs before anyone recovers the take.
        auto relaunched = makeRecorder();
        relaunched->cleanupOrphanedRecordings();
    }
    const auto partials = filesWithExtension(home, RecordingTakeStream::kPartialExtension);
    check(partials.size() == 1, "exactly one .partial take is on disk (got " + std::to_string(partials.size()) + ")");
    if (partials.size() != 1) {
        return;
    }
    const int64_t frames = RecordingTakeStream::readPartialFrameCount(partials.front().string());
    const auto expected = static_cast<int64_t>(kSeconds * kSampleRate);
    check(frames == expected,
          "its header says all 20 s (" + std::to_string(frames) + " of " + std::to_string(expected) + " frames)");
    const auto bytes = std::filesystem::file_size(partials.front());
    check(bytes == 44u + static_cast<uintmax_t>(expected) * sizeof(float), "the file holds every frame's bytes");
}

} // namespace

int main() {
    // Recordings with no project path go under $HOME; keep them in a temp dir.
    Aestra::Tests::ScopedTempDirectory home("recording-take-length");
    setenv("HOME", home.path().string().c_str(), 1);

    testLongTakeKeepsEverySecond();
    testFinishedTakeLeavesNoPartialFile(home.path());
    testCrashMidTakeKeepsAudioOnDisk(home.path());

    if (g_failures > 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "All recording take-length checks passed\n";
    return 0;
}
