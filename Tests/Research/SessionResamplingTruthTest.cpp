// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SessionResamplingTruthTest — full-session engine-level resampling truth (Phase 2D).
//
// Phase 1 (ResamplerQualityAuditTest) measured the resampling engines in ISOLATION.
// This test proves the same behavior through the actual shipped paths:
//
//   1. Realtime playback: TrackManager -> AudioGraphBuilder -> AudioEngine::processBlock
//      -> renderGraph's inline clip loop (AudioEngine.cpp:2066-2348), driven
//      block-by-block exactly like the device callback (GoldenAudio::renderBlocks).
//      A clip whose AudioBufferData declares its true source rate flows through
//      PlaylistModel::buildRuntimeSnapshot -> ClipRenderState.sourceSampleRate into a
//      `phase += ratio` interpolator loop. FINDING (this test): for quality Sinc64
//      that loop dispatches to the LEGACY exact-sinc Sinc64Interpolator
//      (AudioEngine.cpp:2326, per-sample double-precision Kaiser sinc, no LUT) —
//      NOT Sinc64Turbo. Phase 1's ~88 dB LUT-quantization floor therefore does NOT
//      apply to mainline playback, which measures ~146-154 dB here.
//      Phase 4: DOWNSAMPLED clips are additionally anti-aliased by the ClipPrefilter
//      pipeline (worker-thread Kaiser low-pass at clip load / rate change; see
//      AestraDocs/clip-prefilter-lifecycle.md) — the F1 downsampling gates in this
//      test flipped from "KNOWN LIMITATION" to "< -95 dBc", and a dedicated case
//      proves the non-blocking fallback (unfiltered until the copy is ready).
//   2. Offline full-mix export: AudioEngine::bounceRangeToWav(trackId=-1) ->
//      AudioExporter -> the same processBlock/renderGraph at a 4096-frame block size,
//      written as Float_32. Same legacy kernel; proven sample-identical to realtime.
//   3. Offline ISOLATED-TRACK bounce: bounceRangeToWav(trackId>=0) takes a different
//      loop (AudioEngine.cpp:3102+) through AudioRenderer::renderClipAudio
//      (AudioRenderer.cpp:246+). Phase 2D found it dispatching Sinc64 -> Sinc64Turbo
//      (~88 dB floor) — finding F6; Phase 2E unified its kernel table with
//      renderGraph's, and this test now proves solo bounces match the full mix.
//   4. Preview: PreviewEngine::processRealtime — its OWN resampler (per-voice
//      `phase += ratio` with a Cubic Hermite kernel, PreviewEngine.cpp:285+), NOT the
//      Interpolators dispatch and NOT affected by the user's quality setting.
//   5. Sampler pitch-shift: SamplerPlugin::process — Sinc64Turbo via the same
//      Interpolators family (SamplerPlugin.cpp:402), ratio = 2^(semitones/12).
//
// Two kinds of checks, deliberately separated:
//   * REGRESSION gates (one-sided): fail only if behavior gets WORSE than measured.
//     A future ratio-aware anti-aliasing improvement passes these unchanged.
//   * IDENTITY checks (exact): the session render must null against the isolated
//     interpolator replica scaled by the pan-law center gain. These pin that the
//     shipped session path IS the isolated path Phase 1 measured. If production
//     resampling changes intentionally (e.g. anti-aliasing lands in AudioRenderer),
//     these checks MUST be updated in the same PR — they characterize current
//     behavior, they are not a quality target.
//
// Session rate matrix: 44.1k clip in 48k session, 48k in 44.1k, 96k in 48k,
// 48k in 96k, plus a 48k-in-48k control. Probe frequencies match Phase 1 so the
// isolated and full-session numbers are directly comparable.
//
// Every gate threshold cites the measurement that justified it. Numbers printed
// with [MEASURE] are the raw data quoted in AestraDocs/audio-research-bench.md.

#include "AudioMeasure.h"
#include "SignalLab.h"

#include "GoldenAudio/GoldenAudioHarness.h"

#include "DSP/ClipPrefilter.h"
#include "DSP/Interpolators.h"
#include "DSP/PanLaw.h"
#include "IO/MiniAudioDecoder.h"
#include "Playback/PreviewEngine.h"
#include "Plugin/SamplerPlugin.h"
#include "PluginHost.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace AR = AudioResearch;
namespace GA = GoldenAudio;
using namespace Aestra::Audio;
namespace fs = std::filesystem;
namespace Interp = Aestra::Audio::Interpolators;

namespace {

constexpr double kAmp = 0.5;
constexpr double kToneHz = 1000.0;
constexpr double kClipSeconds = 1.5;
// Analysis window skips the head (transport start + clip edge fade of 128 project
// samples + kernel starvation) and the tail (clip edge fade-out) generously.
constexpr uint32_t kWinSkip = 8192;

// =============================================================================
// Session fixture: a clip that declares its TRUE source rate
// =============================================================================

/// Like GoldenAudio::addAudioTrack, but the clip buffer keeps the Signal's own
/// sample rate — the engine must resample it to the session rate. This is the
/// exact fixture shape a recorded/imported file takes (MiniAudioDecoder decodes
/// at native rate; PlaylistModel:428 copies buffer->sampleRate into the snapshot).
void addAudioTrackAtRate(TrackManager& tm, const std::string& label, const AR::Signal& clip,
                         const GA::SessionConfig& cfg) {
    tm.addChannel(label);

    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = clip.sampleRate;
    buffer->numChannels = clip.channels;
    buffer->numFrames = clip.frames();
    buffer->interleavedData = clip.samples;

    const std::string path = (fs::temp_directory_path() / ("session_truth_" + label + ".wav")).string();
    ClipSourceID sourceId = tm.getSourceManager().createRecordedSource(path, label, buffer);

    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = static_cast<double>(clip.frames()) / clip.sampleRate;
    payload.slices.push_back({0.0, payload.durationSeconds, 0.0, static_cast<double>(clip.frames())});

    PlaylistLaneID laneId = tm.getPlaylistModel().createLane(label);
    const double durationBeats = payload.durationSeconds * (static_cast<double>(cfg.bpm) / 60.0);
    PatternID patternId = tm.getPatternManager().createAudioPattern(label, durationBeats, payload);
    tm.getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, durationBeats);
}

std::shared_ptr<TrackManager> buildSession(const std::string& label, const AR::Signal& clip,
                                           const GA::SessionConfig& cfg, bool waitForPrefilters = true) {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    addAudioTrackAtRate(*tm, label, clip, cfg);
    if (waitForPrefilters) {
        // Phase 4: queue + deterministically complete the anti-alias prefilter for
        // downsampled clips before any render (no-op for same-rate/upsampling).
        tm->ensureClipPrefilters();
        tm->waitForClipPrefilters();
    }
    return tm;
}

/// Render `renderFrames` of the session through the realtime path at the given
/// interpolation quality (the app applies the user's Resampling setting the same
/// way: AudioSettingsPage -> AudioEngine::setInterpolationQuality; default High/Sinc64).
AR::Signal renderSessionRealtime(const std::shared_ptr<TrackManager>& tm, const GA::SessionConfig& cfg,
                                 uint64_t renderFrames, Interp::InterpolationQuality quality) {
    AudioEngine engine;
    GA::prepareEngine(engine, tm, cfg);
    engine.setInterpolationQuality(quality);
    engine.setTransportPlaying(true);
    std::vector<float> out = GA::renderBlocks(engine, renderFrames, cfg);
    engine.setTransportPlaying(false);

    AR::Signal s;
    s.sampleRate = cfg.sampleRate;
    s.channels = cfg.channels;
    s.samples = std::move(out);
    return s;
}

