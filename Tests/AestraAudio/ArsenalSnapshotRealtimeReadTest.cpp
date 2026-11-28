// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ArsenalSnapshotRealtimeReadTest — the audio thread must read the Arsenal snapshot through the
// real-time reader (UnitManager::readAudioSnapshotRt), never getAudioSnapshot().
//
// getAudioSnapshot() goes through AtomicSharedPtr, which is not guaranteed lock-free: in a C++17 build
// it takes a pooled mutex, and std::atomic<shared_ptr> uses a lock bit. The audio thread must not wait
// on either. The allocation trap does not see locks, so this test watches the call itself:
// getAudioSnapshot() reports a real-time misuse when it runs inside a real-time scope.
//
// Two renderer paths read the snapshot on every block and were the offenders:
//   AudioRenderer::renderArsenalUnitsForTrack  (a unit routed to a timeline track)
//   AudioRenderer::processArsenalUnits         (a unit that plays straight to the master)
// Both run here, in one engine, so a regression in either fails the test.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"
#include "RealtimeThreadGuard.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace Aestra::Audio;

namespace {
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kBlockSize = 256;
constexpr double kBpm = 120.0;
constexpr double kRenderSeconds = 1.0;
constexpr double kRenderBeats = 4.0;
constexpr double kTau = 6.28318530717958647692;

int g_failures = 0;

void check(bool condition, const char* label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) {
        ++g_failures;
    }
}

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

// Counts every misuse report, and separately the ones that name the snapshot reader.
std::atomic<int> g_misuseTotal{0};
std::atomic<int> g_misuseSnapshot{0};

void countMisuse(const char* apiName) noexcept {
    g_misuseTotal.fetch_add(1, std::memory_order_relaxed);
    if (apiName && std::strstr(apiName, "getAudioSnapshot") != nullptr) {
        g_misuseSnapshot.fetch_add(1, std::memory_order_relaxed);
    }
}

struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;
    uint16_t numChannels = 1;
    uint32_t sampleRate = kSampleRate;
    uint32_t byteRate = kSampleRate * 2;
    uint16_t blockAlign = 2;
    uint16_t bitsPerSample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};

bool writeToneWav(const std::filesystem::path& path, double frequencyHz, float amplitude, double seconds) {
    const uint32_t frames = static_cast<uint32_t>(seconds * static_cast<double>(kSampleRate));
    if (frames == 0) {
        return false;
    }
    std::vector<int16_t> pcm(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSampleRate);
        const float s = std::sin(kTau * frequencyHz * t) * amplitude;
        pcm[i] = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
    }
    WavHeader header;
    header.dataSize = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    header.fileSize = sizeof(WavHeader) - 8 + header.dataSize;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(header.dataSize));
    return out.good();
}

// The guard itself: getAudioSnapshot() is for control threads, readAudioSnapshotRt() for the audio thread.
void testGuardOnTheReaders() {
    UnitManager manager;
    manager.publishSnapshot();

    g_misuseSnapshot.store(0, std::memory_order_relaxed);
    auto previous = setRealtimeMisuseHandler(countMisuse);

    (void)manager.getAudioSnapshot(); // control thread: fine
    check(g_misuseSnapshot.load() == 0, "getAudioSnapshot() on a control thread reports nothing");

    {
        ScopedRealtimeAudioThread realtimeScope;
        auto pin = manager.readAudioSnapshotRt(); // the audio-thread reader: fine
        (void)pin;
    }
    check(g_misuseSnapshot.load() == 0, "readAudioSnapshotRt() on the audio thread reports nothing");

    {
        ScopedRealtimeAudioThread realtimeScope;
        (void)manager.getAudioSnapshot(); // wrong reader on the audio thread: must be reported
    }
    check(g_misuseSnapshot.load() == 1, "getAudioSnapshot() on the audio thread is reported as misuse");

    setRealtimeMisuseHandler(previous);
}

