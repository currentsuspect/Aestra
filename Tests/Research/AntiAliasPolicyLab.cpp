// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AntiAliasPolicyLab — Phase 3: downsampling anti-alias prototype comparison.
//
// Finding F1 (Aestra-Internals: aestra-docs/audio-research-bench.md): no Aestra clip-resampling path
// applies ratio-aware anti-aliasing when downsampling, so source content between the
// destination and source Nyquist frequencies folds into the audible band at full
// level (measured -1.1 dBc at 48->44.1, -0.0 dBc at 96->48, through real sessions).
//
// This lab measures PROTOTYPE options against the shipped baseline so the production
// decision can be made from numbers instead of adjectives. Everything here is
// TEST-SIDE ONLY — no production DSP is changed by this file:
//
//   Baseline  — legacy exact-sinc Sinc64Interpolator via the phase-accumulator
//               replica (what mainline playback/full-mix export/isolated bounce
//               actually run since Phase 2E).
//   Option A  — INTEGRATED ratio-aware kernel: the same windowed-sinc interpolation,
//               but the sinc cutoff is scaled by c = min(1, dstRate/srcRate) so the
//               kernel itself rejects content above the destination Nyquist.
//               Zero-phase, stateless, no memory: the shape that could run directly
//               inside the existing per-voice `phase += ratio` loops. Measured at 64
//               and 128 taps (transition width is set by taps).
//   Option B  — PREFILTER: a designed linear-phase Kaiser low-pass applied to the
//               source at the source rate (delay-compensated by its integer group
//               delay), followed by the UNCHANGED baseline interpolator. The shape
//               that would run once at clip load / rate change (needs a filtered
//               copy of the clip: memory cost, but zero extra audio-thread work).
//   Reference — libsamplerate SINC_BEST / soxr VHQ on identical probes when present
//               at configure time (diagnostics, never gated).
//
// Gate policy: gates pin PROTOTYPE correctness and the measured quality claims of
// this lab (calibrated from this lab's own first run, cited in comments). They make
// no claim about production behavior — SessionResamplingTruthTest owns that.
//
// Doc: Aestra-Internals: aestra-docs/audio-research-bench.md §9 quotes the [MEASURE] lines printed here.

#include "AudioMeasure.h"
#include "SignalLab.h"

#include "DSP/Interpolators.h"

#ifdef AESTRA_HAS_LIBSAMPLERATE
#include <samplerate.h>
#endif
#ifdef AESTRA_HAS_SOXR
#include <soxr.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace AudioResearch;
namespace Interp = Aestra::Audio::Interpolators;

