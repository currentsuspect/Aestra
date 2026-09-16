// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Spec 1 item 1 regression coverage (Timeline → Arsenal → Timeline audio).
//
// Drives the REAL transition call sequence from AestraContent::setViewFocus
// (ENTERING TIMELINE teardown + transport hot-swap) against a live
// TrackManager/AudioEngine pair with committed audible content, through the
// live processBlock path. No audio device needed.
//
// The core invariant: after the return sequence, pattern mode is off and the
// hot-swapped play schedules timeline instances that render audibly —
// TrackManager::play() in pattern mode pushes the playing command but
// schedules nothing, which is exactly "transport rolling in silence", so the
// suite pins both the healthy end-state and the trap itself.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Headless/HeadlessMusicGenerator.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Aestra::Audio::AudioEngine;
using Aestra::Audio::TrackManager;

namespace {

int g_failures = 0;

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    }
}

size_t scheduledInstances(TrackManager& tm) {
    return tm.getPatternPlaybackEngine().getActiveInstanceCount();
}

void report(const char* tag, TrackManager& tm) {
    std::cout << "[probe " << tag << "] patternMode=" << tm.isPatternMode()
              << " isPlaying=" << tm.isPlaying() << " instances=" << scheduledInstances(tm)
              << " position=" << tm.getPosition() << "\n";
}

// Wire the engine to the live session the way the exporter does, but WITHOUT
// clearing the live schedule and WITHOUT touching transport flags — so the
// blocks rendered below reflect the live transition state, not a re-render.
void wireEngineForLiveRender(AudioEngine& engine, TrackManager& tm,
                             std::shared_ptr<TrackManager>& borrowed) {
    borrowed = std::shared_ptr<TrackManager>(&tm, [](TrackManager*) {});
    engine.setSampleRate(48000);
    engine.setBufferConfig(512, 2);
    engine.setBPM(120.0f);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setPreviewDuckingAttenuationDb(0.0f);
    engine.setTrackManager(borrowed);
    engine.setUnitManager(&tm.getUnitManager());
    engine.setPatternPlaybackEngine(&tm.getPatternPlaybackEngine());
    tm.buildAndShareSlotMap();
    if (auto slotMap = tm.getChannelSlotMapShared()) {
        engine.setChannelSlotMap(slotMap);
    }
    engine.setGraph(Aestra::Audio::AudioGraphBuilder::buildFromTrackManager(tm));
    engine.initialize();
    tm.setCommandSink([&engine](const Aestra::Audio::AudioQueueCommand& cmd) {
        return engine.commandQueue().pushReliable(cmd);
    });
}

double renderPeak(AudioEngine& engine, double seconds) {
    constexpr uint32_t kFrames = 512;
    std::vector<float> buffer(static_cast<size_t>(kFrames) * 2u, 0.0f);
    double peak = 0.0;
    uint64_t remaining = static_cast<uint64_t>(48000.0 * seconds);
    while (remaining > 0) {
        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(kFrames, remaining));
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        engine.processBlock(buffer.data(), nullptr, n, 0.0);
        engine.performNonRealtimeMaintenance();
        for (uint32_t i = 0; i < n * 2u; ++i) {
            peak = std::max(peak, static_cast<double>(std::abs(buffer[i])));
        }
        remaining -= n;
    }
    return peak;
}

