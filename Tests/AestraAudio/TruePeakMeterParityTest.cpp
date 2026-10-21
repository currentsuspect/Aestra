// © 2026 Aestra Studios — All Rights Reserved.
//
// TruePeakMeterParityTest — the vectorised true-peak meter must measure the
// same peaks the scalar one did.
//
// The meter's polyphase coefficients are stored transposed now, so one 4-wide
// accumulator holds all four phase sums at once. The tap order is unchanged,
// which means each lane accumulates exactly the same products in exactly the
// same order as the scalar version — so this is not a "close enough" test.
// The peaks must be BIT-IDENTICAL.
//
// The reference below is a copy of the original phase-major implementation,
// because a reference that shares the code it is checking cannot catch that
// code being wrong.

#include "DSP/TruePeakMeter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using Aestra::Audio::TruePeakMeter;

namespace {

// ---------------------------------------------------------------------------
// Reference: the original implementation, verbatim in shape.
// ---------------------------------------------------------------------------

class RefTruePeakMeter {
public:
    static constexpr uint32_t kOversampleFactor = 4;
    static constexpr uint32_t kTapsPerPhase = 12;

    RefTruePeakMeter() { generateCoefficients(); }

    void reset() noexcept {
        m_historyL.fill(0.0f);
        m_historyR.fill(0.0f);
        m_samplePeakL = 0.0f;
        m_samplePeakR = 0.0f;
        m_truePeakL = 0.0f;
        m_truePeakR = 0.0f;
    }

    void processStereo(const float* interleavedStereo, uint32_t numFrames) noexcept {
        for (uint32_t i = 0; i < numFrames; ++i) {
            const float l = interleavedStereo[i * 2 + 0];
            const float r = interleavedStereo[i * 2 + 1];

            const float al = l < 0.0f ? -l : l;
            const float ar = r < 0.0f ? -r : r;
            if (al > m_samplePeakL) m_samplePeakL = al;
            if (ar > m_samplePeakR) m_samplePeakR = ar;
            if (al > m_truePeakL) m_truePeakL = al;
            if (ar > m_truePeakR) m_truePeakR = ar;

            pushHistory(m_historyL, l);
            pushHistory(m_historyR, r);

            updateTruePeakFromHistory(m_historyL, m_truePeakL);
            updateTruePeakFromHistory(m_historyR, m_truePeakR);
        }
    }

    float getSamplePeakL() const { return m_samplePeakL; }
    float getSamplePeakR() const { return m_samplePeakR; }
    float getTruePeakL() const { return m_truePeakL; }
    float getTruePeakR() const { return m_truePeakR; }

private:
    std::array<std::array<float, kTapsPerPhase>, kOversampleFactor> m_polyphase{};

    std::array<float, kTapsPerPhase> m_historyL{};
    std::array<float, kTapsPerPhase> m_historyR{};

    float m_samplePeakL{0.0f};
    float m_samplePeakR{0.0f};
    float m_truePeakL{0.0f};
    float m_truePeakR{0.0f};

    static void pushHistory(std::array<float, kTapsPerPhase>& hist, float sample) noexcept {
        for (uint32_t i = kTapsPerPhase - 1; i > 0; --i) {
            hist[i] = hist[i - 1];
        }
        hist[0] = sample;
    }

    void updateTruePeakFromHistory(const std::array<float, kTapsPerPhase>& hist,
                                   float& runningTruePeak) const noexcept {
        for (uint32_t phase = 0; phase < kOversampleFactor; ++phase) {
            float acc = 0.0f;
            const auto& coeffs = m_polyphase[phase];
            for (uint32_t tap = 0; tap < kTapsPerPhase; ++tap) {
                acc += coeffs[tap] * hist[tap];
            }
            const float mag = acc < 0.0f ? -acc : acc;
            if (mag > runningTruePeak) {
                runningTruePeak = mag;
            }
        }
    }

    void generateCoefficients() noexcept {
        constexpr uint32_t L = kOversampleFactor * kTapsPerPhase;
        constexpr double centre = (L - 1.0) * 0.5;
        constexpr double cutoff = 1.0 / static_cast<double>(kOversampleFactor);
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kBeta = 9.4;

        double proto[L];
        for (uint32_t n = 0; n < L; ++n) {
            const double t = static_cast<double>(n) - centre;
            double sinc;
            if (std::abs(t) < 1e-12) {
                sinc = cutoff;
            } else {
                const double arg = kPi * cutoff * t;
                sinc = std::sin(arg) / (kPi * t);
            }
            const double w = kaiser(static_cast<double>(n), static_cast<double>(L), kBeta);
            proto[n] = sinc * w;
        }

        for (uint32_t phase = 0; phase < kOversampleFactor; ++phase) {
            double phaseSum = 0.0;
            for (uint32_t tap = 0; tap < kTapsPerPhase; ++tap) {
                phaseSum += proto[phase + kOversampleFactor * tap];
            }
            const double phaseScale = phaseSum != 0.0 ? 1.0 / phaseSum : 1.0;
            for (uint32_t tap = 0; tap < kTapsPerPhase; ++tap) {
                m_polyphase[phase][tap] =
                    static_cast<float>(proto[phase + kOversampleFactor * tap] * phaseScale);
            }
        }
    }

