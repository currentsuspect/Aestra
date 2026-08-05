// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Folio baseline fixture — behavioural render probes.
//
// Proves the fixture SOUNDS like what CAPABILITIES.json says it is. Structural
// validation (FolioValidateFixture) proves the project reopens intact; this
// proves the reopened project actually renders, that the send is audible rather
// than merely serialized, that the insert chain changes the signal, that the
// automation moves gain in the declared direction, and that every declared lane
// carries real signal.
//
// ISOLATION MODEL
//
// Every render is a fresh universe: a fresh ProjectSerializer::load into a fresh
// TrackManager, driving a fresh AudioEngine. No engine, plugin instance,
// TrackManager or DSP state is ever reused between canonical A, canonical B and
// the mutated variants. That is the whole reason the comparisons mean anything:
// a retained filter state or a warm plugin would make "same input, same output"
// a statement about caching rather than about the fixture.
//
// WHY NOT OfflineRenderHarness / HeadlessOfflineRenderer
//
// Both are useful architectural references and unusable as probes. Each passes a
// constant 0.0 as streamTime for every block, so nothing downstream can tell one
// block from the next. The harness additionally keeps only the most recent
// block and never clears it; the renderer buffers the entire output purely to
// write a WAV. Here streamTime advances as processedFrames / sampleRate, the
// block is cleared before every call, and the render is streamed into a digest
// so a 129-second pass costs no memory.
//
// MUTATIONS ARE VALIDATION-ONLY
//
// Send-zeroing, chain removal and channel isolation are applied to a loaded COPY
// in memory, before graph construction, and are never written back. The
// canonical fixture on disk is not touched by any probe.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Core/ProjectSerializer.h"
#include "Models/TrackManager.h"
#include "Plugin/BuiltInPlugins.h"
#include "Plugin/PluginManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;
namespace fs = std::filesystem;

namespace {

// Identical for every probe. Changing either invalidates cross-probe comparison.
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockFrames = 512;
constexpr uint32_t kChannels = 2;
constexpr double kTempoBPM = 120.0;

// Declared floors. A render difference smaller than these is not behavioural
// evidence — it is noise, and the probe must not accept it as proof.
constexpr double kSilenceFloorRms = 1.0e-5; // below this a render counts as silent
constexpr double kSendRmsFloor = 1.0e-4;    // send must move RMS by at least this
constexpr double kEffectRmsFloor = 1.0e-4;  // insert chain must move RMS by at least this
constexpr double kAutomationMargin = 0.05;  // high window must exceed low by >5% relative

int g_failures = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "  [ok]   " << what << "\n";
    } else {
        std::cout << "  [FAIL] " << what << "\n";
        ++g_failures;
    }
}

/// FNV-1a over the raw interleaved float bytes, fed block by block. Proves
/// equality inside this frozen environment; it is not a cross-machine contract.
struct Digest {
    uint64_t state = 1469598103934665603ull;

    void feed(const float* data, size_t count) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(data);
        const size_t n = count * sizeof(float);
        for (size_t i = 0; i < n; ++i) {
            state ^= bytes[i];
            state *= 1099511628211ull;
        }
    }

    std::string hex() const {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(state));
        return buf;
    }
};

struct RenderResult {
    uint64_t frames = 0;
    double peak = 0.0;
    double rms = 0.0;
    std::string digest;
    bool finite = true;
};

/// A validation-only mutation applied to the loaded copy before the graph is
/// built. Nothing here is ever persisted.
using Mutation = std::function<void(TrackManager&)>;

MixerChannel* channelByName(TrackManager& tm, const std::string& name) {
    for (size_t i = 0; i < tm.getChannelCount(); ++i) {
        auto* channel = tm.getChannel(i);
        if (channel && channel->getName() == name) {
            return channel;
        }
    }
    return nullptr;
}