// =============================================================================
// Isolated replica (the Phase-1 harness) — the IDENTITY reference
// =============================================================================

using InterpFn = void (*)(const float*, int64_t, double, float&, float&);

/// Replicates the production clip-resampling loop (output frame n reads source
/// position n*(src/dst)) exactly as ResamplerQualityAuditTest does. Instantiated with
/// Sinc64Interpolator it models the MAINLINE renderGraph path; with Sinc64Turbo it
/// models the AudioRenderer path (isolated-track bounce). The session renders are
/// nulled against the matching replica — the independent reimplementation.
AR::Signal resampleViaInterpolator(InterpFn fn, const AR::Signal& src, uint32_t dstRate) {
    AR::Signal out;
    out.sampleRate = dstRate;
    out.channels = 2;
    const double ratio = static_cast<double>(src.sampleRate) / static_cast<double>(dstRate);
    const uint32_t outFrames =
        static_cast<uint32_t>(std::floor(static_cast<double>(src.frames()) * dstRate / src.sampleRate));
    out.samples.resize(static_cast<size_t>(outFrames) * 2);
    double phase = 0.0;
    for (uint32_t i = 0; i < outFrames; ++i) {
        float l = 0.0f;
        float r = 0.0f;
        fn(src.samples.data(), src.frames(), phase, l, r);
        out.at(i, 0) = l;
        out.at(i, 1) = r;
        phase += ratio;
    }
    return out;
}

/// The Phase-4 anti-alias step, applied via the PRODUCTION ClipPrefilter functions:
/// what a downsampled clip's buffer contains by the time the interpolator reads it.
/// Returns the input unchanged when no prefilter applies (same-rate/upsampling).
AR::Signal prefilterReplica(const AR::Signal& clip, uint32_t sessionRate) {
    if (!ClipPrefilter::isNeeded(clip.sampleRate, sessionRate)) {
        return clip;
    }
    const std::vector<double> h = ClipPrefilter::design(clip.sampleRate, sessionRate);
    AR::Signal out = clip;
    ClipPrefilter::apply(clip.samples.data(), out.samples.data(), clip.frames(), clip.channels, h);
    return out;
}

/// Window [start, end) of a signal as a standalone Signal (for interior-only diffs).
AR::Signal window(const AR::Signal& s, uint32_t startFrame, uint32_t endFrame) {
    AR::Signal w;
    w.sampleRate = s.sampleRate;
    w.channels = s.channels;
    startFrame = std::min(startFrame, s.frames());
    endFrame = std::min(endFrame, s.frames());
    if (endFrame > startFrame) {
        w.samples.assign(s.samples.begin() + static_cast<size_t>(startFrame) * s.channels,
                         s.samples.begin() + static_cast<size_t>(endFrame) * s.channels);
    }
    return w;
}

// =============================================================================
// Rate-pair matrix (probes identical to Phase 1 for direct comparability)
// =============================================================================

struct SessionPair {
    uint32_t srcRate;     // clip's true rate
    uint32_t sessionRate; // engine/session rate
    double probeHz;       // near-Nyquist probe (see ResamplerQualityAuditTest kPairs note)
    double artifactHz;    // expected image/alias frequency in the session output
    bool downsampling;
    // Phase-1 isolated Sinc64Turbo measurement (ResamplerQualityAuditTest, 2026-07-07),
    // quoted so the printed kernel-split delta has its reference in the log:
    double turboArtifactDbc;
    // REGRESSION ceiling (one-sided): session artifact must not be HOTTER than this.
    // Improvement (a lower level, e.g. from future anti-aliasing) passes.
    // Ceilings cite the session measurement (legacy Sinc64Interpolator kernel).
    double artifactCeilingDbc;
};

const SessionPair kSessionPairs[] = {
    // src     session   probe    artifact  down   turbo   ceiling
    // Upsampling ceilings unchanged (measured session: -21.7 / -63.3 dBc).
    // Downsampling ceilings are the Phase-4 target: the clip prefilter must hold
    // aliases below -95 dBc (measured through real sessions: -104.9 / -117.3 dBc;
    // pre-Phase-4 these folded at -1.1 / -0.0 dBc — finding F1, now resolved on
    // the mainline path).
    {44100, 48000, 21000.0, 23100.0, false, -21.7, -18.0},
    {48000, 44100, 23000.0, 21100.0, true, -1.1, -95.0},
    {96000, 48000, 30000.0, 18000.0, true, -0.0, -95.0},
    {48000, 96000, 21600.0, 26400.0, false, -60.5, -60.0},
};

std::string pairName(const SessionPair& p) {
    return std::to_string(p.srcRate) + "clip_in_" + std::to_string(p.sessionRate) + "session";
}

// =============================================================================
// Float32 WAV writer (IEEE float, format tag 3) — keeps preview/sampler probe
// sources exact so measured floors are the DSP's, not 16-bit quantization.
// =============================================================================