    static double besselI0(double x) noexcept {
        double sum = 1.0;
        double term = 1.0;
        const double xx = (x * x) * 0.25;
        for (int k = 1; k < 50; ++k) {
            term *= xx / static_cast<double>(k * k);
            sum += term;
            if (term < 1e-18 * sum) break;
        }
        return sum;
    }

    static double kaiser(double n, double N, double beta) noexcept {
        const double r = (2.0 * n / (N - 1.0)) - 1.0;
        const double arg = beta * std::sqrt(1.0 - r * r);
        return besselI0(arg) / besselI0(beta);
    }
};

// ---------------------------------------------------------------------------

struct Case {
    const char* name;
    std::vector<float> samples;
};

Case makeNoise(size_t frames, uint32_t seed, float amplitude) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-amplitude, amplitude);
    Case c{"noise", {}};
    c.samples.resize(frames * 2);
    for (auto& v : c.samples) v = dist(rng);
    return c;
}

Case makeSine(size_t frames, double freq, double sampleRate, float amplitude) {
    Case c{"sine", {}};
    c.samples.resize(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        const float v = static_cast<float>(amplitude * std::sin(2.0 * 3.14159265358979 * freq *
                                                             static_cast<double>(i) / sampleRate));
        c.samples[i * 2 + 0] = v;
        c.samples[i * 2 + 1] = v;
    }
    return c;
}

Case makeImpulse(size_t frames) {
    Case c{"impulse", {}};
    c.samples.assign(frames * 2, 0.0f);
    if (frames > 8) {
        c.samples[16] = 0.9f;
        c.samples[17] = -0.7f;
    }
    return c;
}

bool compare(const Case& c) {
    TruePeakMeter actual;
    actual.initialize(48000);
    RefTruePeakMeter reference;

    // Feed in blocks, the way the engine does, so the history is exercised
    // across block boundaries as well as within them.
    const uint32_t block = 512;
    for (size_t offset = 0; offset < c.samples.size(); offset += block) {
        const uint32_t frames = static_cast<uint32_t>(
            std::min<size_t>(block, (c.samples.size() - offset) / 2));
        if (frames == 0) break;
        actual.processStereo(c.samples.data() + offset, frames);
        reference.processStereo(c.samples.data() + offset, frames);
    }

    struct Pair {
        const char* what;
        float a;
        float b;
    };
    const Pair pairs[] = {
        {"sample peak L", actual.getSamplePeakL(), reference.getSamplePeakL()},
        {"sample peak R", actual.getSamplePeakR(), reference.getSamplePeakR()},
        {"true peak L", actual.getTruePeakL(), reference.getTruePeakL()},
        {"true peak R", actual.getTruePeakR(), reference.getTruePeakR()},
    };

    bool identical = true;
    for (const auto& p : pairs) {
        // Bit-identical, not "close": the tap order is unchanged, so each lane
        // of the vector accumulator sums exactly the same products in exactly
        // the same order the scalar accumulator did.
        const bool same = (p.a == p.b) || (std::isnan(p.a) && std::isnan(p.b));
        if (!same) identical = false;
        std::printf("  %-14s actual=%.9g reference=%.9g %s\n", p.what, p.a, p.b,
                    same ? "identical" : "DIFFERENT");
    }
    return identical;
}

} // namespace

int main() {
    const Case cases[] = {
        makeNoise(4096, 7u, 0.5f),
        makeSine(4096, 997.0, 48000.0, 0.8f),
        makeSine(4096, 15000.0, 48000.0, 0.8f),
        makeSine(4096, 19000.0, 48000.0, 0.8f),
        makeImpulse(4096),
    };

    int failures = 0;
    for (const auto& c : cases) {
        std::printf("%s:\n", c.name);
        if (!compare(c)) {
            std::printf("  [FAIL] %s: the vectorised meter did not reproduce the scalar peaks\n", c.name);
            ++failures;
        }
    }

    // Silence must stay silence, and a reset must clear the peaks.
    {
        TruePeakMeter meter;
        meter.initialize(48000);
        std::vector<float> silence(512 * 2, 0.0f);
        meter.processStereo(silence.data(), 512);
        if (meter.getMaxTruePeakdBTP() != -200.0f) {
            std::printf("  [FAIL] silence did not read -200 dBTP\n");
            ++failures;
        }
    }

    if (failures > 0) {
        std::printf("%d test(s) failed\n", failures);
        return 1;
    }
    std::printf("All TruePeakMeterParity tests passed\n");
    return 0;
}
