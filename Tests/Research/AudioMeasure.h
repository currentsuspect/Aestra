// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AudioMeasure — deterministic test-side audio analysis for the Aestra Audio Research Bench.
//
// All analysis runs in double precision over float buffers. Every routine is a pure
// function of its inputs: no global state, no randomness, no wall-clock dependence.
//
// Measurement definitions (see AestraDocs/audio-research-bench.md for the full policy):
//   * peak            — max |sample| over the window.
//   * RMS             — sqrt(mean(sample^2)) over the window; dBFS via 20*log10.
//   * DC offset       — arithmetic mean over the window.
//   * tone fit        — 3-parameter least squares (a*sin + b*cos + c) at a KNOWN
//                       frequency. Amplitude = sqrt(a^2+b^2). Exact when the window
//                       holds an integer number of cycles; near-exact for long windows.
//   * residual SINAD  — ratio of fitted-tone RMS to post-subtraction residual RMS.
//                       The residual contains noise + distortion + aliasing across the
//                       whole band. This is a THD+N-style figure for single-tone
//                       stimuli, NOT a spectrum-analyzer SINAD; do not quote it as one.
//   * THD             — sqrt(sum of harmonic-amplitude^2) / fundamental amplitude,
//                       harmonics measured by per-harmonic tone fits BELOW Nyquist only.
//                       Aliased harmonics are not attributed; callers must state the
//                       harmonic range they counted.
//   * impulse stats   — peak position/value, span above a relative threshold,
//                       pre-span (pre-ring) RMS and post-span (tail) RMS.
//   * stereo correlation — Pearson correlation of L vs R (mean-removed).
//
// Failure output is forensic: sample rate, channels, frames, peaks, RMS, max error,
// first mismatching frame/channel, and an expected/actual snippet around the mismatch.
//
// Test-side only. This header must never be included from production code.
#pragma once

#include "SignalLab.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