void resetTransport(TrackManager& tm) {
    tm.stop();
    tm.getPatternPlaybackEngine().clearScheduledInstances();
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path primePath = fs::temp_directory_path() / "aestra_switch_probe_prime.wav";
    fs::remove(primePath);

    AudioEngine engine;
    TrackManager trackManager;
    Aestra::Audio::HeadlessMusicGenerator gen(engine, trackManager);

    gen.createProject("switchprobe")
        .setTempo(120.0)
        .setSampleRate(48000)
        .createPattern("Kick", 16)
        .addNote(0, 36, 100, 1.0)
        .addNote(8, 36, 100, 1.0)
        .addClipToPlaylist("Kick", 0.0, 4.0);
    require(gen.exportTo(primePath.string(), 48000, Aestra::Audio::AudioExporter::BitDepth::PCM_16),
            "prime export commits audible content");
    fs::remove(primePath);

    // --- S1: playing timeline → Arsenal → Timeline + hot-swap ---
    trackManager.play();
    report("S1 timeline-playing", trackManager);
    require(scheduledInstances(trackManager) > 0, "S1: timeline play schedules instances");

    // ENTERING ARSENAL (engine mirror + armed pattern playback)
    engine.setPatternPlaybackMode(true, 16.0);
    trackManager.setPatternMode(true);

    // ENTERING TIMELINE teardown, leavingArsenal=true (AestraContent.cpp):
    // stopArsenalPlayback(false) covers patternStillArmed; panic kills voices.
    const bool leavingArsenal = true;
    const bool patternStillArmed = trackManager.isPatternMode();
    if (leavingArsenal || patternStillArmed) {
        if (patternStillArmed) {
            trackManager.stopArsenalPlayback(false);
        } else if (trackManager.isPlaying()) {
            trackManager.stop();
        }
        engine.panic();
    }
    engine.setPatternPlaybackMode(false, 4.0);

    // Hot-swap (wasPlaying): transport stop + play.
    trackManager.stop();
    trackManager.play();
    report("S1 after-return+hotswap", trackManager);
    require(!trackManager.isPatternMode(), "S1: pattern mode is off after the return");
    require(scheduledInstances(trackManager) > 0,
            "S1: hot-swapped play schedules timeline instances (else rolling silence)");

    // --- S2: stopped in Arsenal → Timeline, no hot-swap: stays stopped ---
    trackManager.play();
    trackManager.setPatternMode(true);
    trackManager.stop();
    if (trackManager.isPatternMode()) {
        trackManager.stopArsenalPlayback(false);
    }
    engine.panic();
    engine.setPatternPlaybackMode(false, 4.0);
    report("S2 stopped-return", trackManager);
    require(!trackManager.isPlaying(), "S2: a stopped return stays stopped");

    // --- S3: pin the trap the teardown exists to avoid ---
    // play() while patternMode is stuck ON pushes the playing command but
    // schedules nothing — rolling silence. Any future change that skips the
    // teardown must trip this before it ships.
    trackManager.setPatternMode(true);
    trackManager.play();
    report("S3 pattern-stuck-play", trackManager);
    require(scheduledInstances(trackManager) == 0,
            "S3: play() in pattern mode schedules nothing (the silence trap)");
    trackManager.stopArsenalPlayback(false);
    trackManager.stop();

    // --- Round 2: energy-measured, through processBlock (the live path) ---
    std::shared_ptr<TrackManager> borrowed;
    wireEngineForLiveRender(engine, trackManager, borrowed);

    // R0 control: plain timeline play renders audibly.
    resetTransport(trackManager);
    trackManager.play();
    const double peakR0 = renderPeak(engine, 2.0);
    std::cout << "[probe R0] plain timeline play peak=" << peakR0 << "\n";
    require(peakR0 > 0.0, "R0: control render is audible (else the probe setup is broken)");

    // R1: the real Arsenal -> Timeline sequence, then render.
    resetTransport(trackManager);
    trackManager.play();
    engine.setPatternPlaybackMode(true, 16.0);
    trackManager.setPatternMode(true);
    if (trackManager.isPatternMode()) {
        trackManager.stopArsenalPlayback(false);
    } else if (trackManager.isPlaying()) {
        trackManager.stop();
    }
    engine.panic();
    engine.setPatternPlaybackMode(false, 4.0);
    trackManager.stop();
    trackManager.play();
    const double peakR1 = renderPeak(engine, 2.0);
    std::cout << "[probe R1] post-switch render peak=" << peakR1 << "\n";
    require(peakR1 > 0.0, "R1: playback after the view switch renders audibly");

    // R2: panic AFTER play (the late-honor race, deterministically).
    resetTransport(trackManager);
    trackManager.play();
    engine.panic();
    const double peakR2 = renderPeak(engine, 2.0);
    std::cout << "[probe R2] panic-after-play render peak=" << peakR2 << "\n";

    // R3: panic mid-stream, measure only what comes after.
    resetTransport(trackManager);
    trackManager.play();
    renderPeak(engine, 0.5);
    engine.panic();
    const double peakR3 = renderPeak(engine, 1.5);
    std::cout << "[probe R3] post-mid-stream-panic peak=" << peakR3 << "\n";

    if (g_failures == 0) {
        std::cout << "All Timeline/Arsenal switch audio checks passed.\n";
        return 0;
    }
    std::cerr << g_failures << " probe check(s) failed.\n";
    return 1;
}
