// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ResamplerQualityAuditTest — scientific audit of Aestra's resampling behavior.
//
// Two distinct resampling engines exist in AestraAudio and this test measures BOTH:
//
//   1. The PRODUCTION clip-playback path: the Interpolators family (Cubic /
//      Sinc64Turbo, etc.) driven by a `phase += ratio` accumulator. This is what
//      AudioRenderer.cpp (isolated-track bounce), AuditionEngine.cpp and
//      SamplerPlugin.cpp actually run. (Phase 2D correction: the MAINLINE session
//      clip loop in AudioEngine::renderGraph dispatches Sinc64 to the legacy
//      exact-sinc Sinc64Interpolator instead — measured end-to-end by
//      SessionResamplingTruthTest; see AestraDocs/audio-research-bench.md §8.)
//      The kernel cutoff sits at the SOURCE Nyquist and there is no
//      ratio-aware anti-alias filtering: when downsampling, content between the two
//      Nyquist frequencies is EXPECTED to alias. The audit pins that behavior as a
//      measured characteristic (see AestraDocs/audio-research-bench.md), it does not
//      bless it as ideal.
//
//   2. The streaming SampleRateConverter (polyphase, ratio-aware cutoff — it
//      anti-aliases when downsampling). As of this audit it has NO production call
//      sites (tests/benchmarks only); it is measured here as the in-repo reference
//      and to extend ResamplerWarTest's single 48->44.1 coverage to a 5-pair matrix.
//
// Rate matrix: 44.1->48, 48->44.1, 48->96, 96->48, 44.1->96 (kHz).
// Signals: DC, 1 kHz sine, near-Nyquist sine (pair-specific imaging/alias probes),
// impulse, transient burst, plus a 44.1->48->44.1 round trip.
//
// Every gate threshold cites the measurement that justified it. Numbers printed
// with [MEASURE] are the raw audit data quoted in the research doc.
//
// Optional diagnostics: when libsamplerate/soxr are present at configure time the
// same probes run through them and print (no gating) for external context.

#include "AudioMeasure.h"
#include "SignalLab.h"

#include "DSP/Interpolators.h"
#include "DSP/SampleRateConverter.h"

#ifdef AESTRA_HAS_LIBSAMPLERATE
#include <samplerate.h>
#endif
#ifdef AESTRA_HAS_SOXR
#include <soxr.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace AudioResearch;
using Aestra::Audio::SampleRateConverter;
using Aestra::Audio::SRCQuality;
namespace Interp = Aestra::Audio::Interpolators;