namespace {

constexpr double kAmp = 0.5;
constexpr double kToneHz = 1000.0;
constexpr uint32_t kEdgeSkip = 512; // generous: covers the widest prototype kernel half-width
constexpr uint32_t kSeconds = 1;

// =============================================================================
// Baseline replica (identical to ResamplerQualityAuditTest / SessionResamplingTruthTest)
// =============================================================================

using InterpFn = void (*)(const float*, int64_t, double, float&, float&);

Signal resampleViaInterpolator(InterpFn fn, const Signal& src, uint32_t dstRate) {
    Signal out;
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

// =============================================================================
// Option A — integrated ratio-aware windowed sinc (cutoff-scaled kernel)
// =============================================================================
//
// Standard bandlimited-interpolation form (J.O. Smith): for conversion ratio
// r = srcRate/dstRate, the kernel is h(x) = c * sinc(c*x) * w(x) with
// c = min(1, 1/r), evaluated at x = t - frac over `taps` source samples and
// normalized by the summed kernel (keeps DC exact, like the legacy kernel).
// Zero-phase by construction (kernel centered on the read position), so clip
// timing does not move. Cost: one sin() per TAP per output sample — the legacy
// kernel's single-sin trick (sin(pi(t-frac)) = +/-sin(pi*frac)) does not survive
// cutoff scaling. That cost is the headline CPU question this lab measures.
Signal resampleRatioAwareSinc(const Signal& src, uint32_t dstRate, int taps) {
    Signal out;
    out.sampleRate = dstRate;
    out.channels = 2;
    const double ratio = static_cast<double>(src.sampleRate) / static_cast<double>(dstRate);
    const double c = std::min(1.0, 1.0 / ratio);
    const int half = taps / 2;
    const uint32_t outFrames =
        static_cast<uint32_t>(std::floor(static_cast<double>(src.frames()) * dstRate / src.sampleRate));
    out.samples.resize(static_cast<size_t>(outFrames) * 2);
    const float* data = src.samples.data();
    const int64_t totalFrames = src.frames();

    // The window depends only on the tap index, never on frac — precompute it once
    // (the legacy kernel does the same with its static weights table), so the timed
    // per-sample cost is the honest one: one sinc (one sin) per tap.
    std::vector<double> win(static_cast<size_t>(taps));
    for (int i = 0; i < taps; ++i) {
        win[static_cast<size_t>(i)] =
            Interp::kaiserWindow(static_cast<double>(i), static_cast<double>(taps), 12.0);
    }

    double phase = 0.0;
    for (uint32_t i = 0; i < outFrames; ++i) {
        const int64_t idx = static_cast<int64_t>(phase);
        const double frac = phase - static_cast<double>(idx);
        double sumL = 0.0;
        double sumR = 0.0;
        double ksum = 0.0;
        for (int t = -half + 1; t <= half; ++t) {
            const int64_t sIdx = idx + t;
            if (sIdx < 0 || sIdx >= totalFrames) {
                continue;
            }
            const double x = static_cast<double>(t) - frac;
            const double k = c * Interp::sinc(c * x) * win[static_cast<size_t>(t + half - 1)];
            sumL += static_cast<double>(data[sIdx * 2]) * k;
            sumR += static_cast<double>(data[(sIdx * 2) + 1]) * k;
            ksum += k;
        }
        if (ksum > 0.0) {
            sumL /= ksum;
            sumR /= ksum;
        }
        out.at(i, 0) = static_cast<float>(sumL);
        out.at(i, 1) = static_cast<float>(sumR);
        phase += ratio;
    }
    return out;
}

// =============================================================================
// Option B — designed Kaiser FIR prefilter at the source rate, then the baseline
// =============================================================================

struct FirSpec {
    std::vector<double> h; // odd length, linear phase, sum == 1
    int taps{0};
    double betaUsed{0.0};
};

/// Kaiser low-pass design from a spec (passband edge, stopband edge, stopband
/// attenuation), the textbook Kaiser formulas: beta = 0.1102(A - 8.7) for A > 50,
/// N ~= (A - 7.95) / (2.285 * dOmega). Cut frequency at the band center.
FirSpec designKaiserLowpass(double sampleRate, double passHz, double stopHz, double attenDb) {
    FirSpec f;
    const double dOmega = 2.0 * Interp::PI * (stopHz - passHz) / sampleRate;
    int taps = static_cast<int>(std::ceil((attenDb - 7.95) / (2.285 * dOmega))) + 1;
    if ((taps % 2) == 0) {
        ++taps; // odd length -> integer group delay (taps-1)/2
    }
    f.taps = taps;
    f.betaUsed = 0.1102 * (attenDb - 8.7);
    const double fc = 0.5 * (passHz + stopHz) / sampleRate; // normalized (cycles/sample)
    const int mid = (taps - 1) / 2;
    f.h.resize(static_cast<size_t>(taps));
    double sum = 0.0;
    for (int i = 0; i < taps; ++i) {
        const double x = static_cast<double>(i - mid);
        const double v = 2.0 * fc * Interp::sinc(2.0 * fc * x) *
                         Interp::kaiserWindow(static_cast<double>(i), static_cast<double>(taps), f.betaUsed);
        f.h[static_cast<size_t>(i)] = v;
        sum += v;
    }
    for (double& v : f.h) {
        v /= sum; // DC-exact
    }
    return f;
}

/// Convolve (stereo interleaved) and compensate the integer group delay, so the
/// filtered clip is time-aligned with the original (same frame count; the last
/// (taps-1)/2 frames fall off the end, as they would with a real look-ahead).
Signal prefilter(const Signal& src, const FirSpec& f) {
    Signal out = src;
    const int mid = (f.taps - 1) / 2;
    const int64_t n = src.frames();
    for (int64_t i = 0; i < n; ++i) {
        double accL = 0.0;
        double accR = 0.0;
        for (int k = 0; k < f.taps; ++k) {
            const int64_t j = i + mid - k; // delay-compensated read
            if (j < 0 || j >= n) {
                continue;
            }
            accL += f.h[static_cast<size_t>(k)] * static_cast<double>(src.at(static_cast<uint32_t>(j), 0));
            accR += f.h[static_cast<size_t>(k)] * static_cast<double>(src.at(static_cast<uint32_t>(j), 1));
        }
        out.at(static_cast<uint32_t>(i), 0) = static_cast<float>(accL);
        out.at(static_cast<uint32_t>(i), 1) = static_cast<float>(accR);
    }
    return out;
}

// =============================================================================
// Scenario table
// =============================================================================

struct DownPair {
    uint32_t srcRate;
    uint32_t dstRate;
    // Alias probe set: tones between the two Nyquists -> fold to (dstRate - f).
    // First entry is the Phase-1 probe so results line up with the earlier audits.
    double aliasProbesHz[4];
    int aliasProbeCount;
    // Passband profile points (below the eventual anti-alias transition band).
    double passbandHz[6];
    int passbandCount;
    // Prefilter design spec (Option B): pass edge, stop edge at dst Nyquist.
    double prefilterPassHz;
};

const DownPair kDownPairs[] = {
    // 48k -> 44.1k: fold band is 22.05-24 kHz (narrow, top of hearing range).
    {48000, 44100, {23000.0, 22500.0, 23500.0, 0.0}, 3,
     {100.0, 1000.0, 5000.0, 10000.0, 15000.0, 19000.0}, 6, 19845.0},
    // 96k -> 48k: fold band is 24-48 kHz (a full octave folding across the whole
    // audible band — the severe case, typical for hi-res library content).
    {96000, 48000, {30000.0, 25000.0, 40000.0, 47000.0}, 4,
     {100.0, 1000.0, 5000.0, 10000.0, 15000.0, 21000.0}, 6, 21600.0},
};

std::string pairName(const DownPair& p) {
    return std::to_string(p.srcRate) + "->" + std::to_string(p.dstRate);
}

// =============================================================================
// Option wrappers (uniform call shape for the measurement loop)
// =============================================================================

enum class Option { Baseline, RatioA64, RatioA128, PrefilterB };

const char* optionName(Option o) {
    switch (o) {
    case Option::Baseline:
        return "baseline(Sinc64legacy)";
    case Option::RatioA64:
        return "optionA(ratio-sinc,64taps)";
    case Option::RatioA128:
        return "optionA(ratio-sinc,128taps)";
    case Option::PrefilterB:
        return "optionB(prefilter+legacy)";
    }
    return "?";
}

Signal runOption(Option o, const Signal& src, uint32_t dstRate, const DownPair* spec) {
    switch (o) {
    case Option::Baseline:
        return resampleViaInterpolator(Interp::Sinc64Interpolator::interpolate, src, dstRate);
    case Option::RatioA64:
        return resampleRatioAwareSinc(src, dstRate, 64);
    case Option::RatioA128:
        return resampleRatioAwareSinc(src, dstRate, 128);
    case Option::PrefilterB: {
        // Design per (srcRate, dstRate); on non-downsampling ratios Option B is
        // defined as "no prefilter" (nothing to protect), i.e. the baseline.
        if (spec == nullptr || src.sampleRate <= dstRate) {
            return resampleViaInterpolator(Interp::Sinc64Interpolator::interpolate, src, dstRate);
        }
        const FirSpec f =
            designKaiserLowpass(src.sampleRate, spec->prefilterPassHz, 0.5 * dstRate, 100.0);
        const Signal filtered = prefilter(src, f);
        return resampleViaInterpolator(Interp::Sinc64Interpolator::interpolate, filtered, dstRate);
    }
    }
    return Signal{};
}

// =============================================================================
// Measurement passes
// =============================================================================

// ---- correctness controls -------------------------------------------------

void runControls(CheckSession& t) {
    std::printf("\n=== controls: options must not change behavior when there is nothing to do ===\n");

    // Same-rate: every option must be an identity-quality pass (48k -> 48k).
    {
        const Signal src = makeSine(48000, 48000 * kSeconds, kToneHz, kAmp);
        const Signal base = resampleViaInterpolator(Interp::Sinc64Interpolator::interpolate, src, 48000);
        for (Option o : {Option::RatioA64, Option::RatioA128, Option::PrefilterB}) {
            const Signal out = runOption(o, src, 48000, nullptr);
            const DiffReport d = diff(out, base, 1e-6);
            std::printf("[MEASURE] control same-rate %s vs baseline: maxErr=%.3e rmsErr=%.1f dB\n",
                        optionName(o), d.maxAbsError, d.rmsErrorDb);
            // Gate: at ratio 1 every output sample lands on an input sample (frac = 0),
            // where both kernel shapes reduce to the unit tap. Measured 0 exactly.
            t.expect((std::string("control same-rate: ") + optionName(o) + " == baseline").c_str(),
                     d.maxAbsError < 1e-6, "maxErr=" + std::to_string(d.maxAbsError));
        }
    }

    // Upsampling (44.1k -> 48k): c = 1, Option A must null the baseline shape class
    // and Option B is baseline by definition. A ratio-aware change must never make
    // upsampling worse.
    {
        const Signal src = makeSine(44100, 44100 * kSeconds, kToneHz, kAmp);
        const Signal base = resampleViaInterpolator(Interp::Sinc64Interpolator::interpolate, src, 48000);
        const ToneFit baseFit = fitTone(base, 0, kToneHz, kEdgeSkip, base.frames() - kEdgeSkip);
        const Signal a64 = runOption(Option::RatioA64, src, 48000, nullptr);
        const DiffReport d = diff(a64, base, 1e-6);
        const ToneFit aFit = fitTone(a64, 0, kToneHz, kEdgeSkip, a64.frames() - kEdgeSkip);
        std::printf("[MEASURE] control upsample 44.1->48 optionA64 vs baseline: maxErr=%.3e rmsErr=%.1f dB "
                    "(sinad %.1f vs %.1f dB)\n",
                    d.maxAbsError, d.rmsErrorDb, aFit.sinadDb, baseFit.sinadDb);
        // At c = 1 Option A's kernel is EXACTLY the legacy kernel's math (same sinc,
        // same window, same normalization) evaluated the slow way. Measured null: 0.
        t.expect("control upsample: optionA64 == baseline at c=1", d.maxAbsError < 1e-6,
                 "maxErr=" + std::to_string(d.maxAbsError));
    }
}

// ---- per-pair quality matrix ------------------------------------------------

void measurePair(CheckSession& t, const DownPair& p) {
    const uint32_t srcFrames = p.srcRate * kSeconds;
    std::printf("\n=== downsampling pair %s ===\n", pairName(p).c_str());

    // Print the Option-B design once per pair (taps drive its CPU/memory story).
    const FirSpec fir = designKaiserLowpass(p.srcRate, p.prefilterPassHz, 0.5 * p.dstRate, 100.0);
    std::printf("[MEASURE] %s optionB prefilter design: pass %.0f Hz, stop %.0f Hz, 100 dB -> %d taps "
                "(beta %.2f, group delay %d src frames, compensated)\n",
                pairName(p).c_str(), p.prefilterPassHz, 0.5 * p.dstRate, fir.taps, fir.betaUsed,
                (fir.taps - 1) / 2);

    for (Option o : {Option::Baseline, Option::RatioA64, Option::RatioA128, Option::PrefilterB}) {
        const std::string name = pairName(p) + " " + optionName(o);
        std::printf("--- %s ---\n", name.c_str());

        // DC exactness.
        {
            const Signal out = runOption(o, makeDC(p.srcRate, srcFrames, kAmp), p.dstRate, &p);
            const double dc = dcOffset(out, 0, kEdgeSkip, out.frames() - kEdgeSkip);
            std::printf("[MEASURE] %s DC out=%.9f\n", name.c_str(), dc);
            // Gate 1e-6: all options normalize their kernel/filter to unity DC sum;
            // measured worst-case error across the matrix is < 1e-7.
            t.expectNear((name + ": DC exact").c_str(), dc, kAmp, 1e-6);
        }

        // 1 kHz level + residual.
        {
            const Signal out = runOption(o, makeSine(p.srcRate, srcFrames, kToneHz, kAmp), p.dstRate, &p);
            const ToneFit fit = fitTone(out, 0, kToneHz, kEdgeSkip, out.frames() - kEdgeSkip);
            std::printf("[MEASURE] %s 1kHz gainErr=%.6f dB sinad=%.1f dB\n", name.c_str(),
                        toDb(fit.amplitude / kAmp), fit.sinadDb);
            t.expect((name + ": 1 kHz gain within 0.05 dB").c_str(),
                     std::abs(toDb(fit.amplitude / kAmp)) < 0.05,
                     "gainErrDb=" + std::to_string(toDb(fit.amplitude / kAmp)));
        }

        // Passband profile (loss/ripple toward the transition band).
        {
            double worstLossDb = 0.0;
            std::string profile;
            for (int i = 0; i < p.passbandCount; ++i) {
                const double f = p.passbandHz[i];
                const Signal out = runOption(o, makeSine(p.srcRate, srcFrames, f, kAmp), p.dstRate, &p);
                const double g = toDb(toneAmplitude(out, 0, f, kEdgeSkip, out.frames() - kEdgeSkip) / kAmp);
                profile += std::to_string(f) + "Hz:" + std::to_string(g) + "dB ";
                worstLossDb = std::min(worstLossDb, g);
            }
            std::printf("[MEASURE] %s passband profile: %s(worst %.4f dB)\n", name.c_str(), profile.c_str(),
                        worstLossDb);
            // Gate 0.5 dB at/below the stated passband edge. Measured worst:
            // baseline, optionA128 and optionB are exact to < 0.0001 dB everywhere;
            // optionA64 droops -0.294 dB at 21 kHz (96->48) — the 64-tap scaled
            // kernel's transition reaching down into the passband, quantified here.
            t.expect((name + ": passband loss < 0.5 dB up to the profile edge").c_str(),
                     worstLossDb > -0.5, "worstLossDb=" + std::to_string(worstLossDb));
        }

        // Alias rejection set (the point of the whole exercise).
        {
            for (int i = 0; i < p.aliasProbeCount; ++i) {
                const double f = p.aliasProbesHz[i];
                const double aliasHz = static_cast<double>(p.dstRate) - f;
                // "Deep" fold = probe at least 25% above the destination Nyquist,
                // i.e. clear of any realistic kernel transition band.
                const bool deepFold = f >= 1.25 * 0.5 * static_cast<double>(p.dstRate);
                const Signal out = runOption(o, makeSine(p.srcRate, srcFrames, f, kAmp), p.dstRate, &p);
                const double aliasDbc =
                    toDb(toneAmplitude(out, 0, aliasHz, kEdgeSkip, out.frames() - kEdgeSkip) / kAmp);
                std::printf("[MEASURE] %s alias probe %.0f Hz -> %.0f Hz: %.2f dBc%s\n", name.c_str(), f,
                            aliasHz, aliasDbc, deepFold ? "" : "  (transition-band probe)");
                if (o == Option::Baseline) {
                    // Characterization only (F1): the baseline folds these near full
                    // scale (measured -0.0 to -2.9 dBc across the matrix);
                    // SessionResamplingTruthTest owns the production gate.
                    continue;
                }
                if (o == Option::PrefilterB) {
                    // Option B's DESIGNED transition (pass edge at 0.9x dst Nyquist,
                    // 100 dB spec) covers every probe, transition-band ones included.
                    // Gate -95 dBc: measured -101.5 to -139.1 dBc across the matrix.
                    t.expect((name + ": alias " + std::to_string(static_cast<int>(f)) +
                              " Hz below -95 dBc (designed stopband)")
                                 .c_str(),
                             aliasDbc < -95.0, "aliasDbc=" + std::to_string(aliasDbc));
                } else if (deepFold) {
                    // Option A rejects deep folds hard. Gate -110 dBc: measured
                    // -117.6 to -140.6 dBc at the 96->48 30/40/47 kHz probes.
                    t.expect((name + ": deep-fold alias " + std::to_string(static_cast<int>(f)) +
                              " Hz below -110 dBc")
                                 .c_str(),
                             aliasDbc < -110.0, "aliasDbc=" + std::to_string(aliasDbc));
                } else {
                    // FINDING (characterization, no quality gate): Option A's
                    // transition band stays hot at practical tap counts — measured
                    // -10.5/-17.8/-28.3 dBc (64 taps) and -17.1/-41.9/-123.1 dBc
                    // (128 taps) at 48->44.1, -11.1/-18.9 dBc at the 96->48 25 kHz
                    // probe. For 48->44.1 the ENTIRE fold band (22.05-24 kHz) is
                    // transition band at <= 128 taps: cutoff scaling alone cannot
                    // protect near-unity downsampling ratios. This is the decisive
                    // measurement against Option A; the doc quotes it. The loose
                    // ceiling below only pins "clearly better than the baseline's
                    // near-full-scale folding" so a regression in the prototype
                    // itself is still caught.
                    t.expect((name + ": transition-band alias " + std::to_string(static_cast<int>(f)) +
                              " Hz at least 7 dB better than baseline class (< -8 dBc)")
                                 .c_str(),
                             aliasDbc < -8.0, "aliasDbc=" + std::to_string(aliasDbc));
                }
            }
        }

        // Impulse: position (group delay) and spread.
        {
            const uint32_t impPos = p.srcRate / 2;
            const Signal out =
                runOption(o, makeImpulse(p.srcRate, srcFrames, impPos, 1.0), p.dstRate, &p);
            const ImpulseReport r = analyzeImpulse(out, 0);
            const double expectedPeak = static_cast<double>(impPos) * p.dstRate / p.srcRate;
            std::printf("[MEASURE] %s impulse peak at %lld (expect ~%.0f), span=%lld frames, preRms=%.2e "
                        "tailRms=%.2e\n",
                        name.c_str(), static_cast<long long>(r.peakFrame), expectedPeak,
                        static_cast<long long>(r.spanFrames), r.preSpanRms, r.tailRms);
            // Zero/compensated group delay: all options must keep the impulse on the
            // mapped frame (Option A is kernel-centered; Option B compensates its
            // integer FIR delay exactly).
            t.expect((name + ": impulse lands at mapped frame (+/-2)").c_str(),
                     std::abs(static_cast<double>(r.peakFrame) - expectedPeak) <= 2.0,
                     "peakFrame=" + std::to_string(r.peakFrame));
            // Span ceiling 256 output frames: measured -60 dB spans are 1-37
            // (baseline; 37 on the fractional 48->44.1 pair), 1 (optionA — the
            // 0.5 s impulse position maps to an exact integer phase, where the
            // scaled kernel is still a unit tap), 73 (optionB — the designed
            // 100 dB/0.9-Nyquist prefilter's response; the time-domain price of
            // its narrow transition, and the number the doc quotes).
            t.expect((name + ": impulse -60 dB span < 256 frames").c_str(), r.spanFrames < 256,
                     "spanFrames=" + std::to_string(r.spanFrames));
        }

        // Transient burst: pre-echo beyond the kernel/filter support must be silence.
        {
            const uint32_t burstStart = p.srcRate / 4;
            const uint32_t burstLen = p.srcRate / 10;
            const Signal out = runOption(
                o, makeTransientBurst(p.srcRate, srcFrames, burstStart, burstLen, kToneHz, kAmp), p.dstRate,
                &p);
            const double scale = static_cast<double>(p.dstRate) / p.srcRate;
            const uint32_t outStart = static_cast<uint32_t>(burstStart * scale);
            // Guard: half the widest support in output frames (option B: taps/2 src
            // frames -> *scale) plus margin.
            const uint32_t guard = static_cast<uint32_t>((fir.taps / 2) * scale) + 64;
            const double preRms = rms(out, -1, 0, outStart - guard);
            std::printf("[MEASURE] %s burst preRms(outside support)=%.2e\n", name.c_str(), preRms);
            // Gate 1e-6: measured 0.0 for every option (all kernels/filters have
            // bounded support; no acausal energy beyond it).
            t.expect((name + ": no pre-burst energy outside kernel support").c_str(), preRms < 1e-6,
                     "preRms=" + std::to_string(preRms));
        }

        // Fixed-seed noise: determinism + level sanity (broadband material must not
        // change level by more than the removed out-of-band share allows).
        {
            const Signal noise = makeNoise(p.srcRate, srcFrames, 0xA5A5F00Dull, kAmp);
            const Signal out1 = runOption(o, noise, p.dstRate, &p);
            const Signal out2 = runOption(o, noise, p.dstRate, &p);
            const DiffReport d = diff(out1, out2, 0.0);
            const double rmsOut = rms(out1, -1, kEdgeSkip, out1.frames() - kEdgeSkip);
            const double rmsIn = rms(noise, -1, kEdgeSkip, noise.frames() - kEdgeSkip);
            std::printf("[MEASURE] %s noise: rms in=%.6f out=%.6f (%.3f dB), determinism maxErr=%.1e\n",
                        name.c_str(), rmsIn, rmsOut, toDb(rmsOut / rmsIn), d.maxAbsError);
            t.expect((name + ": bit-deterministic across runs").c_str(), d.maxAbsError == 0.0,
                     "maxErr=" + std::to_string(d.maxAbsError));
            // White noise at srcRate carries energy up to srcNyq; removing the
            // (dstNyq, srcNyq) share bounds the RMS drop at 10*log10(dst/src):
            // -0.37 dB (48->44.1) / -3.01 dB (96->48). Gate: within that bound
            // +/- 0.5 dB for AA options; baseline keeps ~full level (folded energy
            // stays in-band).
            const double bound = 10.0 * std::log10(static_cast<double>(p.dstRate) / p.srcRate);
            if (o != Option::Baseline) {
                const double drop = toDb(rmsOut / rmsIn);
                t.expect((name + ": broadband level drop matches removed band (+/-0.5 dB)").c_str(),
                         std::abs(drop - bound) < 0.5,
                         "dropDb=" + std::to_string(drop) + " boundDb=" + std::to_string(bound));
            }
        }
    }
}

// ---- length correctness (all options share the caller-derived length model) ----

void runLengthChecks(CheckSession& t) {
    std::printf("\n=== length correctness ===\n");
    for (const DownPair& p : kDownPairs) {
        const uint32_t srcFrames = p.srcRate * kSeconds;
        const uint32_t expected =
            static_cast<uint32_t>(std::floor(static_cast<double>(srcFrames) * p.dstRate / p.srcRate));
        for (Option o : {Option::RatioA64, Option::RatioA128, Option::PrefilterB}) {
            const Signal out = runOption(o, makeSilence(p.srcRate, srcFrames), p.dstRate, &p);
            t.expect((pairName(p) + " " + optionName(o) + ": output length == mapped length").c_str(),
                     out.frames() == expected,
                     "frames=" + std::to_string(out.frames()) + " expected=" + std::to_string(expected));
        }
    }
}

// ---- CPU cost ---------------------------------------------------------------

double timeNsPerFrame(Option o, const Signal& src, uint32_t dstRate, const DownPair* spec) {
    // Best (fastest) of 3 runs — least scheduler interference; wall clock on an
    // otherwise idle process. For RELATIVE comparison on one machine, not benchmarks.
    double best = 1e18;
    uint32_t outFrames = 0;
    for (int run = 0; run < 3; ++run) {
        const auto t0 = std::chrono::steady_clock::now();
        const Signal out = runOption(o, src, dstRate, spec);
        const auto t1 = std::chrono::steady_clock::now();
        outFrames = out.frames();
        best = std::min(best,
                        static_cast<double>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }
    return outFrames > 0 ? best / static_cast<double>(outFrames) : 0.0;
}

void runCpuCost(CheckSession& t) {
    std::printf("\n=== CPU cost (stereo, ns per OUTPUT frame; relative comparison only) ===\n");
    for (const DownPair& p : kDownPairs) {
        const Signal src = makeNoise(p.srcRate, p.srcRate * kSeconds, 0xBEEF5EEDull, kAmp);
        const double baseNs = timeNsPerFrame(Option::Baseline, src, p.dstRate, &p);
        std::printf("[MEASURE] %s CPU baseline=%.0f ns/frame (%.2f MFrame/s)\n", pairName(p).c_str(), baseNs,
                    1000.0 / std::max(baseNs, 1e-9));
        for (Option o : {Option::RatioA64, Option::RatioA128, Option::PrefilterB}) {
            const double ns = timeNsPerFrame(o, src, p.dstRate, &p);
            std::printf("[MEASURE] %s CPU %s=%.0f ns/frame (%.2f MFrame/s, %.1fx baseline)\n",
                        pairName(p).c_str(), optionName(o), ns, 1000.0 / std::max(ns, 1e-9), ns / baseNs);
            // Sanity ceiling, NOT a benchmark gate: catches accidental O(n^2) or a
            // debug-quality regression while tolerating machine variance. Measured
            // ratios (window precomputed): optionA64 5.2-6.6x, optionA128 12.0-13.9x,
            // optionB 2.1-4.4x baseline (option B's one-time prefilter amortizes over
            // the whole clip; its steady-state playback cost is EXACTLY baseline).
            t.expect((pairName(p) + " " + std::string(optionName(o)) + ": CPU within 60x baseline sanity bound")
                         .c_str(),
                     ns < baseNs * 60.0,
                     "ns=" + std::to_string(ns) + " base=" + std::to_string(baseNs));
        }
    }
    std::printf("  NOTE optionB: cost shown includes the one-time prefilter of the whole clip; its\n"
                "  per-frame playback cost after that is identical to baseline. Memory: one filtered\n"
                "  copy of the clip (2x clip RAM transient, 1x steady if it replaces the original\n"
                "  for mismatched-rate sessions). optionA is stateless: zero memory, all CPU — and a\n"
                "  production version could replace the per-tap std::sin with a rotation recurrence\n"
                "  (~2 muls/tap), but no CPU work fixes its transition-band folding (see alias set).\n");
}

// ---- optional external references (never gated) -------------------------------

void runReferences() {
    std::printf("\n=== external references (diagnostics, not gated) ===\n");
#if !defined(AESTRA_HAS_LIBSAMPLERATE) && !defined(AESTRA_HAS_SOXR)
    std::printf("  libsamplerate/soxr not available at configure time; skipped\n");
#endif
#ifdef AESTRA_HAS_LIBSAMPLERATE
    for (const DownPair& p : kDownPairs) {
        const double f = p.aliasProbesHz[0];
        const Signal in = makeSine(p.srcRate, p.srcRate * kSeconds, f, kAmp);
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
            const double aliasDbc = toDb(
                toneAmplitude(out, 0, p.dstRate - f, kEdgeSkip, out.frames() - kEdgeSkip) / kAmp);
            std::printf("[REF] libsamplerate BEST %s: alias %.0f Hz = %.1f dBc\n", pairName(p).c_str(),
                        p.dstRate - f, aliasDbc);
        }
    }
#endif
#ifdef AESTRA_HAS_SOXR
    for (const DownPair& p : kDownPairs) {
        const double f = p.aliasProbesHz[0];
        const Signal in = makeSine(p.srcRate, p.srcRate * kSeconds, f, kAmp);
        const uint32_t maxOut =
            static_cast<uint32_t>(std::ceil(static_cast<double>(p.srcRate) * p.dstRate / p.srcRate)) + 128;
        Signal out;
        out.sampleRate = p.dstRate;
        out.channels = 2;
        out.samples.resize(static_cast<size_t>(maxOut) * 2);
        soxr_quality_spec_t qspec = soxr_quality_spec(SOXR_VHQ, 0);
        soxr_t soxr = soxr_create(p.srcRate, p.dstRate, 2, nullptr, nullptr, &qspec, nullptr);
        if (soxr != nullptr) {
            size_t written = 0;
            if (soxr_process(soxr, in.samples.data(), in.frames(), nullptr, out.samples.data(), maxOut,
                             &written) == nullptr) {
                out.samples.resize(written * 2);
                const double aliasDbc = toDb(
                    toneAmplitude(out, 0, p.dstRate - f, kEdgeSkip, out.frames() - kEdgeSkip) / kAmp);
                std::printf("[REF] soxr VHQ %s: alias %.0f Hz = %.1f dBc\n", pairName(p).c_str(),
                            p.dstRate - f, aliasDbc);
            }
            soxr_delete(soxr);
        }
    }
#endif
}

} // namespace

int main() {
    std::printf("============================================================\n");
    std::printf("  Aestra Audio Research Bench — Anti-Alias Policy Lab (Ph.3)\n");
    std::printf("  Prototype comparison for F1; test-side only, no production\n");
    std::printf("  DSP is exercised or changed by this binary beyond the\n");
    std::printf("  baseline kernel replica.\n");
    std::printf("============================================================\n");

    CheckSession t;
    runControls(t);
    for (const DownPair& p : kDownPairs) {
        measurePair(t, p);
    }
    runLengthChecks(t);
    runCpuCost(t);
    runReferences();
    return t.exitCode();
}
