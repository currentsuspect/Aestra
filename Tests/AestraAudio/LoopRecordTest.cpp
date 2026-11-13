// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// LoopRecordTest — Loop record: recording across the timeline loop gives one take per pass.
//
// The transport's Loop record key used to light up and do nothing (DAW Essentials Checklist, P0): a recording that
// ran over the loop came out as one long clip running past the loop end.

#include "../Support/TestTempDirectory.h"
#include "Core/MixerChannel.h"
#include "Models/LoopRecordPasses.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr double kBpm = 120.0; // 2 beats a second: an 8-beat loop is 4 s
int g_failures = 0;
std::filesystem::path g_projectDir;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    } else {
        std::cout << "  PASS: " << label << "\n";
    }
}

std::shared_ptr<TrackManager> makeRecorder(bool loopRecord) {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);
    tm->setInputChannelCount(1);
    tm->setRecordingProjectPath(g_projectDir.string());
    tm->setTransportLoopRegion(0.0, 8.0, true);
    tm->setLoopRecordEnabled(loopRecord);
    return tm;
}

uint64_t armTrack(TrackManager& tm) {
    const PlaylistLaneID laneId = tm.getPlaylistModel().createLane("Vocal");
    MixerChannel* ch = tm.addChannel("Vocal");
    if (!ch) return 0;
    ch->setInputChannelIndex(0);
    const uint64_t trackId = tm.createTrack(laneId, "Vocal", ch->getChannelId());
    if (trackId != 0) tm.setTrackArmed(trackId, true);
    return trackId;
}

void record(TrackManager& tm, double seconds) {
    tm.record();
    tm.onTransportStateApplied(true, 0, static_cast<double>(kSampleRate));
    const auto frames = static_cast<uint32_t>(seconds * kSampleRate);
    std::vector<float> input(frames, 0.25f);
    constexpr uint32_t kBlock = 512;
    for (uint32_t off = 0; off < frames; off += kBlock) {
        tm.processInput(input.data() + off, std::min(kBlock, frames - off));
        if (off % (kSampleRate / 2) < kBlock) tm.drainRecordingCaptures();
    }
    tm.onTransportStateApplied(false, frames, static_cast<double>(kSampleRate));
}

/** Every recorded clip on the track's lanes, as {startBeat, seconds}, sorted by length (longest first). */
std::vector<std::pair<double, double>> takes(TrackManager& tm, uint64_t trackId) {
    std::vector<std::pair<double, double>> out;
    if (auto* track = tm.getTrack(trackId)) {
        for (const auto& laneId : track->laneIds) {
            if (auto* lane = tm.getPlaylistModel().getLane(laneId)) {
                for (const auto& clip : lane->clips) out.emplace_back(clip.startBeat, clip.durationBeats * 60.0 / kBpm);
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

void testPassesAreCutAtTheLoopEnd() {
    std::cout << "[1] loopRecordPasses cuts at the loop end, then every loop length\n";
    const double fpb = 1000.0;
    auto p = loopRecordPasses(20000, 0.0, 0.0, 0.0, 0.0, 8.0, fpb, 250);
    check(p.size() == 3, "20 beats over an 8-beat loop = 3 passes");
    check(p.size() == 3 && p[0].lengthFrames == 8000 && p[1].offsetFrames == 8000 && p[2].lengthFrames == 4000,
          "8 + 8 + 4 beats");
    check(p.size() == 3 && p[1].startBeat == 0.0 && p[2].startBeat == 0.0, "later passes sit at the loop start");
    p = loopRecordPasses(20000, 1.5, 2.0, 0.5, 0.0, 8.0, fpb, 250);
    check(!p.empty() && p[0].lengthFrames == 6500, "first pass runs from the capture start to the loop end, plus latency");
    check(p.size() >= 2 && p[1].offsetFrames == 6500, "the second pass starts where the first ends");
    p = loopRecordPasses(16100, 0.0, 0.0, 0.0, 0.0, 8.0, fpb, 250);
    check(p.size() == 2, "a 0.1-beat tail after a wrap is dropped");
    p = loopRecordPasses(5000, 0.0, 0.0, 0.0, 0.0, 8.0, fpb, 250);
    check(p.size() == 1 && p[0].lengthFrames == 5000, "a take inside the loop stays one take");
    p = loopRecordPasses(20000, 9.0, 9.0, 0.0, 0.0, 8.0, fpb, 250);
    check(p.size() == 1, "a take started after the loop is not cut");
}

void testLoopRecordMakesOneTakePerPass() {
    std::cout << "[2] recording 10 s over a 4 s loop with Loop record on gives three takes at the loop start\n";
    auto tm = makeRecorder(true);
    const uint64_t trackId = armTrack(*tm);
    check(trackId != 0, "track armed");
    record(*tm, 10.0);
    const auto t = takes(*tm, trackId);
    check(t.size() == 3, "three takes (got " + std::to_string(t.size()) + ")");
    if (t.size() == 3) {
        check(std::abs(t[0].second - 4.0) < 0.01 && std::abs(t[1].second - 4.0) < 0.01 && std::abs(t[2].second - 2.0) < 0.01,
              "4 s, 4 s and 2 s");
        check(t[0].first < 0.01 && t[1].first < 0.01 && t[2].first < 0.01, "every pass starts at the loop start");
    }
}

void testLoopRecordOffKeepsOneTake() {
    std::cout << "[3] with Loop record off the same recording stays one 10 s take\n";
    auto tm = makeRecorder(false);
    const uint64_t trackId = armTrack(*tm);
    record(*tm, 10.0);
    const auto t = takes(*tm, trackId);
    check(t.size() == 1 && std::abs(t[0].second - 10.0) < 0.01, "one 10 s take");
}

} // namespace

int main() {
    Aestra::Tests::ScopedTempDirectory home("loop-record");
    g_projectDir = home.path();
    testPassesAreCutAtTheLoopEnd();
    testLoopRecordMakesOneTakePerPass();
    testLoopRecordOffKeepsOneTake();
    if (g_failures > 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "All loop record checks passed\n";
    return 0;
}