namespace AudioResearch {

// =============================================================================
// Windows & scalar helpers
// =============================================================================

constexpr uint32_t kToEnd = std::numeric_limits<uint32_t>::max();

inline double toDb(double linear) { return 20.0 * std::log10(std::max(linear, 1e-30)); }

/// Clamp [startFrame, endFrame) to the signal length; endFrame = kToEnd means "to end".
inline void clampWindow(const Signal& s, uint32_t& startFrame, uint32_t& endFrame) {
    endFrame = std::min(endFrame, s.frames());
    startFrame = std::min(startFrame, endFrame);
}

// =============================================================================
// Basic statistics
// =============================================================================

/// Max |sample| over the window. channel = -1 scans all channels.
inline double peak(const Signal& s, int channel = -1, uint32_t startFrame = 0, uint32_t endFrame = kToEnd) {
    clampWindow(s, startFrame, endFrame);
    double p = 0.0;
    for (uint32_t i = startFrame; i < endFrame; ++i) {
        for (uint32_t ch = 0; ch < s.channels; ++ch) {
            if (channel >= 0 && static_cast<uint32_t>(channel) != ch) {
                continue;
            }
            p = std::max(p, std::abs(static_cast<double>(s.at(i, ch))));
        }
    }
    return p;
}

/// sqrt(mean(sample^2)) over the window. channel = -1 pools all channels.
inline double rms(const Signal& s, int channel = -1, uint32_t startFrame = 0, uint32_t endFrame = kToEnd) {
    clampWindow(s, startFrame, endFrame);
    double sumSq = 0.0;
    uint64_t count = 0;
    for (uint32_t i = startFrame; i < endFrame; ++i) {
        for (uint32_t ch = 0; ch < s.channels; ++ch) {
            if (channel >= 0 && static_cast<uint32_t>(channel) != ch) {
                continue;
            }
            const double v = static_cast<double>(s.at(i, ch));
            sumSq += v * v;
            ++count;
        }
    }
    return count > 0 ? std::sqrt(sumSq / static_cast<double>(count)) : 0.0;
}

/// Arithmetic mean over the window (DC offset). channel = -1 pools all channels.
inline double dcOffset(const Signal& s, int channel = -1, uint32_t startFrame = 0, uint32_t endFrame = kToEnd) {
    clampWindow(s, startFrame, endFrame);
    double sum = 0.0;
    uint64_t count = 0;
    for (uint32_t i = startFrame; i < endFrame; ++i) {
        for (uint32_t ch = 0; ch < s.channels; ++ch) {
            if (channel >= 0 && static_cast<uint32_t>(channel) != ch) {
                continue;
            }
            sum += static_cast<double>(s.at(i, ch));
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

/// First frame with |sample| > threshold on any channel; -1 if none.
inline int64_t firstNonSilentFrame(const Signal& s, double threshold = 0.0) {
    for (uint32_t i = 0; i < s.frames(); ++i) {
        for (uint32_t ch = 0; ch < s.channels; ++ch) {
            if (std::abs(static_cast<double>(s.at(i, ch))) > threshold) {
                return static_cast<int64_t>(i);
            }
        }
    }
    return -1;
}

// =============================================================================
// Null / difference forensics
// =============================================================================

struct DiffReport {
    double maxAbsError{0.0};
    double rmsError{0.0};        // linear
    double rmsErrorDb{-999.0};
    int64_t firstMismatchFrame{-1};  // first |a-b| > tolerance; -1 = none
    int firstMismatchChannel{-1};
    uint64_t framesCompared{0};
    bool lengthMismatch{false};
    uint32_t framesA{0};
    uint32_t framesB{0};
    double peakA{0.0};
    double peakB{0.0};
    double rmsA{0.0};
    double rmsB{0.0};
    uint32_t sampleRate{0};
    uint32_t channels{0};
};

/// Null-difference over the common length of two equally-formatted signals.
/// `tolerance` only localizes the first mismatch; pass/fail policy is the caller's.
inline DiffReport diff(const Signal& a, const Signal& b, double tolerance = 0.0) {
    DiffReport r;
    r.sampleRate = a.sampleRate;
    r.channels = a.channels;
    r.framesA = a.frames();
    r.framesB = b.frames();
    r.lengthMismatch = (r.framesA != r.framesB) || (a.channels != b.channels);
    r.peakA = peak(a);
    r.peakB = peak(b);
    r.rmsA = rms(a);
    r.rmsB = rms(b);

    const uint32_t frames = std::min(r.framesA, r.framesB);
    const uint32_t chans = std::min(a.channels, b.channels);
    r.framesCompared = frames;

    double sumSq = 0.0;
    uint64_t count = 0;
    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < chans; ++ch) {
            const double d = static_cast<double>(a.at(i, ch)) - static_cast<double>(b.at(i, ch));
            const double ad = std::abs(d);
            sumSq += d * d;
            ++count;
            r.maxAbsError = std::max(r.maxAbsError, ad);
            if (ad > tolerance && r.firstMismatchFrame < 0) {
                r.firstMismatchFrame = static_cast<int64_t>(i);
                r.firstMismatchChannel = static_cast<int>(ch);
            }
        }
    }
    r.rmsError = count > 0 ? std::sqrt(sumSq / static_cast<double>(count)) : 0.0;
    r.rmsErrorDb = toDb(r.rmsError);
    return r;
}

/// Full forensic dump for a failed comparison, including an actual/expected snippet
/// around the first mismatch (or buffer start if there is none).
inline void printDiffForensics(const std::string& label, const DiffReport& r, const Signal& actual,
                               const Signal& expected, int snippetRadius = 3) {
    std::printf("  ---- research-bench diff forensics: %s ----\n", label.c_str());
    std::printf("  sample rate:     %u Hz\n", r.sampleRate);
    std::printf("  channels:        %u\n", r.channels);
    std::printf("  frames:          actual=%u expected=%u%s\n", r.framesA, r.framesB,
                r.lengthMismatch ? "  (LENGTH MISMATCH)" : "");
    std::printf("  peak:            actual=%.9f expected=%.9f\n", r.peakA, r.peakB);
    std::printf("  RMS:             actual=%.9f expected=%.9f\n", r.rmsA, r.rmsB);
    std::printf("  max abs error:   %.9g\n", r.maxAbsError);
    std::printf("  RMS error:       %.9g (%.1f dB)\n", r.rmsError, r.rmsErrorDb);
    std::printf("  first mismatch:  frame %lld, channel %d\n", static_cast<long long>(r.firstMismatchFrame),
                r.firstMismatchChannel);
    const int64_t center = r.firstMismatchFrame >= 0 ? r.firstMismatchFrame : 0;
    const int64_t lo = std::max<int64_t>(0, center - snippetRadius);
    const int64_t hi = std::min<int64_t>(static_cast<int64_t>(r.framesCompared), center + snippetRadius + 1);
    const uint32_t chans = std::min(actual.channels, expected.channels);
    for (int64_t i = lo; i < hi; ++i) {
        std::printf("    frame %6lld:", static_cast<long long>(i));
        for (uint32_t ch = 0; ch < chans; ++ch) {
            std::printf("  ch%u actual=%+.9f expected=%+.9f", ch, actual.at(static_cast<uint32_t>(i), ch),
                        expected.at(static_cast<uint32_t>(i), ch));
        }
        std::printf("\n");
    }
}

// =============================================================================
// Tone fitting (known-frequency least squares)
// =============================================================================

struct ToneFit {
    double amplitude{0.0};     // sqrt(a^2 + b^2)
    double phaseRad{0.0};      // atan2(b, a) for a*sin + b*cos
    double dc{0.0};            // fitted constant term
    double residualRms{0.0};   // RMS of (signal - fit), full band
    double sinadDb{-999.0};    // 20*log10(fitted tone RMS / residual RMS); see header caveat
};

/// Least-squares fit of a*sin(w n) + b*cos(w n) + c at a KNOWN frequency, solved via
/// 3x3 normal equations in double precision. Exact for integer-cycle windows; use
/// windows >= ~100 cycles when the cycle count is fractional.
inline ToneFit fitTone(const Signal& s, int channel, double freqHz, uint32_t startFrame = 0,
                       uint32_t endFrame = kToEnd) {
    clampWindow(s, startFrame, endFrame);
    const uint32_t n = endFrame - startFrame;
    ToneFit fit;
    if (n < 8 || freqHz <= 0.0 || freqHz >= 0.5 * s.sampleRate) {
        return fit;
    }
    const double w = kTau * freqHz / static_cast<double>(s.sampleRate);

    // Accumulate normal-equation terms for basis {sin, cos, 1}.
    double ss = 0.0, cc = 0.0, sc = 0.0, s1 = 0.0, c1 = 0.0;
    double xs = 0.0, xc = 0.0, x1 = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        const double sn = std::sin(w * static_cast<double>(startFrame + i));
        const double cs = std::cos(w * static_cast<double>(startFrame + i));
        const double x = static_cast<double>(s.at(startFrame + i, static_cast<uint32_t>(channel)));
        ss += sn * sn;
        cc += cs * cs;
        sc += sn * cs;
        s1 += sn;
        c1 += cs;
        xs += x * sn;
        xc += x * cs;
        x1 += x;
    }
    const double nn = static_cast<double>(n);

    // Solve [ss sc s1; sc cc c1; s1 c1 nn] * [a b c]' = [xs xc x1]' by Cramer's rule.
    const double det = ss * (cc * nn - c1 * c1) - sc * (sc * nn - c1 * s1) + s1 * (sc * c1 - cc * s1);
    if (std::abs(det) < 1e-12) {
        return fit;
    }
    const double a =
        (xs * (cc * nn - c1 * c1) - sc * (xc * nn - c1 * x1) + s1 * (xc * c1 - cc * x1)) / det;
    const double b =
        (ss * (xc * nn - c1 * x1) - xs * (sc * nn - c1 * s1) + s1 * (sc * x1 - xc * s1)) / det;
    const double c =
        (ss * (cc * x1 - xc * c1) - sc * (sc * x1 - xs * c1) + xs * (sc * c1 - cc * s1)) / det;

    fit.amplitude = std::sqrt(a * a + b * b);
    fit.phaseRad = std::atan2(b, a);
    fit.dc = c;

    double residSq = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        const double sn = std::sin(w * static_cast<double>(startFrame + i));
        const double cs = std::cos(w * static_cast<double>(startFrame + i));
        const double x = static_cast<double>(s.at(startFrame + i, static_cast<uint32_t>(channel)));
        const double resid = x - (a * sn + b * cs + c);
        residSq += resid * resid;
    }
    fit.residualRms = std::sqrt(residSq / nn);
    const double toneRms = fit.amplitude / std::sqrt(2.0);
    fit.sinadDb = fit.residualRms > 0.0 ? toDb(toneRms / fit.residualRms) : 999.0;
    return fit;
}