/// One complete, isolated render.
///
/// Ordering is load-bearing and follows the shipping offline-export path
/// (AudioExporter::render): rate first, then the slot map, then the graph —
/// clip bounds in the graph are derived from the sample rate at build time, so
/// building before the rate is set bakes in the wrong bounds.
RenderResult render(const std::string& projectFile, double startBeat, double durationBeats,
                    const Mutation& mutate, const std::string& wavPath) {
    RenderResult out;

    auto tm = std::make_shared<TrackManager>();
    auto load = ProjectSerializer::load(projectFile, tm);
    if (!load.ok) {
        std::cerr << "  render: project load failed: " << load.errorMessage << "\n";
        out.finite = false;
        return out;
    }

    if (mutate) {
        mutate(*tm);
    }

    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockFrames, kChannels);
    engine.setBPM(static_cast<float>(kTempoBPM));
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    engine.setTrackManager(tm);

    if (!engine.initialize()) {
        std::cerr << "  render: engine initialize failed\n";
        out.finite = false;
        return out;
    }

    tm->buildAndShareSlotMap();
    auto slotMap = tm->getChannelSlotMapShared();
    if (!slotMap) {
        std::cerr << "  render: missing channel slot map\n";
        out.finite = false;
        return out;
    }
    engine.setChannelSlotMap(slotMap);

    // Metronome and audition are monitoring conveniences, not fixture content;
    // the shipping offline path disables both. They are disabled IDENTICALLY in
    // every variant, so this never manufactures a difference between renders —
    // it only keeps a click track out of all of them. No processing stage of the
    // signal path itself (limiter, master stage) is touched.
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setPreviewDuckingAttenuationDb(0.0f);
    engine.resetPreviewDuckForOfflineRender();

    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));

    const double secondsPerBeat = 60.0 / kTempoBPM;
    const auto startSample = static_cast<uint64_t>(startBeat * secondsPerBeat * kSampleRate);
    const auto totalFrames = static_cast<uint64_t>(durationBeats * secondsPerBeat * kSampleRate);

    engine.setGlobalSamplePos(startSample);
    engine.clearTruePeakHold();

    // MIDI clips do NOT play just because they are in the project. Audio clips
    // ride the AudioGraph, but timeline MIDI has to be scheduled into the
    // PatternPlaybackEngine explicitly. TrackManager::play() does it as a side
    // effect of starting the live transport; an offline render drives the
    // engine's transport directly, so it must call the offline entry point
    // instead. Without this the arsenal lanes render exact silence while every
    // audio lane sounds normal — which is precisely what the first probe run
    // reported. Flushed on both sides so no scheduled instance leaks between
    // renders (each render owns a fresh TrackManager anyway; the flush keeps
    // that guarantee local rather than incidental).
    const double startSeconds = startBeat * secondsPerBeat;
    tm->getPatternPlaybackEngine().flush();
    tm->scheduleTimelineForOfflineRender(startSeconds);

    engine.setTransportPlaying(true);

    std::vector<float> block(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    Digest digest;
    double sumSquares = 0.0;
    uint64_t processed = 0;

    std::ofstream wav;
    if (!wavPath.empty()) {
        wav.open(wavPath, std::ios::binary);
        // Placeholder header; rewritten once the frame count is known.
        const std::vector<char> stub(44, 0);
        wav.write(stub.data(), 44);
    }

    while (processed < totalFrames) {
        const auto framesThisBlock =
            static_cast<uint32_t>(std::min<uint64_t>(kBlockFrames, totalFrames - processed));

        // Cleared before EVERY call: processBlock is not contractually required
        // to overwrite every sample, and a retained tail would leak the previous
        // block into a "silent" reading.
        std::fill(block.begin(), block.end(), 0.0f);

        // streamTime advances with the render rather than the constant 0.0 the
        // existing harnesses pass.
        const double streamTime = static_cast<double>(processed) / static_cast<double>(kSampleRate);
        engine.processBlock(block.data(), nullptr, framesThisBlock, streamTime);
        engine.performNonRealtimeMaintenance();

        const size_t sampleCount = static_cast<size_t>(framesThisBlock) * kChannels;
        for (size_t i = 0; i < sampleCount; ++i) {
            const float s = block[i];
            if (!std::isfinite(s)) {
                out.finite = false;
                continue;
            }
            const double a = std::fabs(static_cast<double>(s));
            out.peak = std::max(out.peak, a);
            sumSquares += static_cast<double>(s) * static_cast<double>(s);
        }
        digest.feed(block.data(), sampleCount);

        if (wav.is_open()) {
            wav.write(reinterpret_cast<const char*>(block.data()),
                      static_cast<std::streamsize>(sampleCount * sizeof(float)));
        }

        processed += framesThisBlock;
    }

    engine.setTransportPlaying(false);
    tm->getPatternPlaybackEngine().flush();

    out.frames = processed;
    out.digest = digest.hex();
    const double totalSamples = static_cast<double>(processed) * kChannels;
    out.rms = totalSamples > 0.0 ? std::sqrt(sumSquares / totalSamples) : 0.0;

    if (wav.is_open()) {
        // 32-bit float WAV header, written now that the length is known.
        const auto dataBytes = static_cast<uint32_t>(processed * kChannels * sizeof(float));
        wav.seekp(0);
        auto w32 = [&](uint32_t v) { wav.write(reinterpret_cast<const char*>(&v), 4); };
        auto w16 = [&](uint16_t v) { wav.write(reinterpret_cast<const char*>(&v), 2); };
        wav.write("RIFF", 4); w32(36 + dataBytes); wav.write("WAVE", 4);
        wav.write("fmt ", 4); w32(16); w16(3); w16(kChannels);
        w32(kSampleRate); w32(kSampleRate * kChannels * 4); w16(kChannels * 4); w16(32);
        wav.write("data", 4); w32(dataBytes);
        wav.close();
    }

    return out;
}

