// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SignalLab — deterministic test-signal generators for the Aestra Audio Research Bench.
//
// Design rules:
//   * Every generator is fully deterministic: same arguments -> bit-identical buffer,
//     on every platform. Noise uses a fixed-seed xorshift64* generator, never <random>
//     distributions (their output is implementation-defined).
//   * Expectations stay analytic: signals are simple closed forms so tests can compute
//     the expected peak/RMS/DC/harmonic content instead of storing binary fixtures.
//   * Test-side only. This header must never be included from production code.
//
// Doc: AestraDocs/audio-research-bench.md
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace AudioResearch {

constexpr double kTau = 6.28318530717958647692;

/// Interleaved deterministic test buffer. Channel-major sample layout matches the
/// engine convention (frame-interleaved).
struct Signal {
    std::vector<float> samples; // interleaved
    uint32_t channels{2};
    uint32_t sampleRate{48000};

    uint32_t frames() const { return channels > 0 ? static_cast<uint32_t>(samples.size() / channels) : 0; }

    float at(uint32_t frame, uint32_t channel) const {
        return samples[(static_cast<size_t>(frame) * channels) + channel];
    }
    float& at(uint32_t frame, uint32_t channel) {
        return samples[(static_cast<size_t>(frame) * channels) + channel];
    }
};

inline Signal makeSilence(uint32_t sampleRate, uint32_t frames, uint32_t channels = 2) {
    Signal s;
    s.sampleRate = sampleRate;
    s.channels = channels;
    s.samples.assign(static_cast<size_t>(frames) * channels, 0.0f);
    return s;
}

/// Single-sample unit impulse (Kronecker delta) at `position`, all channels.
inline Signal makeImpulse(uint32_t sampleRate, uint32_t frames, uint32_t position, double amplitude = 1.0,
                          uint32_t channels = 2) {
    Signal s = makeSilence(sampleRate, frames, channels);
    if (position < frames) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            s.at(position, ch) = static_cast<float>(amplitude);
        }
    }
    return s;
}

/// 0 -> `level` step at frame `stepAt` (inclusive), all channels.
inline Signal makeStep(uint32_t sampleRate, uint32_t frames, uint32_t stepAt, double level = 1.0,
                       uint32_t channels = 2) {
    Signal s = makeSilence(sampleRate, frames, channels);
    for (uint32_t i = stepAt; i < frames; ++i) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            s.at(i, ch) = static_cast<float>(level);
        }
    }
    return s;
}

/// Constant DC at `level`, all channels.
inline Signal makeDC(uint32_t sampleRate, uint32_t frames, double level, uint32_t channels = 2) {
    Signal s;
    s.sampleRate = sampleRate;
    s.channels = channels;
    s.samples.assign(static_cast<size_t>(frames) * channels, static_cast<float>(level));
    return s;
}

/// Sine at `freqHz`, identical on all channels. Phase in radians.
inline Signal makeSine(uint32_t sampleRate, uint32_t frames, double freqHz, double amplitude = 0.5,
                       uint32_t channels = 2, double phaseRad = 0.0) {
    Signal s;
    s.sampleRate = sampleRate;
    s.channels = channels;
    s.samples.resize(static_cast<size_t>(frames) * channels);
    for (uint32_t i = 0; i < frames; ++i) {
        const double v = amplitude * std::sin((kTau * freqHz * static_cast<double>(i) / sampleRate) + phaseRad);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            s.at(i, ch) = static_cast<float>(v);
        }
    }
    return s;
}

/// Sine at `fraction` of Nyquist (default 0.9 -> e.g. 21.6 kHz at 48 kHz).
inline Signal makeNearNyquistSine(uint32_t sampleRate, uint32_t frames, double amplitude = 0.5,
                                  double fractionOfNyquist = 0.9, uint32_t channels = 2) {
    const double freq = fractionOfNyquist * 0.5 * static_cast<double>(sampleRate);
    return makeSine(sampleRate, frames, freq, amplitude, channels);
}

/// Linear chirp from `f0Hz` to `f1Hz` over the buffer. Instantaneous phase is the
/// integral of the linear frequency ramp: phi(t) = tau * (f0*t + (f1-f0)*t^2 / (2*T)).
inline Signal makeLinearSweep(uint32_t sampleRate, uint32_t frames, double f0Hz, double f1Hz,
                              double amplitude = 0.5, uint32_t channels = 2) {
    Signal s;
    s.sampleRate = sampleRate;
    s.channels = channels;
    s.samples.resize(static_cast<size_t>(frames) * channels);
    const double totalSeconds = static_cast<double>(frames) / sampleRate;
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double phase = kTau * ((f0Hz * t) + ((f1Hz - f0Hz) * t * t / (2.0 * totalSeconds)));
        const double v = amplitude * std::sin(phase);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            s.at(i, ch) = static_cast<float>(v);
        }
    }
    return s;
}