/// Amplitude of a known-frequency component (convenience wrapper over fitTone).
inline double toneAmplitude(const Signal& s, int channel, double freqHz, uint32_t startFrame = 0,
                            uint32_t endFrame = kToEnd) {
    return fitTone(s, channel, freqHz, startFrame, endFrame).amplitude;
}

// =============================================================================
// Harmonic analysis (THD)
// =============================================================================

struct HarmonicReport {
    double fundamentalAmplitude{0.0};
    // harmonicAmplitudes[k] = amplitude at (k+2)*f0, i.e. index 0 is the 2nd harmonic.
    std::vector<double> harmonicAmplitudes;
    uint32_t harmonicsMeasured{0}; // number of harmonics that fit below Nyquist
    double thdRatio{0.0};          // sqrt(sum h^2) / fundamental
    double thdDb{-999.0};
};

/// Per-harmonic tone fits at k*f0 for k = 2..maxHarmonic, skipping any harmonic at or
/// above Nyquist. Exact when the window holds integer periods of f0. Aliased harmonic
/// energy is NOT attributed here — it lands in fitTone().residualRms instead.
inline HarmonicReport measureHarmonics(const Signal& s, int channel, double f0Hz, uint32_t maxHarmonic = 15,
                                       uint32_t startFrame = 0, uint32_t endFrame = kToEnd) {
    HarmonicReport r;
    r.fundamentalAmplitude = toneAmplitude(s, channel, f0Hz, startFrame, endFrame);
    double sumSq = 0.0;
    for (uint32_t k = 2; k <= maxHarmonic; ++k) {
        const double f = f0Hz * static_cast<double>(k);
        if (f >= 0.5 * s.sampleRate) {
            break;
        }
        const double amp = toneAmplitude(s, channel, f, startFrame, endFrame);
        r.harmonicAmplitudes.push_back(amp);
        sumSq += amp * amp;
        ++r.harmonicsMeasured;
    }
    if (r.fundamentalAmplitude > 0.0) {
        r.thdRatio = std::sqrt(sumSq) / r.fundamentalAmplitude;
        r.thdDb = toDb(r.thdRatio);
    }
    return r;
}