namespace {

// =============================================================================
// Harnesses
// =============================================================================

using InterpFn = void (*)(const float*, int64_t, double, float&, float&);

/// Replicates the production clip-resampling loop from AudioRenderer.cpp
/// (interpolateFunc dispatch + `phase += ratio` accumulator): output frame n reads
/// source position n * (srcRate/dstRate). Zero latency by construction; output
/// length is caller-derived, as it is in the engine (from clip start/end samples).
Signal resampleViaInterpolator(InterpFn fn, const Signal& src, uint32_t dstRate) {
    Signal out;
    out.sampleRate = dstRate;
    out.channels = 2;
    const double ratio = static_cast<double>(src.sampleRate) / static_cast<double>(dstRate);
    const uint32_t outFrames = static_cast<uint32_t>(
        std::floor(static_cast<double>(src.frames()) * dstRate / src.sampleRate));
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

struct StreamResult {
    Signal out;
    uint32_t written{0};
    uint32_t latency{0};
};

/// Single-shot pass through the streaming polyphase SampleRateConverter.
StreamResult resampleViaStreamingSRC(const Signal& src, uint32_t dstRate, SRCQuality quality) {
    StreamResult r;
    SampleRateConverter converter;
    converter.configure(src.sampleRate, dstRate, 2, quality);
    r.latency = converter.getLatency();
    const uint32_t maxOut = Aestra::Audio::estimateOutputFrames(src.frames(), src.sampleRate, dstRate, r.latency);
    r.out.sampleRate = dstRate;
    r.out.channels = 2;
    r.out.samples.resize(static_cast<size_t>(maxOut) * 2);
    r.written = converter.process(src.samples.data(), src.frames(), r.out.samples.data(), maxOut);
    r.out.samples.resize(static_cast<size_t>(r.written) * 2);
    return r;
}

// =============================================================================
// Rate-pair matrix
// =============================================================================

struct RatePair {
    uint32_t srcRate;
    uint32_t dstRate;
    // Near-Nyquist probe. For upsampling pairs the tone is inside the source band and
    // its first spectral image (srcRate - probe) must land BELOW the output Nyquist so
    // it is directly measurable. For downsampling pairs the tone sits between the two
    // Nyquists and folds to (dstRate - probe) in the output.
    double probeHz;
    double artifactHz;         // expected image/alias frequency in the OUTPUT
    bool downsampling;         // true -> probe aliases; false -> probe images
    // Evidence-based regression ceilings (measured 2026-07-07, this test, Release
    // x86-64; ~3-6 dB margin over the measured artifact level quoted alongside).
    double interpArtifactGateDbc;  // production Interpolators path
    double streamArtifactGateDbc;  // streaming SampleRateConverter
};

// NOTE on probe placement: for 44.1->48 the image is only observable below the output
// Nyquist if the probe sits above 20.1 kHz, i.e. at 0.952 of the source Nyquist —
// inside the kernel's transition band. Its high measured image level (-21.7 dBc)
// reflects that placement; the 0.9-Nyquist probes of the 96 kHz pairs sit lower and
// measure the stopband instead. Same-probe references for context: libsamplerate
// SINC_BEST -73.7 dBc, soxr VHQ -78.4 dBc (printed by this test when available).
const RatePair kPairs[] = {
    // src     dst     probe    artifact  down   interpGate streamGate  (measured: interp / stream)
    {44100, 48000, 21000.0, 23100.0, false, -18.0, -38.0}, // -21.7 / -41.4 dBc
    {48000, 44100, 23000.0, 21100.0, true, -6.0, -52.0},   // -1.1  / -57.8 dBc
    {48000, 96000, 21600.0, 26400.0, false, -55.0, -66.0}, // -60.5 / -72.5 dBc
    {96000, 48000, 30000.0, 18000.0, true, -6.0, -110.0},  // -0.0  / -117.1 dBc
    {44100, 96000, 19845.0, 24255.0, false, -62.0, -44.0}, // -68.2 / -47.6 dBc
};

std::string pairName(const RatePair& p) {
    return std::to_string(p.srcRate) + "->" + std::to_string(p.dstRate);
}

// Analysis windows skip the buffer edges where the interpolation kernel is starved
// of history (64-tap kernel -> 32 frames; 256 is a generous margin at any rate here).
constexpr uint32_t kEdgeSkip = 256;
constexpr double kAmp = 0.5;
constexpr double kToneHz = 1000.0;

// =============================================================================
// Production interpolator-path audit
// =============================================================================

void auditInterpolatorPair(CheckSession& t, const RatePair& p) {
    const std::string name = "interp(Sinc64) " + pairName(p);
    std::printf("\n--- production interpolator path: %s ---\n", pairName(p).c_str());
    const uint32_t srcFrames = p.srcRate; // 1 second
    const InterpFn sinc64 = Interp::Sinc64Turbo::interpolate;

    // DC preservation. Gate 1e-6: measured worst-case DC error across all five pairs
    // is 4.5e-9 (44.1->48); 1e-6 leaves >100x margin while still catching any real
    // kernel-normalization regression.
    {
        const Signal out = resampleViaInterpolator(sinc64, makeDC(p.srcRate, srcFrames, kAmp), p.dstRate);
        const double dc = dcOffset(out, 0, kEdgeSkip, out.frames() - kEdgeSkip);
        std::printf("[MEASURE] %s DC out=%.9f (in %.1f)\n", name.c_str(), dc, kAmp);
        t.expectNear((name + ": DC preserved").c_str(), dc, kAmp, 1e-6);
    }

    // Passband level accuracy at 1 kHz. Gate 0.05 dB: measured worst-case gain error
    // across the matrix is < 1e-5 dB; 0.05 dB is the audibility-motivated ceiling.
    {
        const Signal out =
            resampleViaInterpolator(sinc64, makeSine(p.srcRate, srcFrames, kToneHz, kAmp), p.dstRate);
        const ToneFit fit = fitTone(out, 0, kToneHz, kEdgeSkip, out.frames() - kEdgeSkip);
        const double gainDb = toDb(fit.amplitude / kAmp);
        std::printf("[MEASURE] %s 1kHz amp=%.9f gainErr=%.6f dB sinad=%.1f dB\n", name.c_str(), fit.amplitude,
                    gainDb, fit.sinadDb);
        t.expect((name + ": 1 kHz gain within 0.05 dB").c_str(), std::abs(gainDb) < 0.05,
                 "gainErrDb=" + std::to_string(gainDb));
        // Full-band residual for a clean passband tone. Gate 84 dB: measured 87.8-89.9
        // dB at fractional ratios and 154.2 dB at exact 2:1 (96->48, where every output
        // sample lands on an input sample). The fractional-ratio floor matches
        // Sinc64Turbo's 2048-phase nearest-phase LUT quantization (worst phase error
        // 1/4096 sample -> ~ -89 dB at 1 kHz), NOT the header's "~144dB SNR" figure,
        // which describes the kernel stopband. See AestraDocs/audio-research-bench.md.
        t.expect((name + ": 1 kHz residual SINAD > 84 dB").c_str(), fit.sinadDb > 84.0,
                 "sinadDb=" + std::to_string(fit.sinadDb));
    }

    // Near-Nyquist probe: imaging (upsampling) or aliasing (downsampling).
    {
        const Signal out =
            resampleViaInterpolator(sinc64, makeSine(p.srcRate, srcFrames, p.probeHz, kAmp), p.dstRate);
        const double probeAmp = toneAmplitude(out, 0, p.probeHz < 0.5 * p.dstRate ? p.probeHz : p.artifactHz,
                                              kEdgeSkip, out.frames() - kEdgeSkip);
        const double artifactAmp = toneAmplitude(out, 0, p.artifactHz, kEdgeSkip, out.frames() - kEdgeSkip);
        const double artifactDb = toDb(artifactAmp / kAmp);
        std::printf("[MEASURE] %s probe %.0f Hz -> artifact %.0f Hz: probeAmp=%.6f artifact=%.6f (%.1f dBc)\n",
                    name.c_str(), p.probeHz, p.artifactHz, probeAmp, artifactAmp, artifactDb);
        if (p.downsampling) {
            // CHARACTERIZATION, not endorsement: no ratio-aware anti-alias filter on
            // this path, so the between-Nyquists tone folds back at high level.
            // Measured: -1.1 dBc (48->44.1) and -0.0 dBc (96->48). The gate pins the
            // behavior so an (intentional) future anti-aliasing change must update
            // this test + the research doc together.
            t.expect((name + ": KNOWN LIMITATION - downsampling aliases near full scale").c_str(),
                     artifactDb > p.interpArtifactGateDbc, "aliasDbc=" + std::to_string(artifactDb));
        } else {
            // Imaging rejection comes from the 64-tap kernel; the ceiling is per-pair
            // because the 44.1->48 probe sits in the transition band (see kPairs note).
            // Measured: -21.7 (44.1->48), -60.5 (48->96), -68.2 (44.1->96) dBc.
            t.expect((name + ": upsampling image below per-pair ceiling").c_str(),
                     artifactDb < p.interpArtifactGateDbc,
                     "imageDbc=" + std::to_string(artifactDb) +
                         " gate=" + std::to_string(p.interpArtifactGateDbc));
        }
    }

    // Impulse response: position, spread, tail.
    {
        const uint32_t impPos = 1000;
        const Signal out =
            resampleViaInterpolator(sinc64, makeImpulse(p.srcRate, srcFrames, impPos, 1.0), p.dstRate);
        const ImpulseReport r = analyzeImpulse(out, 0);
        const double expectedPeak = static_cast<double>(impPos) * p.dstRate / p.srcRate;
        std::printf("[MEASURE] %s impulse peak=%.6f at frame %lld (expect ~%.1f), span=%lld frames "
                    "(-60 dB), preRms=%.2e tailRms=%.2e\n",
                    name.c_str(), r.peakAbs, static_cast<long long>(r.peakFrame), expectedPeak,
                    static_cast<long long>(r.spanFrames), r.preSpanRms, r.tailRms);
        t.expect((name + ": impulse lands at the mapped position (+/-2)").c_str(),
                 std::abs(static_cast<double>(r.peakFrame) - expectedPeak) <= 2.0,
                 "peakFrame=" + std::to_string(r.peakFrame));
        // Span gate 160: measured -60 dB spans are 1 output frame (96->48, exact 2:1 —
        // outputs coincide with inputs) up to 86 output frames (44.1->96).
        t.expect((name + ": impulse -60 dB span < 160 frames").c_str(), r.spanFrames < 160,
                 "spanFrames=" + std::to_string(r.spanFrames));
        // Tail gate 1e-4: measured tail RMS beyond the span is <= 5.3e-6.
        t.expect((name + ": impulse tail RMS < 1e-4").c_str(), r.tailRms < 1e-4,
                 "tailRms=" + std::to_string(r.tailRms));
    }

    // Transient burst: no energy before the mapped burst start (no acausal smear
    // beyond the kernel half-width), body level preserved.
    {
        const uint32_t burstStart = 8000;
        const uint32_t burstLen = 4800;
        const Signal out = resampleViaInterpolator(
            sinc64, makeTransientBurst(p.srcRate, srcFrames, burstStart, burstLen, kToneHz, kAmp), p.dstRate);
        const double scale = static_cast<double>(p.dstRate) / p.srcRate;
        const uint32_t outStart = static_cast<uint32_t>(burstStart * scale);
        const uint32_t outEnd = static_cast<uint32_t>((burstStart + burstLen) * scale);
        const uint32_t guard = 64; // kernel half-width (32) + margin
        const double preRms = rms(out, -1, 0, outStart - guard);
        const double bodyRms = rms(out, -1, outStart + guard, outEnd - guard);
        std::printf("[MEASURE] %s burst preRms=%.2e bodyRms=%.6f (expect ~%.6f)\n", name.c_str(), preRms,
                    bodyRms, kAmp / std::sqrt(2.0));
        // Pre-smear gate 1e-6: measured 0.0 exactly on four pairs and 3.1e-10 on
        // 44.1->96 (kernel support is bounded, so silence stays essentially silent
        // outside the half-width guard).
        t.expect((name + ": no pre-burst smear beyond kernel half-width").c_str(), preRms < 1e-6,
                 "preRms=" + std::to_string(preRms));
        t.expectNear((name + ": burst body RMS preserved").c_str(), bodyRms, kAmp / std::sqrt(2.0), 2e-3);
    }
}

void auditInterpolatorRoundTrip(CheckSession& t) {
    std::printf("\n--- production interpolator path: 44.1k -> 48k -> 44.1k round trip ---\n");
    const InterpFn sinc64 = Interp::Sinc64Turbo::interpolate;
    const Signal original = makeSine(44100, 44100, kToneHz, kAmp);
    const Signal up = resampleViaInterpolator(sinc64, original, 48000);
    const Signal back = resampleViaInterpolator(sinc64, up, 44100);

    Signal origTrim = original;
    origTrim.samples.resize(back.samples.size());
    const DiffReport d = diff(back, origTrim, 1e-3);
    // Interior-only view (edges are kernel-starved on both passes).
    Signal backInner = back;
    Signal origInner = origTrim;
    backInner.samples.assign(back.samples.begin() + kEdgeSkip * 2, back.samples.end() - kEdgeSkip * 2);
    origInner.samples.assign(origTrim.samples.begin() + kEdgeSkip * 2, origTrim.samples.end() - kEdgeSkip * 2);
    const DiffReport inner = diff(backInner, origInner, 1e-3);
    std::printf("[MEASURE] round trip full: maxErr=%.3e rmsErr=%.1f dB | interior: maxErr=%.3e rmsErr=%.1f dB\n",
                d.maxAbsError, d.rmsErrorDb, inner.maxAbsError, inner.rmsErrorDb);
    // Gate -90 dB: measured interior round-trip error is -95.5 dB RMS for a passband
    // tone (two float-LUT sinc passes); -90 leaves margin without masking regressions.
    t.expect("interp round trip 44.1->48->44.1 interior RMS error < -90 dB", inner.rmsErrorDb < -90.0,
             "rmsErrDb=" + std::to_string(inner.rmsErrorDb));
}

// Cubic contrast numbers for the research doc (no hard quality gates: Cubic is the
// documented low-CPU mode; we only pin that it stays roughly level-true).
// Measured 44.1->48: 1 kHz gain error -0.000045 dB, SINAD 89.5 dB, but the 23.1 kHz
// image is only -7.2 dBc (vs Sinc64's -21.7): cubic is level-accurate at low
// frequencies and much dirtier near Nyquist.
void measureCubicContrast(CheckSession& t) {
    std::printf("\n--- production interpolator path: Cubic contrast (documentation numbers) ---\n");
    const InterpFn cubic = Interp::CubicInterpolator::interpolate;
    const RatePair& p = kPairs[0]; // 44.1 -> 48
    const Signal out =
        resampleViaInterpolator(cubic, makeSine(p.srcRate, p.srcRate, kToneHz, kAmp), p.dstRate);
    const ToneFit fit = fitTone(out, 0, kToneHz, kEdgeSkip, out.frames() - kEdgeSkip);
    const Signal outNyq =
        resampleViaInterpolator(cubic, makeSine(p.srcRate, p.srcRate, p.probeHz, kAmp), p.dstRate);
    const double imageAmp = toneAmplitude(outNyq, 0, p.artifactHz, kEdgeSkip, outNyq.frames() - kEdgeSkip);
    std::printf("[MEASURE] cubic 44.1->48 1kHz gainErr=%.6f dB sinad=%.1f dB; image at %.0f Hz = %.1f dBc\n",
                toDb(fit.amplitude / kAmp), fit.sinadDb, p.artifactHz, toDb(imageAmp / kAmp));
    t.expect("cubic 44.1->48 1 kHz level within 0.1 dB", std::abs(toDb(fit.amplitude / kAmp)) < 0.1,
             "gainErrDb=" + std::to_string(toDb(fit.amplitude / kAmp)));
}

// =============================================================================
// Streaming SampleRateConverter audit (no production call sites; in-repo reference)
// =============================================================================

void auditStreamingPair(CheckSession& t, const RatePair& p) {
    const std::string name = "streamSRC(Sinc64) " + pairName(p);
    std::printf("\n--- streaming SampleRateConverter: %s ---\n", pairName(p).c_str());
    const uint32_t srcFrames = p.srcRate; // 1 second

    // Output length. getLatency() is in INPUT frames (halfTaps of history priming), so
    // the single-shot deficit scales with the ratio. Gate latency*ratio + 8: measured
    // deficits are 34 (44.1->48, 32*1.088=34.8), 29 (48->44.1), 63 (48->96, 32*2=64),
    // 15 (96->48), 69 (44.1->96, 32*2.177=69.7) output frames.
    {
        const StreamResult r = resampleViaStreamingSRC(makeDC(p.srcRate, srcFrames, kAmp), p.dstRate,
                                                       SRCQuality::Sinc64);
        const double ratio = static_cast<double>(p.dstRate) / p.srcRate;
        const double expected = static_cast<double>(srcFrames) * ratio;
        const double deficit = expected - static_cast<double>(r.written);
        std::printf("[MEASURE] %s length written=%u expected~%.0f (deficit %.0f) latency=%u input frames\n",
                    name.c_str(), r.written, expected, deficit, r.latency);
        t.expect((name + ": output length short by at most latency*ratio+8").c_str(),
                 deficit >= 0.0 && deficit <= static_cast<double>(r.latency) * ratio + 8.0,
                 "written=" + std::to_string(r.written) + " expected=" + std::to_string(expected));

        // DC gain after settling. Gate 5e-5 abs matches ResamplerWarTest's existing
        // gate; measured error here is <= 3.0e-8 across the matrix.
        const double dc = dcOffset(r.out, 0, kEdgeSkip, r.out.frames() - kEdgeSkip);
        std::printf("[MEASURE] %s DC out=%.9f\n", name.c_str(), dc);
        t.expectNear((name + ": DC gain ~= 1").c_str(), dc, kAmp, 5e-5);
    }

    // Passband level at 1 kHz. Gate 0.1 dB: measured worst gain error 0.00045 dB.
    {
        const StreamResult r = resampleViaStreamingSRC(makeSine(p.srcRate, srcFrames, kToneHz, kAmp),
                                                       p.dstRate, SRCQuality::Sinc64);
        const ToneFit fit = fitTone(r.out, 0, kToneHz, kEdgeSkip, r.out.frames() - kEdgeSkip);
        std::printf("[MEASURE] %s 1kHz amp=%.9f gainErr=%.6f dB sinad=%.1f dB\n", name.c_str(), fit.amplitude,
                    toDb(fit.amplitude / kAmp), fit.sinadDb);
        t.expect((name + ": 1 kHz gain within 0.1 dB").c_str(), std::abs(toDb(fit.amplitude / kAmp)) < 0.1,
                 "gainErrDb=" + std::to_string(toDb(fit.amplitude / kAmp)));
        // FINDING (characterization gate, 35 dB): measured residual SINAD is only
        // 39.7-44.3 dB at FRACTIONAL ratios (vs 127.0/141.7 dB at the integer-ratio
        // pairs, and vs 87.8+ dB on the production Interpolators path). This is far
        // below the "mastering/reference grade" wording in SampleRateConverter.h.
        // Mechanism not fully attributed yet (256-phase bank without inter-phase
        // interpolation is the prime suspect but under-predicts the loss) — flagged
        // as a Phase-2 research target in AestraDocs/audio-research-bench.md. The
        // converter has no production call sites today, so this is not user-facing.
        t.expect((name + ": 1 kHz residual SINAD > 35 dB (characterization; see FINDING)").c_str(),
                 fit.sinadDb > 35.0, "sinadDb=" + std::to_string(fit.sinadDb));
    }

    // Near-Nyquist probe. Unlike the interp path this converter is ratio-aware:
    // downsampling must SUPPRESS the between-Nyquists probe instead of folding it.
    {
        const StreamResult r = resampleViaStreamingSRC(makeSine(p.srcRate, srcFrames, p.probeHz, kAmp),
                                                       p.dstRate, SRCQuality::Sinc64);
        const double artifactAmp = toneAmplitude(r.out, 0, p.artifactHz, kEdgeSkip, r.out.frames() - kEdgeSkip);
        const double artifactDb = toDb(artifactAmp / kAmp);
        std::printf("[MEASURE] %s probe %.0f Hz -> artifact %.0f Hz = %.1f dBc\n", name.c_str(), p.probeHz,
                    p.artifactHz, artifactDb);
        // Per-pair ceilings: measured -41.4 (44.1->48), -57.8 (48->44.1), -72.5
        // (48->96), -117.1 (96->48), -47.6 (44.1->96) dBc. Unlike the interp path the
        // downsampling aliases ARE suppressed (ratio-aware cutoff), but fractional-
        // ratio phase quantization keeps several pairs in the -41..-48 dBc range —
        // same-probe references: libsamplerate SINC_BEST -73.7..-163.7 dBc, soxr VHQ
        // -78.4..-200.7 dBc (printed below when available).
        t.expect((name + ": near-Nyquist artifact below per-pair ceiling").c_str(),
                 artifactDb < p.streamArtifactGateDbc,
                 "artifactDbc=" + std::to_string(artifactDb) +
                     " gate=" + std::to_string(p.streamArtifactGateDbc));
    }
}

// =============================================================================
// Optional external references (diagnostics only, never gated)
// =============================================================================

void printExternalReferenceDiagnostics() {
    std::printf("\n--- external reference diagnostics (not gated) ---\n");
#if !defined(AESTRA_HAS_LIBSAMPLERATE) && !defined(AESTRA_HAS_SOXR)
    std::printf("  libsamplerate/soxr not available at configure time; skipped\n");
#endif
#ifdef AESTRA_HAS_LIBSAMPLERATE
    for (const RatePair& p : kPairs) {
        const Signal in = makeSine(p.srcRate, p.srcRate, p.probeHz, kAmp);
        const double ratio = static_cast<double>(p.dstRate) / p.srcRate;
        const uint32_t maxOut = static_cast<uint32_t>(std::ceil(p.srcRate * ratio)) + 128;
        Signal out;
        out.sampleRate = p.dstRate;
        out.channels = 2;
        out.samples.resize(static_cast<size_t>(maxOut) * 2);
        SRC_DATA data{};
        data.data_in = in.samples.data();
        data.data_out = out.samples.data();
        data.input_frames = static_cast<long>(in.frames());
        data.output_frames = static_cast<long>(maxOut);
        data.src_ratio = ratio;
        data.end_of_input = 1;
        if (src_simple(&data, SRC_SINC_BEST_QUALITY, 2) == 0) {
            out.samples.resize(static_cast<size_t>(data.output_frames_gen) * 2);
            const double artifact =
                toneAmplitude(out, 0, p.artifactHz, kEdgeSkip, out.frames() - kEdgeSkip);
            std::printf("[REF] libsamplerate BEST %s: artifact at %.0f Hz = %.1f dBc\n", pairName(p).c_str(),
                        p.artifactHz, toDb(artifact / kAmp));
        }
    }
#endif
#ifdef AESTRA_HAS_SOXR
    for (const RatePair& p : kPairs) {
        const Signal in = makeSine(p.srcRate, p.srcRate, p.probeHz, kAmp);
        const double ratio = static_cast<double>(p.dstRate) / p.srcRate;
        const uint32_t maxOut = static_cast<uint32_t>(std::ceil(p.srcRate * ratio)) + 128;
        Signal out;
        out.sampleRate = p.dstRate;
        out.channels = 2;
        out.samples.resize(static_cast<size_t>(maxOut) * 2);
        soxr_quality_spec_t qspec = soxr_quality_spec(SOXR_VHQ, 0);
        soxr_t soxr = soxr_create(p.srcRate, p.dstRate, 2, nullptr, nullptr, &qspec, nullptr);
        if (soxr) {
            size_t written = 0;
            if (!soxr_process(soxr, in.samples.data(), in.frames(), nullptr, out.samples.data(), maxOut,
                              &written)) {
                out.samples.resize(written * 2);
                const double artifact =
                    toneAmplitude(out, 0, p.artifactHz, kEdgeSkip, out.frames() - kEdgeSkip);
                std::printf("[REF] soxr VHQ %s: artifact at %.0f Hz = %.1f dBc\n", pairName(p).c_str(),
                            p.artifactHz, toDb(artifact / kAmp));
            }
            soxr_delete(soxr);
        }
    }
#endif
}

} // namespace

int main() {
    std::printf("============================================================\n");
    std::printf("  Aestra Audio Research Bench — Resampler Quality Audit\n");
    std::printf("============================================================\n");

    CheckSession t;
    for (const RatePair& p : kPairs) {
        auditInterpolatorPair(t, p);
    }
    auditInterpolatorRoundTrip(t);
    measureCubicContrast(t);
    for (const RatePair& p : kPairs) {
        auditStreamingPair(t, p);
    }
    printExternalReferenceDiagnostics();
    return t.exitCode();
}