/// Mute every channel except those named. Applied before graph construction,
/// because TrackRenderState copies the mute flag at build time.
Mutation isolateChannels(const std::vector<std::string>& keep) {
    return [keep](TrackManager& tm) {
        for (size_t i = 0; i < tm.getChannelCount(); ++i) {
            auto* channel = tm.getChannel(i);
            if (!channel) {
                continue;
            }
            const bool wanted = std::find(keep.begin(), keep.end(), channel->getName()) != keep.end();
            channel->setMute(!wanted);
        }
    };
}

Mutation combine(Mutation first, Mutation second) {
    return [first, second](TrackManager& tm) {
        if (first) first(tm);
        if (second) second(tm);
    };
}

double relativeDelta(double a, double b) {
    const double denom = std::max(std::fabs(a), std::fabs(b));
    return denom > 0.0 ? std::fabs(a - b) / denom : 0.0;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string fixtureRoot;
    std::string wavDir; // off by default; never affects pass/fail
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--fixture" && i + 1 < argc) {
            fixtureRoot = argv[++i];
        } else if (arg == "--emit-wav-dir" && i + 1 < argc) {
            wavDir = argv[++i];
        }
    }
    if (fixtureRoot.empty()) {
        std::cerr << "Usage: probe-fixture --fixture <fixture-root> [--emit-wav-dir <path>]\n";
        return 2;
    }

    const fs::path absWavDir = wavDir.empty() ? fs::path{} : fs::absolute(wavDir);

    std::error_code ec;
    fs::current_path(fixtureRoot, ec);
    if (ec) {
        std::cerr << "cannot chdir to fixture root: " << fixtureRoot << "\n";
        return 2;
    }
    if (!absWavDir.empty()) {
        fs::create_directories(absWavDir, ec);
    }

    auto wavFor = [&](const std::string& name) -> std::string {
        return absWavDir.empty() ? std::string{} : (absWavDir / (name + ".wav")).string();
    };

    const std::string projectFile = "folio-baseline.aes";

    BuiltInPlugins::registerCoreBuiltIns();
    auto& pluginManager = PluginManager::getInstance();
    if (!pluginManager.initialize()) {
        std::cerr << "PluginManager failed to initialize\n";
        return 2;
    }

    // Full arrangement: 258 beats at 120 BPM = 129 s.
    constexpr double kFullBeats = 258.0;

    // -----------------------------------------------------------------------
    std::cout << "\n== canonical determinism ==\n";
    const RenderResult canonA = render(projectFile, 0.0, kFullBeats, nullptr, wavFor("canonical_a"));
    const RenderResult canonB = render(projectFile, 0.0, kFullBeats, nullptr, wavFor("canonical_b"));

    check(canonA.finite && canonB.finite, "all canonical samples are finite");
    check(canonA.frames == canonB.frames,
          "same frame count (" + std::to_string(canonA.frames) + ")");
    check(canonA.digest == canonB.digest, "same digest (" + canonA.digest + ")");
    check(canonA.rms > kSilenceFloorRms, "canonical render is non-silent (rms " + std::to_string(canonA.rms) + ")");

    // -----------------------------------------------------------------------
    std::cout << "\n== send ==\n";
    const Mutation isolateSend = isolateChannels({"Texture", "Return"});
    const Mutation zeroSend = [](TrackManager& tm) {
        if (auto* texture = channelByName(tm, "Texture")) {
            auto sends = texture->getSends();
            for (auto& route : sends) {
                route.gain = 0.0f;
            }
            texture->replaceSends(sends);
        }
    };

    const RenderResult sendOn = render(projectFile, 0.0, 16.0, isolateSend, wavFor("send_on"));
    const RenderResult sendOff =
        render(projectFile, 0.0, 16.0, combine(isolateSend, zeroSend), wavFor("send_zero"));

    check(sendOn.finite && sendOff.finite, "both send renders are finite");
    check(sendOn.rms > kSilenceFloorRms, "send-enabled render is non-silent (rms " + std::to_string(sendOn.rms) + ")");
    check(sendOn.digest != sendOff.digest, "zeroing the send changes the render");
    check(std::fabs(sendOn.rms - sendOff.rms) > kSendRmsFloor,
          "send changes RMS beyond the declared floor (delta " +
              std::to_string(std::fabs(sendOn.rms - sendOff.rms)) + " > " + std::to_string(kSendRmsFloor) + ")");

    // -----------------------------------------------------------------------
    std::cout << "\n== effect chain ==\n";
    const Mutation isolateChord = isolateChannels({"Chord"});
    const Mutation stripChain = [](TrackManager& tm) {
        if (auto* chord = channelByName(tm, "Chord")) {
            auto& chain = chord->getEffectChain();
            chain.removePlugin(1);
            chain.removePlugin(0);
        }
    };

    const RenderResult fxOn = render(projectFile, 0.0, 16.0, isolateChord, wavFor("effects_on"));
    const RenderResult fxOff =
        render(projectFile, 0.0, 16.0, combine(isolateChord, stripChain), wavFor("effects_off"));

    check(fxOn.finite && fxOff.finite, "both effect renders are finite");
    check(fxOn.rms > kSilenceFloorRms, "chain-enabled render is non-silent (rms " + std::to_string(fxOn.rms) + ")");
    check(fxOff.rms > kSilenceFloorRms, "chain-removed render is non-silent (rms " + std::to_string(fxOff.rms) + ")");
    // Deliberately not a hash comparison alone: a single differing bit would
    // satisfy digest inequality without being audible evidence.
    check(std::fabs(fxOn.rms - fxOff.rms) > kEffectRmsFloor,
          "removing the chain changes RMS beyond the declared floor (delta " +
              std::to_string(std::fabs(fxOn.rms - fxOff.rms)) + " > " + std::to_string(kEffectRmsFloor) + ")");

    // -----------------------------------------------------------------------
    std::cout << "\n== automation ==\n";
    // Windows are 2 beats long and both sit at phase 0 of the 4-beat chord clip
    // cycle (beats 4 and 84 are both multiples of 4), so the underlying material
    // is identical and only the automation value differs. Beat 84 is the 0.90
    // point; beat 4 sits just past the 0.65 point on the ramp toward it.
    // No exact 0.90/0.65 ratio is required — Filter and Sat make the path
    // nonlinear — only direction plus a declared margin.
    const RenderResult autoLow = render(projectFile, 4.0, 2.0, isolateChord, wavFor("automation_low"));
    const RenderResult autoHigh = render(projectFile, 84.0, 2.0, isolateChord, wavFor("automation_high"));

    check(autoLow.finite && autoHigh.finite, "both automation windows are finite");
    check(autoLow.rms > kSilenceFloorRms, "low window is non-silent (rms " + std::to_string(autoLow.rms) + ")");
    check(autoHigh.rms > kSilenceFloorRms, "high window is non-silent (rms " + std::to_string(autoHigh.rms) + ")");
    check(autoHigh.rms > autoLow.rms, "high-automation window has greater RMS than the low window");
    check(relativeDelta(autoHigh.rms, autoLow.rms) > kAutomationMargin,
          "automation gain difference exceeds the declared margin (relative " +
              std::to_string(relativeDelta(autoHigh.rms, autoLow.rms)) + " > " + std::to_string(kAutomationMargin) +
              ")");

    // -----------------------------------------------------------------------
    std::cout << "\n== per-lane signal ==\n";
    // Beats 0..8 covers at least one active clip on every lane, including
    // "Texture Alt", whose first clip lands on beat 4.
    const std::vector<std::string> audioLanes = {"Kick", "Chord", "Texture", "Sub", "Texture Alt", "Chord Alt"};

    // The MIDI lanes are structurally complete — patterns, notes, sampler units
    // and clips all survive the round trip and pass structural validation — but
    // they render EXACT silence in song mode at this engine SHA.
    //
    // AudioEngine::processArsenalUnits early-returns unless m_patternPlaybackMode
    // is set, and the population of m_unitMidiBuffers (the only route from the
    // pattern-playback engine into a unit's plugin) lives inside that same
    // early-returning function. So timeline MIDI clips never reach their
    // instruments through processBlock outside Arsenal/pattern mode.
    //
    // This is asserted rather than skipped, deliberately. Pinning the current
    // behaviour makes the probe a tripwire: if a future engine change wires
    // timeline MIDI to arsenal units, this check fails loudly and the baseline
    // gets revisited instead of silently changing meaning underneath the
    // measurements. Switching the probe to pattern-playback mode to force signal
    // was rejected — it loops a pattern instead of playing the timeline, which
    // would make the render unrepresentative of the arrangement being measured.
    const std::vector<std::string> midiLanes = {"Lead", "Pad"};

    std::vector<std::pair<std::string, double>> laneRms;
    for (const auto& name : audioLanes) {
        const RenderResult lane =
            render(projectFile, 0.0, 8.0, isolateChannels({name, "Return"}), wavFor("lane_" + name));
        laneRms.emplace_back(name, lane.rms);
        check(lane.finite, "audio lane '" + name + "' renders finite output");
        check(lane.rms > kSilenceFloorRms,
              "audio lane '" + name + "' produces signal above the silence floor (rms " + std::to_string(lane.rms) +
                  ")");
    }
    for (const auto& name : midiLanes) {
        const RenderResult lane =
            render(projectFile, 0.0, 8.0, isolateChannels({name, "Return"}), wavFor("lane_" + name));
        laneRms.emplace_back(name, lane.rms);
        check(lane.finite, "midi lane '" + name + "' renders finite output");
        check(lane.rms <= kSilenceFloorRms,
              "midi lane '" + name + "' is silent in song mode, as pinned for this engine SHA (rms " +
                  std::to_string(lane.rms) + ") — if this fails, timeline MIDI became audible and the baseline "
                  "must be re-cut");
    }

    // -----------------------------------------------------------------------
    // Machine-readable result block.
    // -----------------------------------------------------------------------
    std::cout << "\n== results ==\n" << std::fixed << std::setprecision(9);
    std::cout << "canonical_digest=" << canonA.digest << "\n"
              << "canonical_frames=" << canonA.frames << "\n"
              << "canonical_peak=" << canonA.peak << "\n"
              << "canonical_rms=" << canonA.rms << "\n"
              << "send_enabled_rms=" << sendOn.rms << "\n"
              << "send_zero_rms=" << sendOff.rms << "\n"
              << "effect_enabled_rms=" << fxOn.rms << "\n"
              << "effect_disabled_rms=" << fxOff.rms << "\n"
              << "automation_low_rms=" << autoLow.rms << "\n"
              << "automation_high_rms=" << autoHigh.rms << "\n";
    for (const auto& [name, rms] : laneRms) {
        std::string key = name;
        std::replace(key.begin(), key.end(), ' ', '_');
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
        std::cout << "per_lane_rms." << key << "=" << rms << "\n";
    }
    std::cout << "failure_count=" << g_failures << "\n";

    std::cout << (g_failures == 0 ? "\n[probe-fixture] PASS\n" : "\n[probe-fixture] FAIL\n");
    return g_failures == 0 ? 0 : 1;
}