// =============================================================================
// Impulse analysis
// =============================================================================

struct ImpulseReport {
    double peakAbs{0.0};
    int64_t peakFrame{-1};
    // Contiguous extent of |x| > peakAbs * threshold (relative threshold, linear):
    int64_t spanFirstFrame{-1};
    int64_t spanLastFrame{-1};
    int64_t spanFrames{0};      // spread of the response above threshold
    double preSpanRms{0.0};     // energy before the span (pre-ring / acausal leakage)
    double tailRms{0.0};        // energy after the span (post-ring / echo)
};

/// Characterize an impulse-like response on one channel. `relativeThresholdDb` sets the
/// span cut (default -60 dB below the peak).
inline ImpulseReport analyzeImpulse(const Signal& s, int channel, double relativeThresholdDb = -60.0) {
    ImpulseReport r;
    const uint32_t n = s.frames();
    const uint32_t ch = static_cast<uint32_t>(channel);
    for (uint32_t i = 0; i < n; ++i) {
        const double v = std::abs(static_cast<double>(s.at(i, ch)));
        if (v > r.peakAbs) {
            r.peakAbs = v;
            r.peakFrame = static_cast<int64_t>(i);
        }
    }
    if (r.peakFrame < 0 || r.peakAbs <= 0.0) {
        return r;
    }
    const double cut = r.peakAbs * std::pow(10.0, relativeThresholdDb / 20.0);
    for (uint32_t i = 0; i < n; ++i) {
        if (std::abs(static_cast<double>(s.at(i, ch))) > cut) {
            if (r.spanFirstFrame < 0) {
                r.spanFirstFrame = static_cast<int64_t>(i);
            }
            r.spanLastFrame = static_cast<int64_t>(i);
        }
    }
    r.spanFrames = r.spanLastFrame - r.spanFirstFrame + 1;
    if (r.spanFirstFrame > 0) {
        r.preSpanRms = rms(s, channel, 0, static_cast<uint32_t>(r.spanFirstFrame));
    }
    if (r.spanLastFrame + 1 < static_cast<int64_t>(n)) {
        r.tailRms = rms(s, channel, static_cast<uint32_t>(r.spanLastFrame + 1), n);
    }
    return r;
}