// The engine: render real blocks with a track-routed unit and a master-routed unit, and require that
// neither path calls the control-thread reader.
void testEngineRenderUsesTheRealtimeReader(const std::filesystem::path& tempRoot) {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);

    auto& playlist = tm->getPlaylistModel();
    const PlaylistLaneID laneId = playlist.createLane("Track 1");
    require(laneId.isValid(), "lane creation failed");
    auto* channel = tm->addChannelWithId("Track 1", 42);
    require(channel != nullptr, "channel creation failed");
    if (auto* lane = playlist.getLane(laneId)) {
        lane->volume = 1.0f;
        lane->pan = 0.0f;
    }
    channel->setVolume(1.0f);
    channel->setPan(0.0f);

    const auto samplePath = tempRoot / "snapshot_rt_read_tone.wav";
    require(writeToneWav(samplePath, 220.0, 0.55f, 1.0), "could not write the sample wav");

    auto& unitManager = tm->getUnitManager();

    const UnitID trackUnit = unitManager.createUnit("Routed to track", UnitType::Sampler);
    unitManager.setUnitAudioClip(trackUnit, samplePath.string());
    unitManager.setUnitEnabled(trackUnit, true);
    unitManager.setUnitGain(trackUnit, 1.0f);
    unitManager.setUnitMute(trackUnit, false);
    unitManager.setUnitMixerChannel(trackUnit, channel->getChannelId());
    unitManager.assignUnitToTimelineLane(trackUnit, 0);

    const UnitID masterUnit = unitManager.createUnit("Straight to master", UnitType::Sampler);
    unitManager.setUnitAudioClip(masterUnit, samplePath.string());
    unitManager.setUnitEnabled(masterUnit, true);
    unitManager.setUnitGain(masterUnit, 1.0f);
    unitManager.setUnitMute(masterUnit, false);
    unitManager.setUnitMixerChannel(masterUnit, 0);
    unitManager.clearUnitTimelineLane(masterUnit);

    // One pattern per unit, scheduled as separate instances, the way the mixed-route parity test does it.
    auto& patternManager = tm->getPatternManager();
    auto makePattern = [&](const char* name, uint8_t pitch, UnitID unit) {
        PatternID id = patternManager.createPattern();
        auto* pattern = patternManager.getPattern(id);
        require(pattern != nullptr, "pattern creation failed");
        pattern->type = PatternSource::Type::Midi;
        pattern->name = name;
        pattern->lengthBeats = kRenderBeats;
        pattern->payload = MidiPayload{};
        auto& notes = std::get<MidiPayload>(pattern->payload).notes;
        notes.push_back(MidiNote{pitch, 0.0, 0.75, 120.0f, 0.0f, unit});
        notes.push_back(MidiNote{static_cast<uint8_t>(pitch + 4), 1.0, 0.75, 110.0f, 0.0f, unit});
        return id;
    };
    const PatternID masterPatternId = makePattern("Snapshot RT read, master", 72, masterUnit);
    const PatternID trackPatternId = makePattern("Snapshot RT read, track", 60, trackUnit);

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine initialize failed");
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, kChannels);
    engine.setTrackManager(tm);
    engine.setBPM(static_cast<float>(kBpm));
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
    engine.setUnitManager(&tm->getUnitManager());
    engine.setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
    engine.setPatternPlaybackMode(true, kRenderBeats);
    engine.setGlobalSamplePos(0);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    tm->getPatternPlaybackEngine().rewindScheduledInstances();
    tm->getPatternPlaybackEngine().schedulePatternInstance(masterPatternId, 0.0, 1);
    tm->getPatternPlaybackEngine().schedulePatternInstance(trackPatternId, 0.0, 2);
    engine.setTransportPlaying(true);

    g_misuseTotal.store(0, std::memory_order_relaxed);
    g_misuseSnapshot.store(0, std::memory_order_relaxed);
    auto previous = setRealtimeMisuseHandler(countMisuse);

    const uint32_t totalFrames = static_cast<uint32_t>(kRenderSeconds * static_cast<double>(kSampleRate));
    std::vector<float> block(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
    float peak = 0.0f;
    uint32_t rendered = 0;
    while (rendered < totalFrames) {
        tm->getPatternPlaybackEngine().refillWindow(rendered, static_cast<int>(kSampleRate),
                                                    static_cast<int>(kSampleRate));
        const uint32_t frames = std::min(kBlockSize, totalFrames - rendered);
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, frames, 0.0);
        for (uint32_t i = 0; i < frames * kChannels; ++i) {
            peak = std::max(peak, std::abs(block[i]));
        }
        rendered += frames;
    }
    engine.setTransportPlaying(false);

    setRealtimeMisuseHandler(previous);

    // Without audible output the units never rendered and the checks below would prove nothing.
    std::cout << "render peak = " << peak << "\n";
    check(peak > 1.0e-4f, "both Arsenal units rendered audible output through processBlock");
    check(g_misuseSnapshot.load() == 0, "no block read the snapshot through getAudioSnapshot()");
    check(g_misuseTotal.load() == 0, "no real-time misuse of any kind during the render");
}

} // namespace

int main() {
    const auto tempRoot = std::filesystem::temp_directory_path() / "aestra_snapshot_rt_read_test";
    std::error_code ec;
    std::filesystem::create_directories(tempRoot, ec);

    // The sampler plugin behind every Arsenal unit is created by the plugin manager.
    require(PluginManager::getInstance().initialize(), "PluginManager initialize failed");

    testGuardOnTheReaders();
    testEngineRenderUsesTheRealtimeReader(tempRoot);

    std::filesystem::remove_all(tempRoot, ec);

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "arsenal snapshot real-time read passed\n";
    return 0;
}