/// Two simultaneous sines (classic IMD / component-separation stimulus).
inline Signal makeDualTone(uint32_t sampleRate, uint32_t frames, double f1Hz, double a1, double f2Hz, double a2,
                           uint32_t channels = 2) {
    Signal s;
    s.sampleRate = sampleRate;
    s.channels = channels;
    s.samples.resize(static_cast<size_t>(frames) * channels);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double v = (a1 * std::sin(kTau * f1Hz * t)) + (a2 * std::sin(kTau * f2Hz * t));
        for (uint32_t ch = 0; ch < channels; ++ch) {
            s.at(i, ch) = static_cast<float>(v);
        }
    }
    return s;
}

/// Ideal (non-band-limited) square wave built by sample index, +A for the first half
/// of each period. `periodFrames` must be even so the duty cycle is exactly 50%.
/// NOTE: deliberately aliased — this is a harmonic-rich torture signal whose
/// low-order odd-harmonic amplitudes follow ~ (4A/pi) * 1/n; tests that assert THD
/// must state which harmonics they count.
inline Signal makeSquare(uint32_t sampleRate, uint32_t frames, uint32_t periodFrames, double amplitude = 0.5,
                         uint32_t channels = 2) {
    Signal s;
    s.sampleRate = sampleRate;
    s.channels = channels;
    s.samples.resize(static_cast<size_t>(frames) * channels);
    const uint32_t half = periodFrames / 2;
    for (uint32_t i = 0; i < frames; ++i) {
        const double v = ((i % periodFrames) < half) ? amplitude : -amplitude;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            s.at(i, ch) = static_cast<float>(v);
        }
    }
    return s;
}

/// Deterministic xorshift64* PRNG (public-domain construction; stable across platforms).
struct DeterministicRng {
    uint64_t state;
    explicit DeterministicRng(uint64_t seed) : state(seed ? seed : 0x9E3779B97F4A7C15ull) {}

    uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1Dull;
    }

    /// Uniform double in [-1, 1).
    double nextBipolar() {
        return static_cast<double>(next() >> 11) * (2.0 / 9007199254740992.0) - 1.0;
    }
};

/// Fixed-seed uniform white noise in [-amplitude, amplitude).
/// `independentChannels`=false writes the same sequence to every channel (correlation +1);
/// true draws each channel independently (correlation ~ 0).
inline Signal makeNoise(uint32_t sampleRate, uint32_t frames, uint64_t seed, double amplitude = 0.5,
                        uint32_t channels = 2, bool independentChannels = false) {
    Signal s;
    s.sampleRate = sampleRate;
    s.channels = channels;
    s.samples.resize(static_cast<size_t>(frames) * channels);
    DeterministicRng rng(seed);
    for (uint32_t i = 0; i < frames; ++i) {
        if (independentChannels) {
            for (uint32_t ch = 0; ch < channels; ++ch) {
                s.at(i, ch) = static_cast<float>(amplitude * rng.nextBipolar());
            }
        } else {
            const float v = static_cast<float>(amplitude * rng.nextBipolar());
            for (uint32_t ch = 0; ch < channels; ++ch) {
                s.at(i, ch) = v;
            }
        }
    }
    return s;
}

/// Rectangular-gated sine burst: silence, then `burstFrames` of sine, then silence.
/// Hard edges are intentional — this is the transient-smear stimulus.
inline Signal makeTransientBurst(uint32_t sampleRate, uint32_t frames, uint32_t burstStart, uint32_t burstFrames,
                                 double freqHz, double amplitude = 0.5, uint32_t channels = 2) {
    Signal s = makeSilence(sampleRate, frames, channels);
    const uint32_t end = std::min(frames, burstStart + burstFrames);
    for (uint32_t i = burstStart; i < end; ++i) {
        const double t = static_cast<double>(i - burstStart) / sampleRate;
        const double v = amplitude * std::sin(kTau * freqHz * t);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            s.at(i, ch) = static_cast<float>(v);
        }
    }
    return s;
}

enum class StereoMode {
    InPhase,      // R = L            (correlation +1)
    Inverted,     // R = -L           (correlation -1)
    Decorrelated  // independent noise (correlation ~ 0)
};

/// Stereo correlation/polarity stimulus. InPhase/Inverted use a sine; Decorrelated
/// uses independent fixed-seed noise per channel.
inline Signal makeStereoCase(uint32_t sampleRate, uint32_t frames, StereoMode mode, double freqHz = 997.0,
                             double amplitude = 0.5, uint64_t seed = 0xA35712345678ull) {
    if (mode == StereoMode::Decorrelated) {
        return makeNoise(sampleRate, frames, seed, amplitude, 2, /*independentChannels=*/true);
    }
    Signal s = makeSine(sampleRate, frames, freqHz, amplitude, 2);
    if (mode == StereoMode::Inverted) {
        for (uint32_t i = 0; i < s.frames(); ++i) {
            s.at(i, 1) = -s.at(i, 1);
        }
    }
    return s;
}

} // namespace AudioResearch