// =============================================================================
// Stereo correlation
// =============================================================================

/// Pearson correlation (mean-removed) between channels 0 and 1 over the window.
/// Returns 0 when either channel has ~zero variance (correlation is undefined there).
inline double stereoCorrelation(const Signal& s, uint32_t startFrame = 0, uint32_t endFrame = kToEnd) {
    clampWindow(s, startFrame, endFrame);
    if (s.channels < 2 || endFrame <= startFrame) {
        return 0.0;
    }
    const double meanL = dcOffset(s, 0, startFrame, endFrame);
    const double meanR = dcOffset(s, 1, startFrame, endFrame);
    double sLL = 0.0, sRR = 0.0, sLR = 0.0;
    for (uint32_t i = startFrame; i < endFrame; ++i) {
        const double l = static_cast<double>(s.at(i, 0)) - meanL;
        const double r = static_cast<double>(s.at(i, 1)) - meanR;
        sLL += l * l;
        sRR += r * r;
        sLR += l * r;
    }
    if (sLL < 1e-24 || sRR < 1e-24) {
        return 0.0;
    }
    return sLR / std::sqrt(sLL * sRR);
}

// =============================================================================
// Minimal check harness (shared PASS/FAIL accounting for research tests)
// =============================================================================

struct CheckSession {
    int passed{0};
    int failed{0};

    bool expect(const char* name, bool ok, const std::string& detail = {}) {
        std::printf("[%s] %s", ok ? "PASS" : "FAIL", name);
        if (!detail.empty()) {
            std::printf(" - %s", detail.c_str());
        }
        std::printf("\n");
        ok ? ++passed : ++failed;
        return ok;
    }

    /// Expect |value - expected| <= tolerance, with the numbers in the log either way.
    bool expectNear(const char* name, double value, double expected, double tolerance) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "value=%.12g expected=%.12g tol=%.3g", value, expected, tolerance);
        return expect(name, std::abs(value - expected) <= tolerance, buf);
    }

    int exitCode() const {
        std::printf("\nResearch bench summary: %d passed, %d failed\n", passed, failed);
        return failed == 0 ? 0 : 1;
    }
};

} // namespace AudioResearch
