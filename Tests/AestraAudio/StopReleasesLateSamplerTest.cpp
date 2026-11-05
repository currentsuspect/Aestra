// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A stop releases EVERY sampler unit, including ones added after the engine was wired.
//
// The bug (owner, 4 Nov 2026, Angel Dust session): after one stop in Arsenal the drums cut, but the
// bass and the leads (long samples, release 1 ms) kept playing at full level until the next play.
// The engine released sampler voices through a cache of sampler pointers that was filled once, when
// the app wired the unit manager at start-up. Every unit added later (every sample dropped into
// Arsenal) was missing from it, and the sampler ignored the All Notes Off the engine also sends, so
// nothing reached those voices.
//
// Rig: the unit is created AFTER setUnitManager(), exactly like a sample dropped in mid-session.
//   A. Stop: the held note is silent once its 1 ms release has passed.
//   B. Loop wrap on the timeline: a note held across the wrap does not ring into the loop top.
//   C. Hard stop (panic): silent too.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"
#include "Plugin/SamplerPlugin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kBlock = 256;
constexpr uint64_t kBeat = 24000; // 120 BPM

int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::printf("  %s %s\n", cond ? "PASS" : "FAIL", what.c_str());
    g_failures += cond ? 0 : 1;
}

void le16(std::ofstream& o, uint16_t v) { o.put(char(v & 0xff)); o.put(char((v >> 8) & 0xff)); }
void le32(std::ofstream& o, uint32_t v) { le16(o, uint16_t(v & 0xffff)); le16(o, uint16_t(v >> 16)); }

// 10 s of 220 Hz sine at 0.5: longer than anything rendered, so sound after the release can only
// be a voice that never released.
bool writeWav(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const uint32_t frames = kRate * 10, bytes = frames * 2;
    out.write("RIFF", 4); le32(out, 36 + bytes); out.write("WAVEfmt ", 8); le32(out, 16);
    le16(out, 1); le16(out, 1); le32(out, kRate); le32(out, kRate * 2); le16(out, 2); le16(out, 16);
    out.write("data", 4); le32(out, bytes);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = double(i) / kRate;
        le16(out, uint16_t(int16_t(std::sin(6.28318530717958647692 * 220.0 * t) * 0.5 * 32767.0)));
    }
    return true;
}

struct Rig {
    std::shared_ptr<TrackManager> tm = std::make_shared<TrackManager>();
    AudioEngine engine;
    std::shared_ptr<Plugins::SamplerPlugin> sampler = std::make_shared<Plugins::SamplerPlugin>();
    std::vector<float> out = std::vector<float>(kBlock * 2);
    bool ok = false;

    explicit Rig(const std::filesystem::path& wav) {
        tm->setOutputSampleRate(kRate);
        engine.setSampleRate(kRate);
        engine.setBufferConfig(kBlock, 2);
        engine.setBPM(120.0f);
        engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
        if (auto slotMap = tm->getChannelSlotMapShared()) engine.setChannelSlotMap(slotMap);
        engine.setUnitManager(&tm->getUnitManager()); // wired with no units, as at app start
        engine.setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
        engine.setMetronomeEnabled(false);
        engine.setHoldStartForSchedule(true);

        // Added later: the sample dropped into Arsenal mid-session.
        if (!sampler->initialize(kRate, kBlock) || !sampler->loadSample(wav.string())) return;
        sampler->setRootMidiNote(60);
        sampler->setEnvelope(0.001f, 2.0f, 1.0f, 0.001f); // the session's units: full sustain, 1 ms release
        sampler->activate();
        auto& units = tm->getUnitManager();
        const UnitID unit = units.createUnit("bass", UnitType::Sampler);
        units.attachPlugin(unit, "com.Aestrastudios.sampler", sampler);
        units.setUnitEnabled(unit, true);

        MidiPayload payload;
        MidiNote note; // held from beat 0 for 8 beats: still sounding at every stop below
        note.pitch = 60;
        note.startBeat = 0.0;
        note.durationBeats = 8.0;
        note.velocity = 0.9f;
        note.unitId = unit;
        payload.notes.push_back(note);
        const PatternID pid = tm->getPatternManager().createMidiPattern("p", 8.0, payload);
        tm->getPatternPlaybackEngine().schedulePatternInstance(pid, 0.0, 1);
        ok = true;
    }

    float block() {
        engine.processBlock(out.data(), nullptr, kBlock, 0.0);
        engine.performNonRealtimeMaintenance();
        float peak = 0.0f;
        for (float v : out) peak = std::max(peak, std::abs(v));
        return peak;
    }
    float blocks(int n) {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) peak = std::max(peak, block());
        return peak;
    }
};

void testStop(const std::filesystem::path& wav) {
    Rig rig(wav);
    if (!rig.ok) { check(false, "A: rig setup"); return; }
    rig.engine.setGlobalSamplePos(0);
    rig.engine.setTransportPlaying(true);
    const float playing = rig.blocks(40); // ~0.2 s into the held note
    check(playing > 0.05f, "A: the held note is sounding before the stop (peak " + std::to_string(playing) + ")");

    rig.engine.setTransportPlaying(false);
    rig.blocks(4); // the stop block plus margin: a 1 ms release is long over
    const float after = rig.blocks(40); // measured after the release, not from the stop
    check(after < 1.0e-4f, "A: after one stop the late-added sampler is silent (peak " + std::to_string(after) + ")");
}

void testTimelineLoopWrap(const std::filesystem::path& wav) {
    Rig rig(wav);
    if (!rig.ok) { check(false, "B: rig setup"); return; }
    // Loop beats 0..2 while the note is scheduled for 8 beats from 0. Clear the note's own
    // schedule: the wrap release alone must end the voice, the next loop pass is silent.
    rig.engine.setLoopEnabled(true);
    rig.engine.setLoopRegion(0.0, 2.0);
    rig.engine.setGlobalSamplePos(0);
    rig.engine.setTransportPlaying(true);
    rig.blocks(20);
    rig.tm->getPatternPlaybackEngine().clearScheduledInstances(); // no more note-ons after this
    rig.blocks(200 - 20); // ~1.07 s: past the wrap at 1 s
    const float loopTop = rig.blocks(20);
    check(loopTop < 1.0e-4f, "B: a note held across the timeline loop wrap does not ring into the loop top (peak " +
                                 std::to_string(loopTop) + ")");
}

void testHardStop(const std::filesystem::path& wav) {
    Rig rig(wav);
    if (!rig.ok) { check(false, "C: rig setup"); return; }
    rig.engine.setGlobalSamplePos(0);
    rig.engine.setTransportPlaying(true);
    rig.blocks(40);
    rig.engine.panic();
    rig.engine.setTransportPlaying(false);
    rig.blocks(4);
    rig.engine.setTransportPlaying(true); // the next play: it must not resume the old voice either
    rig.tm->getPatternPlaybackEngine().clearScheduledInstances();
    rig.blocks(4);
    const float after = rig.blocks(40);
    check(after < 1.0e-4f, "C: after a hard stop the late-added sampler is silent (peak " + std::to_string(after) + ")");
}

} // namespace

int main() {
    std::printf("StopReleasesLateSamplerTest\n");
    const auto wav = std::filesystem::temp_directory_path() /
                     ("stop_late_sampler_" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
    if (!writeWav(wav)) { std::printf("  FAIL cannot write wav\n"); return 1; }
    testStop(wav);
    testTimelineLoopWrap(wav);
    testHardStop(wav);
    std::error_code ec;
    std::filesystem::remove(wav, ec);
    return g_failures == 0 ? 0 : 1;
}