bool writeWavFloat32(const fs::path& path, const std::vector<float>& interleaved, uint32_t channels,
                     uint32_t sampleRate) {
    std::ofstream f(path.string(), std::ios::binary);
    if (!f) {
        return false;
    }
    auto write16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    auto write32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    const uint32_t dataSize = static_cast<uint32_t>(interleaved.size() * sizeof(float));
    f.write("RIFF", 4);
    write32(36 + dataSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    write32(16);
    write16(3); // IEEE float
    write16(static_cast<uint16_t>(channels));
    write32(sampleRate);
    write32(sampleRate * channels * sizeof(float));
    write16(static_cast<uint16_t>(channels * sizeof(float)));
    write16(32);
    f.write("data", 4);
    write32(dataSize);
    f.write(reinterpret_cast<const char*>(interleaved.data()), dataSize);
    return f.good();
}

// =============================================================================
// Case 1: control — 48k clip in a 48k session (no resampling; baseline gain/DC/noise)
// =============================================================================

struct ControlBaseline {
    double gainL{0.0};
    double sinadDb{0.0};
    double dc{0.0};
};

ControlBaseline runControlCase(AR::CheckSession& t) {
    std::printf("\n--- control: 48k clip in 48k session (same-rate path) ---\n");
    GA::SessionConfig cfg;
    cfg.sampleRate = 48000;
    const uint32_t clipFrames = static_cast<uint32_t>(kClipSeconds * 48000.0);
    const AR::Signal clip = AR::makeSine(48000, clipFrames, kToneHz, kAmp);
    auto tm = buildSession("ctrl1k", clip, cfg);
    const AR::Signal out =
        renderSessionRealtime(tm, cfg, clipFrames + 4800, Interp::InterpolationQuality::Sinc64);

    const uint32_t winEnd = clipFrames - kWinSkip;
    const AR::ToneFit fit = AR::fitTone(out, 0, kToneHz, kWinSkip, winEnd);
    const double expectedGain = static_cast<double>(PanLaw::kEqualPowerCenterGain);
    const double gain = fit.amplitude / kAmp;
    // DC from the tone fit's constant term: a windowed arithmetic mean of a sine leaks
    // the truncated partial cycle (~5e-6 for this window), which is a measurement
    // artifact, not engine DC. The least-squares fit separates the two.
    const double dc = fit.dc;
    std::printf("[MEASURE] control 1kHz gain=%.9f (pan-law expect %.9f), sinad=%.1f dB, dc=%.3e\n", gain,
                expectedGain, fit.sinadDb, dc);

    // Gain gate 1e-4: the session applies exactly the equal-power center pan gain
    // (measured 0.707106754 vs expected 0.707106769; SampleRateBufferTruthTest
    // independently measured the impulse amplitude exact to 1e-6).
    t.expectNear("control: net session gain == pan-law center gain", gain, expectedGain, 1e-4);
    // Same-rate clips take the direct-copy branch (AudioEngine.cpp:2112,
    // |ratio-1| < 1e-9): no interpolation, so the residual measures only
    // engine-added noise. Gate 120 dB (measured 153.9 dB — the float-quantization
    // floor of the source signal itself).
    t.expect("control: same-rate 1 kHz residual SINAD > 120 dB", fit.sinadDb > 120.0,
             "sinadDb=" + std::to_string(fit.sinadDb));
    // DC gate 1e-6: measured 1.3e-9 — the mix chain adds no offset.
    t.expect("control: DC offset < 1e-6", std::abs(dc) < 1e-6, "dc=" + std::to_string(dc));

    ControlBaseline b;
    b.gainL = gain;
    b.sinadDb = fit.sinadDb;
    b.dc = dc;
    return b;
}

// DC through the full session chain (control + one fractional pair). Phase 1 proved
// the interpolator preserves DC to 4.5e-9 in isolation; this pins that the session
// chain neither blocks nor shifts it.
void runDcCases(AR::CheckSession& t) {
    std::printf("\n--- DC through the full session chain ---\n");
    const double expected = kAmp * static_cast<double>(PanLaw::kEqualPowerCenterGain);
    for (uint32_t srcRate : {48000u, 44100u}) {
        GA::SessionConfig cfg;
        cfg.sampleRate = 48000;
        const uint32_t clipFrames = static_cast<uint32_t>(kClipSeconds * srcRate);
        const AR::Signal clip = AR::makeDC(srcRate, clipFrames, kAmp);
        auto tm = buildSession("dc" + std::to_string(srcRate), clip, cfg);
        const uint32_t outClipFrames = static_cast<uint32_t>(kClipSeconds * cfg.sampleRate);
        const AR::Signal out =
            renderSessionRealtime(tm, cfg, outClipFrames + 4800, Interp::InterpolationQuality::Sinc64);
        const double dc = AR::dcOffset(out, -1, kWinSkip, outClipFrames - kWinSkip);
        std::printf("[MEASURE] DC %uk clip in 48k session: out=%.9f (expect %.9f)\n", srcRate / 1000, dc,
                    expected);
        // Gate 1e-5: both cases measured equal to the expectation at 1e-9 print
        // precision (same-rate copy and Sinc64 resample both preserve DC).
        t.expectNear(("session DC preserved (" + std::to_string(srcRate) + " -> 48000)").c_str(), dc,
                     expected, 1e-5);
    }
}

// =============================================================================
// Case 2: per-pair full-session audit (realtime path, Sinc64 = shipped default)
// =============================================================================

void auditSessionPair(AR::CheckSession& t, const SessionPair& p, const ControlBaseline& control) {
    const std::string name = "session " + pairName(p);
    std::printf("\n--- full session: %s (Sinc64, shipped default) ---\n", pairName(p).c_str());

    GA::SessionConfig cfg;
    cfg.sampleRate = p.sessionRate;
    const uint32_t srcFrames = static_cast<uint32_t>(kClipSeconds * p.srcRate);
    const uint32_t outClipFrames = static_cast<uint32_t>(kClipSeconds * p.sessionRate);
    const uint32_t winEnd = outClipFrames - kWinSkip;
    const double panGain = static_cast<double>(PanLaw::kEqualPowerCenterGain);

    // ---- 1 kHz: gain vs control, SINAD through the whole engine ----
    {
        const AR::Signal clip = AR::makeSine(p.srcRate, srcFrames, kToneHz, kAmp);
        auto tm = buildSession(pairName(p) + "_1k", clip, cfg);
        const AR::Signal out =
            renderSessionRealtime(tm, cfg, outClipFrames + 4800, Interp::InterpolationQuality::Sinc64);
        const AR::ToneFit fit = AR::fitTone(out, 0, kToneHz, kWinSkip, winEnd);
        const double gain = fit.amplitude / kAmp;
        const double gainVsControlDb = AR::toDb(gain / control.gainL);
        std::printf("[MEASURE] %s 1kHz gain=%.9f (vs control %+.6f dB), sinad=%.1f dB\n", name.c_str(), gain,
                    gainVsControlDb, fit.sinadDb);
        // Gate 0.05 dB vs control: Phase 1 measured interpolator passband gain exact
        // to <1e-5 dB, and the Phase-4 clip prefilter is passband-exact to <0.0001 dB
        // (lab §9.3); the session must not add level error on top.
        t.expect((name + ": 1 kHz level matches control within 0.05 dB").c_str(),
                 std::abs(gainVsControlDb) < 0.05, "deltaDb=" + std::to_string(gainVsControlDb));
        // FINDING + gate 140 dB: measured 149.3 / 146.5 / 153.9 / 147.1 dB across the
        // four pairs — mainline playback uses the LEGACY exact-sinc Sinc64Interpolator
        // (AudioEngine.cpp:2326), so it does NOT have Sinc64Turbo's ~88 dB LUT floor.
        // This gate deliberately fails if mainline clip rendering is ever switched to
        // the Turbo kernel (which would be a real, measurable quality regression).
        // (Unaffected by the Phase-4 prefilter: 1 kHz is deep in its passband.)
        t.expect((name + ": 1 kHz residual SINAD > 140 dB (legacy exact-sinc kernel)").c_str(),
                 fit.sinadDb > 140.0, "sinadDb=" + std::to_string(fit.sinadDb));

        // ---- length truth on the in-band tone (survives the anti-alias filter) ----
        const double postPeak = AR::peak(out, -1, outClipFrames + 256, out.frames());
        int64_t lastAudible = -1;
        for (uint32_t i = outClipFrames + 255; i > 0; --i) {
            if (std::abs(out.at(i, 0)) > 1e-4 || std::abs(out.at(i, 1)) > 1e-4) {
                lastAudible = i;
                break;
            }
        }
        std::printf("[MEASURE] %s length: clip end expect frame %u, last audible=%lld, post-clip peak=%.2e\n",
                    name.c_str(), outClipFrames, static_cast<long long>(lastAudible), postPeak);
        t.expect((name + ": tone reaches the clip end (within edge fade)").c_str(),
                 lastAudible >= static_cast<int64_t>(outClipFrames) - 130 &&
                     lastAudible <= static_cast<int64_t>(outClipFrames) + 2,
                 "lastAudible=" + std::to_string(lastAudible) + " expectedEnd=" + std::to_string(outClipFrames));
        // Gate 1e-5: measured post-clip peak is exactly 0 (renderer writes silence
        // past clip.endSample).
        t.expect((name + ": silence after clip end").c_str(), postPeak < 1e-5,
                 "postPeak=" + std::to_string(postPeak));
    }

    // ---- near-Nyquist probe: artifact level + IDENTITY null vs isolated replica ----
    {
        const AR::Signal clip = AR::makeSine(p.srcRate, srcFrames, p.probeHz, kAmp);
        auto tm = buildSession(pairName(p) + "_probe", clip, cfg);
        const AR::Signal out =
            renderSessionRealtime(tm, cfg, outClipFrames + 4800, Interp::InterpolationQuality::Sinc64);

        const double artifactAmp = AR::toneAmplitude(out, 0, p.artifactHz, kWinSkip, winEnd);
        // dBc relative to the full-scale probe as delivered through the session
        // chain (clip amp * pan gain), matching Phase 1's dBc-vs-input convention.
        const double artifactDbc = AR::toDb(artifactAmp / (kAmp * panGain));

        // Isolated replica of the MAINLINE pipeline: the PRODUCTION clip prefilter
        // (downsampling pairs only; identity otherwise) followed by the legacy
        // exact-sinc kernel — measured live with the same window. This is the
        // headline isolated-vs-full-session agreement figure.
        const AR::Signal filteredClip = prefilterReplica(clip, p.sessionRate);
        const AR::Signal isolated =
            resampleViaInterpolator(Interp::Sinc64Interpolator::interpolate, filteredClip, p.sessionRate);
        const double isoArtifactAmp = AR::toneAmplitude(isolated, 0, p.artifactHz, kWinSkip, winEnd);
        const double isoArtifactDbc = AR::toDb(isoArtifactAmp / kAmp);

        std::printf("[MEASURE] %s probe %.0f Hz -> artifact %.0f Hz: session=%.2f dBc, "
                    "isolated(prefilter+legacy)=%.2f dBc (delta %.3f dB) | pre-Phase-4 "
                    "unfiltered path measured %.1f dBc\n",
                    name.c_str(), p.probeHz, p.artifactHz, artifactDbc, isoArtifactDbc,
                    artifactDbc - isoArtifactDbc, p.turboArtifactDbc);

        // Gate (one-sided): not hotter than the ceiling. Downsampling pairs carry the
        // Phase-4 anti-alias target (< -95 dBc; measured -104.9 / -117.2 dBc through
        // real sessions, vs -1.1 / -0.0 dBc before the clip prefilter — F1 resolved
        // on this path). Upsampling ceilings are unchanged regression pins.
        t.expect((name + (p.downsampling ? ": ANTI-ALIASED (Phase 4) - downsampling alias below -95 dBc"
                                         : ": upsampling image not above ceiling"))
                     .c_str(),
                 artifactDbc < p.artifactCeilingDbc,
                 "artifactDbc=" + std::to_string(artifactDbc) +
                     " ceiling=" + std::to_string(p.artifactCeilingDbc));
        // AGREEMENT (characterization): session and replica artifact levels agree.
        // This pins WHICH pipeline the shipped mainline path runs (prefilter+legacy
        // kernel). If mainline clip resampling intentionally changes again, update
        // this check and the research doc together (see header).
        t.expectNear((name + ": isolated-vs-session artifact agreement (dB)").c_str(), artifactDbc,
                     isoArtifactDbc, 0.5);

        // IDENTITY null: interior of the session render must equal the replica
        // scaled by the pan-law center gain, sample by sample.
        AR::Signal expected = isolated;
        for (float& v : expected.samples) {
            v = static_cast<float>(static_cast<double>(v) * panGain);
        }
        const AR::Signal outWin = window(out, kWinSkip, winEnd);
        const AR::Signal expWin = window(expected, kWinSkip, winEnd);
        const AR::DiffReport d = AR::diff(outWin, expWin, 1e-4);
        std::printf("[MEASURE] %s identity null vs isolated(prefilter+legacy)*panGain: maxErr=%.3e "
                    "rmsErr=%.1f dB\n",
                    name.c_str(), d.maxAbsError, d.rmsErrorDb);
        // Gates: measured -153.6 to -275.6 dB RMS across the four pairs, maxErr
        // <= 9e-8 (renderGraph recomputes phase per block from the same closed form
        // the replica accumulates, AudioEngine.cpp:2086; the legacy kernel is
        // phase-continuous, and the replica applies the identical production
        // prefilter, so the pipelines are the same math end to end).
        const bool nullPass = d.rmsErrorDb < -120.0 && d.maxAbsError < 1e-5;
        if (!t.expect((name + ": IDENTITY - session render nulls against isolated replica").c_str(),
                      nullPass,
                      "rmsErrDb=" + std::to_string(d.rmsErrorDb) +
                          " maxAbs=" + std::to_string(d.maxAbsError))) {
            AR::printDiffForensics(name + " identity null", d, outWin, expWin);
        }
        // (Length truth moved to the in-band 1 kHz render above: a between-Nyquists
        // probe is REMOVED by the Phase-4 anti-alias filter, so it can no longer
        // witness the clip end.)
    }
}

// =============================================================================
// Case 2b: fallback transition — before the prefilter is ready, playback is the
// pre-Phase-4 path; after wait + rebuild it is anti-aliased. Deterministic: the
// filtered variant is only applied on the graph-build thread inside
// ensureClipPrefilters(), so the FIRST graph build can never see it.
// =============================================================================

void runFallbackTransitionCase(AR::CheckSession& t) {
    std::printf("\n--- fallback transition: 48k clip in 44.1k session, before/after prefilter ---\n");
    GA::SessionConfig cfg;
    cfg.sampleRate = 44100;
    const uint32_t srcFrames = static_cast<uint32_t>(kClipSeconds * 48000);
    const uint32_t outClipFrames = static_cast<uint32_t>(kClipSeconds * 44100);
    const AR::Signal clip = AR::makeSine(48000, srcFrames, 23000.0, kAmp);
    auto tm = buildSession("fallback", clip, cfg, /*waitForPrefilters=*/false);
    const double panGain = static_cast<double>(PanLaw::kEqualPowerCenterGain);
    const uint32_t winEnd = outClipFrames - kWinSkip;

    // First-ever graph build: enqueues the job and renders with the ORIGINAL buffer.
    const AR::Signal before =
        renderSessionRealtime(tm, cfg, outClipFrames, Interp::InterpolationQuality::Sinc64);
    const double beforeDbc =
        AR::toDb(AR::toneAmplitude(before, 0, 21100.0, kWinSkip, winEnd) / (kAmp * panGain));

    // Deterministic completion, then a fresh build picks the filtered copy up.
    tm->waitForClipPrefilters();
    const AR::Signal after =
        renderSessionRealtime(tm, cfg, outClipFrames, Interp::InterpolationQuality::Sinc64);
    const double afterDbc =
        AR::toDb(AR::toneAmplitude(after, 0, 21100.0, kWinSkip, winEnd) / (kAmp * panGain));

    std::printf("[MEASURE] fallback transition alias 21100 Hz: before=%.2f dBc, after=%.2f dBc\n",
                beforeDbc, afterDbc);
    // Before: the pre-Phase-4 characteristic (measured -1.1 dBc) — playback keeps
    // running unfiltered rather than blocking while the copy is built.
    t.expect("fallback: unfiltered render while prefilter pending (alias > -8 dBc)", beforeDbc > -8.0,
             "beforeDbc=" + std::to_string(beforeDbc));
    // After: the Phase-4 target on the same session object (measured -104.9 dBc).
    t.expect("fallback: anti-aliased after wait + rebuild (alias < -95 dBc)", afterDbc < -95.0,
             "afterDbc=" + std::to_string(afterDbc));
}

// =============================================================================
// Case 3: impulse through the full session (time-domain spread + identity)
// =============================================================================

void runImpulseCases(AR::CheckSession& t) {
    std::printf("\n--- impulse through the full session ---\n");
    struct ImpPair {
        uint32_t srcRate;
        uint32_t sessionRate;
    };
    for (const auto ip : {ImpPair{96000, 48000}, ImpPair{48000, 96000}}) {
        const std::string name =
            "impulse " + std::to_string(ip.srcRate) + "clip_in_" + std::to_string(ip.sessionRate);
        GA::SessionConfig cfg;
        cfg.sampleRate = ip.sessionRate;
        const uint32_t srcFrames = static_cast<uint32_t>(kClipSeconds * ip.srcRate);
        const uint32_t impPos = ip.srcRate / 2; // 0.5 s into the source
        const AR::Signal clip = AR::makeImpulse(ip.srcRate, srcFrames, impPos, 1.0);
        auto tm = buildSession(name, clip, cfg);
        const uint32_t outClipFrames = static_cast<uint32_t>(kClipSeconds * ip.sessionRate);
        const AR::Signal out =
            renderSessionRealtime(tm, cfg, outClipFrames + 4800, Interp::InterpolationQuality::Sinc64);

        const AR::ImpulseReport r = AR::analyzeImpulse(out, 0);
        const double expectedPeakFrame =
            static_cast<double>(impPos) * ip.sessionRate / ip.srcRate; // = 0.5 s * sessionRate
        std::printf("[MEASURE] %s: peak=%.6f at frame %lld (expect ~%.0f), -60 dB span=%lld frames, "
                    "preRms=%.2e tailRms=%.2e\n",
                    name.c_str(), r.peakAbs, static_cast<long long>(r.peakFrame), expectedPeakFrame,
                    static_cast<long long>(r.spanFrames), r.preSpanRms, r.tailRms);
        // Position stays exact: the Phase-4 prefilter (applied on the 96->48
        // downsampling pair only) compensates its integer group delay exactly.
        t.expect((name + ": impulse lands at the mapped session frame (+/-2)").c_str(),
                 std::abs(static_cast<double>(r.peakFrame) - expectedPeakFrame) <= 2.0,
                 "peakFrame=" + std::to_string(r.peakFrame));
        // Span gate 160 output frames: measured 73 (96->48 — the anti-alias FIR's
        // -60 dB response, the time-domain price of its designed transition; was 1
        // pre-Phase-4) and 79 (48->96, upsampling, unchanged by Phase 4).
        t.expect((name + ": impulse -60 dB span < 160 frames").c_str(), r.spanFrames < 160,
                 "spanFrames=" + std::to_string(r.spanFrames));

        // IDENTITY null against the prefilter+legacy replica (same policy as the
        // probe null; prefilterReplica is the identity for the upsampling pair).
        AR::Signal expected = resampleViaInterpolator(Interp::Sinc64Interpolator::interpolate,
                                                      prefilterReplica(clip, ip.sessionRate), ip.sessionRate);
        for (float& v : expected.samples) {
            v = static_cast<float>(static_cast<double>(v) * PanLaw::kEqualPowerCenterGain);
        }
        const AR::Signal outWin = window(out, kWinSkip, outClipFrames - kWinSkip);
        const AR::Signal expWin = window(expected, kWinSkip, outClipFrames - kWinSkip);
        const AR::DiffReport d = AR::diff(outWin, expWin, 1e-4);
        std::printf("[MEASURE] %s identity null: maxErr=%.3e rmsErr=%.1f dB\n", name.c_str(), d.maxAbsError,
                    d.rmsErrorDb);
        t.expect((name + ": IDENTITY - impulse render nulls against isolated replica").c_str(),
                 d.rmsErrorDb < -120.0 && d.maxAbsError < 1e-5,
                 "rmsErrDb=" + std::to_string(d.rmsErrorDb) + " maxAbs=" + std::to_string(d.maxAbsError));
    }
}

// =============================================================================
// Case 3b: isolated-track bounce — the OTHER offline path, now kernel-unified
// =============================================================================

// bounceRangeToWav(trackId >= 0) renders through AudioRenderer::renderClipAudio.
// Phase 2D found it dispatching Sinc64 -> Sinc64Turbo (measured 87.8 dB SINAD)
// while the full mix used the legacy exact-sinc kernel (149.3 dB) — finding F6.
// Phase 2E unified the dispatch table with renderGraph's, so this case now PROVES
// the unification: a solo bounce must match the full-mix render of the same
// single-track session at mainline quality.
void runIsolatedBounceCase(AR::CheckSession& t, const fs::path& tempRoot) {
    std::printf("\n--- isolated-track bounce (trackId>=0): AudioRenderer path, kernel-unified (2E) ---\n");
    GA::SessionConfig cfg;
    cfg.sampleRate = 48000;
    const uint32_t srcRate = 44100;
    const uint32_t srcFrames = static_cast<uint32_t>(kClipSeconds * srcRate);
    const uint32_t outClipFrames = static_cast<uint32_t>(kClipSeconds * cfg.sampleRate);
    const double durationBeats = kClipSeconds * (static_cast<double>(cfg.bpm) / 60.0);
    const AR::Signal clip = AR::makeSine(srcRate, srcFrames, kToneHz, kAmp);
    auto tm = buildSession("isoBounce1k", clip, cfg);

    AudioEngine engine;
    GA::prepareEngine(engine, tm, cfg);
    engine.setInterpolationQuality(Interp::InterpolationQuality::Sinc64);
    const fs::path outPath = tempRoot / "isolated_bounce.wav";
    std::error_code ec;
    fs::remove(outPath, ec);
    if (!t.expect("isolated bounce: bounceRangeToWav(trackId=0) succeeds",
                  engine.bounceRangeToWav(0.0, durationBeats, outPath.string(), 0))) {
        return;
    }
    AR::Signal bounced;
    bounced.channels = 2;
    uint32_t sr = 0;
    uint32_t ch = 0;
    if (!t.expect("isolated bounce: file decodes",
                  decodeAudioFile(outPath.string(), bounced.samples, sr, ch) && sr == cfg.sampleRate &&
                      ch == 2)) {
        return;
    }
    bounced.sampleRate = sr;
    fs::remove(outPath, ec);

    const uint32_t winEnd = std::min(outClipFrames - kWinSkip, bounced.frames());
    const AR::ToneFit fit = AR::fitTone(bounced, 0, kToneHz, kWinSkip, winEnd);

    // The same content through the mainline full-mix path, for the printed contrast.
    const AR::Signal fullMix =
        renderSessionRealtime(tm, cfg, outClipFrames, Interp::InterpolationQuality::Sinc64);
    const AR::ToneFit fullFit = AR::fitTone(fullMix, 0, kToneHz, kWinSkip, winEnd);

    std::printf("[MEASURE] isolated bounce 44.1->48 1 kHz sinad=%.1f dB vs full-mix path %.1f dB "
                "(unified legacy Sinc64Interpolator; pre-2E bounce measured 87.8 dB)\n",
                fit.sinadDb, fullFit.sinadDb);
    t.expect("isolated bounce: output is present and level-true (fit amplitude)",
             std::abs(AR::toDb(fit.amplitude / (kAmp * PanLaw::kEqualPowerCenterGain))) < 0.1,
             "ampDb=" + std::to_string(AR::toDb(fit.amplitude / (kAmp * PanLaw::kEqualPowerCenterGain))));
    // UNIFIED (Phase 2E): the bounce must deliver mainline-class quality, not the
    // pre-unification Turbo floor (was 87.8 dB; mainline measured 146.5-153.9 dB).
    t.expect("isolated bounce: 1 kHz residual SINAD > 140 dB (mainline kernel)", fit.sinadDb > 140.0,
             "sinadDb=" + std::to_string(fit.sinadDb));
    // Agreement with the full-mix path for the same single-track session.
    t.expectNear("isolated bounce: SINAD agrees with full-mix path (dB)", fit.sinadDb, fullFit.sinadDb,
                 3.0);
    // Strongest form: the bounced file nulls against the full-mix realtime render on
    // the interior window (same clip, same kernel, same gain staging).
    const AR::Signal bWin = window(bounced, kWinSkip, winEnd);
    const AR::Signal fWin = window(fullMix, kWinSkip, winEnd);
    const AR::DiffReport d = AR::diff(bWin, fWin, 1e-6);
    std::printf("[MEASURE] isolated bounce null vs full-mix realtime: maxErr=%.3e rmsErr=%.1f dB\n",
                d.maxAbsError, d.rmsErrorDb);
    if (!t.expect("isolated bounce: nulls against the full-mix realtime render",
                  d.rmsErrorDb < -120.0 && d.maxAbsError < 1e-5,
                  "rmsErrDb=" + std::to_string(d.rmsErrorDb) +
                      " maxAbs=" + std::to_string(d.maxAbsError))) {
        AR::printDiffForensics("isolated bounce vs full mix", d, bWin, fWin);
    }
}

// =============================================================================
// Case 4: offline export parity (bounceRangeToWav) for cross-rate sessions
// =============================================================================

void runExportParityCases(AR::CheckSession& t, const fs::path& tempRoot) {
    std::printf("\n--- offline export vs realtime, cross-rate sessions ---\n");
    for (const SessionPair& p : kSessionPairs) {
        if (p.srcRate != 44100 && p.sessionRate != 44100) {
            continue; // export the two 44.1<->48 pairs; 96k pairs exercise the same code
        }
        const std::string name = "export " + pairName(p);
        GA::SessionConfig cfg;
        cfg.sampleRate = p.sessionRate;
        const uint32_t srcFrames = static_cast<uint32_t>(kClipSeconds * p.srcRate);
        const uint32_t outClipFrames = static_cast<uint32_t>(kClipSeconds * p.sessionRate);
        const double durationBeats = kClipSeconds * (static_cast<double>(cfg.bpm) / 60.0);
        const AR::Signal clip = AR::makeSine(p.srcRate, srcFrames, p.probeHz, kAmp);
        auto tm = buildSession(name, clip, cfg);

        // Realtime reference render.
        const AR::Signal realtime =
            renderSessionRealtime(tm, cfg, outClipFrames, Interp::InterpolationQuality::Sinc64);

        // Offline export through AudioExporter (4096-frame blocks, Float_32 WAV).
        const fs::path outPath = tempRoot / (pairName(p) + "_export.wav");
        std::error_code ec;
        fs::remove(outPath, ec);
        AudioEngine engine;
        GA::prepareEngine(engine, tm, cfg);
        engine.setInterpolationQuality(Interp::InterpolationQuality::Sinc64);
        if (!t.expect((name + ": bounceRangeToWav succeeds").c_str(),
                      engine.bounceRangeToWav(0.0, durationBeats, outPath.string(), -1))) {
            continue;
        }
        AR::Signal exported;
        exported.channels = 2;
        uint32_t sr = 0;
        uint32_t ch = 0;
        if (!t.expect((name + ": exported file decodes").c_str(),
                      decodeAudioFile(outPath.string(), exported.samples, sr, ch) && sr == p.sessionRate &&
                          ch == 2)) {
            continue;
        }
        exported.sampleRate = sr;

        // Parity on the interior window (skips the transport-start difference the
        // same way RealtimeExportParityTest trims its head).
        const uint32_t winEnd = std::min(outClipFrames - kWinSkip, std::min(realtime.frames(), exported.frames()));
        const AR::Signal rtWin = window(realtime, kWinSkip, winEnd);
        const AR::Signal exWin = window(exported, kWinSkip, winEnd);
        const AR::DiffReport d = AR::diff(rtWin, exWin, 1e-6);
        std::printf("[MEASURE] %s parity: rtFrames=%u exFrames=%u maxErr=%.3e rmsErr=%.1f dB\n", name.c_str(),
                    realtime.frames(), exported.frames(), d.maxAbsError, d.rmsErrorDb);
        // Gate -120 dB / 1e-6 (same as RealtimeExportParityTest): the exporter drives
        // the same processBlock; phase is recomputed per block from the clip-relative
        // closed form (AudioRenderer.cpp:189), so the 4096-frame export block size must
        // not change a single sample.
        if (!t.expect((name + ": export == realtime (cross-rate parity)").c_str(),
                      d.rmsErrorDb <= -120.0 && d.maxAbsError <= 1e-6,
                      "rmsErrDb=" + std::to_string(d.rmsErrorDb) +
                          " maxAbs=" + std::to_string(d.maxAbsError))) {
            AR::printDiffForensics(name + " parity", d, rtWin, exWin);
        }

        // The artifact level in the exported FILE (what the user's bounced audio
        // actually contains). Must match the realtime measurement to 0.1 dB.
        const double panGain = static_cast<double>(PanLaw::kEqualPowerCenterGain);
        const double exArtifact =
            AR::toDb(AR::toneAmplitude(exported, 0, p.artifactHz, kWinSkip, winEnd) / (kAmp * panGain));
        const double rtArtifact =
            AR::toDb(AR::toneAmplitude(realtime, 0, p.artifactHz, kWinSkip, winEnd) / (kAmp * panGain));
        std::printf("[MEASURE] %s artifact in exported file: %.2f dBc (realtime %.2f dBc)\n", name.c_str(),
                    exArtifact, rtArtifact);
        t.expectNear((name + ": exported artifact level == realtime (dB)").c_str(), exArtifact, rtArtifact,
                     0.1);
        fs::remove(outPath, ec);
    }
}

// =============================================================================
// Case 5: preview path (PreviewEngine) — its own Cubic resampler
// =============================================================================

/// Pump the preview exactly like the device callback: play(), process until the
/// audibility latch sets, then capture. Returns empty on timeout.
AR::Signal capturePreview(const fs::path& wavPath, uint32_t outputRate, uint32_t captureFrames) {
    PreviewEngine preview;
    preview.setOutputSampleRate(static_cast<double>(outputRate));
    preview.play(wavPath.string(), 0.0f, 30.0);

    constexpr uint32_t kBlock = 512;
    std::vector<float> block(static_cast<size_t>(kBlock) * 2, 0.0f);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!preview.isAudiblyPlaying() && std::chrono::steady_clock::now() < deadline) {
        std::fill(block.begin(), block.end(), 0.0f);
        preview.processRealtime(block.data(), kBlock, 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    AR::Signal out;
    out.sampleRate = outputRate;
    out.channels = 2;
    if (!preview.isAudiblyPlaying()) {
        return out;
    }
    out.samples.reserve(static_cast<size_t>(captureFrames) * 2);
    uint32_t captured = 0;
    while (captured < captureFrames) {
        std::fill(block.begin(), block.end(), 0.0f);
        preview.processRealtime(block.data(), kBlock, 2);
        out.samples.insert(out.samples.end(), block.begin(), block.end());
        captured += kBlock;
    }
    return out;
}

void runPreviewCases(AR::CheckSession& t, const fs::path& tempRoot) {
    std::printf("\n--- preview path (PreviewEngine: per-voice Cubic Hermite resampler) ---\n");
    std::printf("    NOTE: this path ignores the user's Resampling quality setting entirely\n");
    struct PreviewCase {
        uint32_t srcRate;
        double probeHz;
        double artifactHz;
        bool downsampling;
        double ceilingDbc; // regression ceiling (calibrated from this test's measurement)
    };
    // Output is always 48k here (typical device rate). Measured -7.16 dBc vs the
    // delivered level — matching CubicInterpolator's isolated Phase-1 contrast
    // (-7.2 dBc), i.e. the preview's own SIMD Catmull-Rom behaves like the engine's
    // Cubic tier. Ceiling -4 leaves ~3 dB margin.
    const PreviewCase cases[] = {
        {44100, 21000.0, 23100.0, false, -4.0},
        {96000, 30000.0, 18000.0, true, 1.0},
    };
    constexpr uint32_t kOutRate = 48000;
    for (const PreviewCase& c : cases) {
        const std::string name = "preview " + std::to_string(c.srcRate) + "->48000";
        const uint32_t srcFrames = c.srcRate * 3; // 3 s: headroom for async decode spin-up
        const AR::Signal tone = AR::makeSine(c.srcRate, srcFrames, c.probeHz, kAmp);
        const fs::path wav = tempRoot / ("preview_" + std::to_string(c.srcRate) + ".wav");
        if (!t.expect((name + ": probe wav written").c_str(),
                      writeWavFloat32(wav, tone.samples, 2, c.srcRate))) {
            continue;
        }
        const AR::Signal out = capturePreview(wav, kOutRate, kOutRate); // capture 1 s
        if (!t.expect((name + ": preview reached audible state").c_str(), out.frames() > 0)) {
            continue;
        }
        // Skip the preview fade-in, measure the steady interior.
        const uint32_t winStart = 4800;
        const uint32_t winEnd = out.frames() - 256;
        // dBc reference: the DELIVERED input level. PreviewEngine applies
        // gain(0 dB) * PanLaw::kEqualPowerCenterGain (PreviewEngine.cpp:292), so a
        // full-level probe arrives at kAmp * 0.7071. Normalizing by the measured
        // primary instead would inflate the image figure by Cubic's own HF droop
        // (~5 dB at 21 kHz) and stop matching Phase 1's dBc-vs-input convention.
        const double deliveredAmp = kAmp * static_cast<double>(PanLaw::kEqualPowerCenterGain);
        const double primaryHz = c.downsampling ? c.artifactHz : c.probeHz;
        const double primaryAmp = AR::toneAmplitude(out, 0, primaryHz, winStart, winEnd);
        const double artifactAmp = AR::toneAmplitude(out, 0, c.artifactHz, winStart, winEnd);
        const double outRms = AR::rms(out, 0, winStart, winEnd);
        if (c.downsampling) {
            // The between-Nyquists probe has NO in-band primary: whatever lands at the
            // alias frequency is pure artifact. Express it against the delivered level
            // a passband tone of the same amplitude would have (rms*sqrt2 of output).
            const double aliasVsFullDb = AR::toDb(artifactAmp / (outRms * std::sqrt(2.0)));
            std::printf("[MEASURE] %s probe %.0f Hz: alias at %.0f Hz = %.6f amp (%.2f dB vs delivered RMS "
                        "level)\n",
                        name.c_str(), c.probeHz, c.artifactHz, artifactAmp, aliasVsFullDb);
            // KNOWN LIMITATION (characterization): no anti-aliasing on the preview
            // path either — the folded tone IS the delivered signal.
            t.expect((name + ": KNOWN LIMITATION - preview downsampling aliases (alias is dominant)").c_str(),
                     artifactAmp > 0.5 * outRms * std::sqrt(2.0),
                     "aliasAmp=" + std::to_string(artifactAmp) + " rms=" + std::to_string(outRms));
        } else {
            const double imageDbc = AR::toDb(artifactAmp / deliveredAmp);
            std::printf("[MEASURE] %s probe %.0f Hz: primary=%.6f (droop %.2f dB), image at %.0f Hz = "
                        "%.2f dBc vs delivered level\n",
                        name.c_str(), c.probeHz, primaryAmp, AR::toDb(primaryAmp / deliveredAmp),
                        c.artifactHz, imageDbc);
            t.expect((name + ": image not above regression ceiling").c_str(), imageDbc < c.ceilingDbc,
                     "imageDbc=" + std::to_string(imageDbc) + " ceiling=" + std::to_string(c.ceilingDbc));
        }
        std::error_code ec;
        fs::remove(wav, ec);
    }

    // Preview 1 kHz residual (documentation number: the Cubic kernel's floor).
    {
        const uint32_t srcRate = 44100;
        const AR::Signal tone = AR::makeSine(srcRate, srcRate * 3, kToneHz, kAmp);
        const fs::path wav = tempRoot / "preview_1k.wav";
        if (writeWavFloat32(wav, tone.samples, 2, srcRate)) {
            const AR::Signal out = capturePreview(wav, kOutRate, kOutRate);
            if (out.frames() > 0) {
                const AR::ToneFit fit = AR::fitTone(out, 0, kToneHz, 4800, out.frames() - 256);
                std::printf("[MEASURE] preview 44.1->48 1 kHz sinad=%.1f dB (Cubic; isolated Phase-1 "
                            "contrast: 89.5 dB)\n",
                            fit.sinadDb);
                // Gate 80 dB: isolated Cubic measured 89.5 dB at 1 kHz (Phase 1).
                t.expect("preview: 1 kHz residual SINAD > 80 dB", fit.sinadDb > 80.0,
                         "sinadDb=" + std::to_string(fit.sinadDb));
            }
            std::error_code ec;
            fs::remove(wav, ec);
        }
    }
}

// =============================================================================
// Case 6: SamplerPlugin pitch-down (Sinc64Turbo via Interpolators, fixed quality)
// =============================================================================

AR::Signal renderSamplerNote(const fs::path& wavPath, uint8_t note, uint32_t renderFrames) {
    constexpr uint32_t kRate = 48000;
    constexpr uint32_t kBlock = 512;
    AR::Signal out;
    out.sampleRate = kRate;
    out.channels = 2;

    Plugins::SamplerPlugin sampler;
    if (!sampler.initialize(kRate, kBlock) || !sampler.loadSample(wavPath.string())) {
        return out;
    }
    sampler.setRootMidiNote(60);
    sampler.setMaxVoices(4);
    sampler.setEnvelope(0.001f, 0.001f, 1.0f, 0.05f); // ~rectangular: attack 1 ms, sustain 1.0
    sampler.activate();

    std::vector<float> left(kBlock, 0.0f);
    std::vector<float> right(kBlock, 0.0f);
    float* outputs[] = {left.data(), right.data()};
    MidiBuffer midi;
    midi.addNoteOn(1, note, 110, 0);

    out.samples.reserve(static_cast<size_t>(renderFrames) * 2);
    uint32_t rendered = 0;
    bool first = true;
    while (rendered < renderFrames) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        sampler.process(nullptr, outputs, 0, 2, kBlock, first ? &midi : nullptr, nullptr);
        first = false;
        for (uint32_t i = 0; i < kBlock; ++i) {
            out.samples.push_back(left[i]);
            out.samples.push_back(right[i]);
        }
        rendered += kBlock;
    }
    return out;
}

void runSamplerCases(AR::CheckSession& t, const fs::path& tempRoot) {
    std::printf("\n--- sampler path (SamplerPlugin, Sinc64Turbo fixed) ---\n");
    constexpr uint32_t kRate = 48000;
    const double probeHz = 21600.0; // 0.9 * Nyquist in the 48k source
    const AR::Signal probe = AR::makeSine(kRate, kRate * 3, probeHz, kAmp);
    const fs::path wav = tempRoot / "sampler_probe.wav";
    if (!t.expect("sampler: probe wav written", writeWavFloat32(wav, probe.samples, 2, kRate))) {
        return;
    }

    // Pitch-down 5 semitones: ratio 2^(-5/12) ~ 0.74915. The voice reads the source
    // slower — an upsampling-shaped ratio, so source images are the artifact.
    {
        const double ratio = std::pow(2.0, -5.0 / 12.0);
        const double primaryHz = probeHz * ratio;                    // ~16181.7 Hz
        const double imageHz = (kRate - probeHz) * ratio;            // ~19777.7 Hz
        const AR::Signal out = renderSamplerNote(wav, 55, kRate * 2); // 2 s
        if (!t.expect("sampler: pitch-down render produced audio", AR::peak(out) > 1e-3)) {
            return;
        }
        const uint32_t winStart = 9600; // skip note attack + kernel spin-up
        const uint32_t winEnd = out.frames() - 4800;
        const double primaryAmp = AR::toneAmplitude(out, 0, primaryHz, winStart, winEnd);
        const double imageAmp = AR::toneAmplitude(out, 0, imageHz, winStart, winEnd);
        const double imageDbc = AR::toDb(imageAmp / std::max(primaryAmp, 1e-12));
        std::printf("[MEASURE] sampler -5st: primary %.1f Hz amp=%.6f, image %.1f Hz = %.2f dBc\n", primaryHz,
                    primaryAmp, imageHz, imageDbc);
        // The tone must land at the equal-tempered frequency (pins the ratio math).
        t.expect("sampler: pitched-down primary present at 2^(-5/12) * probe", primaryAmp > 0.05,
                 "primaryAmp=" + std::to_string(primaryAmp));
        // Regression ceiling -60 dBc: measured -66.5 dBc (same mechanism class as the
        // isolated 0.9-Nyquist Sinc64 image measurements, e.g. -60.5 dBc at 48->96).
        t.expect("sampler: pitch-down image not above ceiling (-60 dBc)", imageDbc < -60.0,
                 "imageDbc=" + std::to_string(imageDbc));

        // 1 kHz fractional-ratio SINAD through the sampler (documentation number).
        const AR::Signal tone1k = AR::makeSine(kRate, kRate * 3, kToneHz, kAmp);
        const fs::path wav1k = tempRoot / "sampler_1k.wav";
        if (writeWavFloat32(wav1k, tone1k.samples, 2, kRate)) {
            const AR::Signal o = renderSamplerNote(wav1k, 55, kRate * 2);
            const AR::ToneFit fit = AR::fitTone(o, 0, kToneHz * ratio, winStart, o.frames() - 4800);
            std::printf("[MEASURE] sampler -5st 1 kHz->%.1f Hz sinad=%.1f dB (Sinc64Turbo path; isolated "
                        "fractional floor 87.8-89.9 dB)\n",
                        kToneHz * ratio, fit.sinadDb);
            // Gate 80 dB: measured 84.2 dB — Turbo-class (the sampler dispatches to
            // Sinc64Turbo at SamplerPlugin.cpp:402), slightly under the isolated floor
            // because the -5 st ratio lands on different LUT phases than 44.1/48 pairs.
            t.expect("sampler: fractional-ratio residual SINAD > 80 dB", fit.sinadDb > 80.0,
                     "sinadDb=" + std::to_string(fit.sinadDb));
            std::error_code ec;
            fs::remove(wav1k, ec);
        }
    }

    // Pitch-UP contrast (documentation number, no gate — mission scope is pitch-down):
    // +7 st pushes the 21.6 kHz probe to 32.36 kHz > Nyquist; with no ratio-aware
    // low-pass it folds to 48000 - 32359 ~ 15641 Hz.
    {
        const double ratio = std::pow(2.0, 7.0 / 12.0);
        const double foldedHz = kRate - probeHz * ratio; // folds around Nyquist
        const AR::Signal out = renderSamplerNote(wav, 67, kRate * 2);
        if (out.frames() > 0 && AR::peak(out) > 1e-3) {
            const double foldAmp = AR::toneAmplitude(out, 0, std::abs(foldedHz), 9600, out.frames() - 4800);
            const double bodyRms = AR::rms(out, 0, 9600, out.frames() - 4800);
            std::printf("[MEASURE] sampler +7st: probe maps above Nyquist, folded tone at %.1f Hz amp=%.6f "
                        "(delivered RMS %.6f) — aliasing, not gated (see doc)\n",
                        std::abs(foldedHz), foldAmp, bodyRms);
        }
    }
    std::error_code ec;
    fs::remove(wav, ec);
}

// =============================================================================
// Engine-default contrast: a fresh AudioEngine defaults to Cubic until the app
// applies the user's settings (AudioEngine.h m_interpQuality{Cubic}). One
// documentation measurement so the doc can state what that state sounds like.
// =============================================================================

void runEngineDefaultContrast(AR::CheckSession& t) {
    std::printf("\n--- engine-default (Cubic) contrast, 44.1k clip in 48k session ---\n");
    const SessionPair& p = kSessionPairs[0];
    GA::SessionConfig cfg;
    cfg.sampleRate = p.sessionRate;
    const uint32_t srcFrames = static_cast<uint32_t>(kClipSeconds * p.srcRate);
    const uint32_t outClipFrames = static_cast<uint32_t>(kClipSeconds * p.sessionRate);
    const AR::Signal clip = AR::makeSine(p.srcRate, srcFrames, p.probeHz, kAmp);
    auto tm = buildSession("cubicContrast", clip, cfg);
    const AR::Signal out =
        renderSessionRealtime(tm, cfg, outClipFrames + 4800, Interp::InterpolationQuality::Cubic);
    const double panGain = static_cast<double>(PanLaw::kEqualPowerCenterGain);
    const double artifactDbc =
        AR::toDb(AR::toneAmplitude(out, 0, p.artifactHz, kWinSkip, outClipFrames - kWinSkip) / (kAmp * panGain));
    std::printf("[MEASURE] session Cubic 44.1->48 image at %.0f Hz = %.2f dBc (isolated Phase-1: -7.2 dBc, "
                "measured session: -7.17 dBc — engine Cubic == CubicInterpolator on both paths)\n",
                p.artifactHz, artifactDbc);
    // One-sided ceiling (measured -7.17 dBc + ~3 dB margin): fails only if the
    // engine-default tier gets dirtier; an improved Cubic tier passes.
    t.expect("engine-default Cubic: image not above -4 dBc ceiling", artifactDbc < -4.0,
             "imageDbc=" + std::to_string(artifactDbc));
}

} // namespace

int main() {
    std::printf("============================================================\n");
    std::printf("  Aestra Audio Research Bench — Session Resampling Truth\n");
    std::printf("  (Phase 2D: the shipped session/render/export paths)\n");
    std::printf("============================================================\n");

    const fs::path tempRoot = fs::temp_directory_path() / "aestra_session_truth";
    std::error_code ec;
    fs::create_directories(tempRoot, ec);

    AR::CheckSession t;
    const ControlBaseline control = runControlCase(t);
    runDcCases(t);
    for (const SessionPair& p : kSessionPairs) {
        auditSessionPair(t, p, control);
    }
    runFallbackTransitionCase(t);
    runImpulseCases(t);
    runIsolatedBounceCase(t, tempRoot);
    runExportParityCases(t, tempRoot);
    runPreviewCases(t, tempRoot);
    runSamplerCases(t, tempRoot);
    runEngineDefaultContrast(t);

    fs::remove_all(tempRoot, ec);
    return t.exitCode();
}
