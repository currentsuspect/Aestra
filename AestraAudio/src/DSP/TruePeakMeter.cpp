// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "DSP/TruePeakMeter.h"

#include <cmath>

namespace Aestra {
namespace Audio {

namespace {
// Kaiser β = 9.4 → ≈ 80 dB stopband, generous transition band → safely above
// the BS.1770-4 minimum of 60 dB even with only 12 taps/phase.
constexpr double kKaiserBeta = 9.4;
constexpr double kPi = 3.14159265358979323846;
} // namespace

TruePeakMeter::TruePeakMeter() {
    generateCoefficients();
    m_initialized = true;
}

void TruePeakMeter::initialize(uint32_t sampleRate) noexcept {
    m_sampleRate = sampleRate ? sampleRate : 48000;
    generateCoefficients();
    reset();
    m_initialized = true;
}

void TruePeakMeter::reset() noexcept {
    m_historyL.fill(0.0f);
    m_historyR.fill(0.0f);
    m_samplePeakL = 0.0f;
    m_samplePeakR = 0.0f;
    m_truePeakL = 0.0f;
    m_truePeakR = 0.0f;
}

double TruePeakMeter::besselI0(double x) noexcept {
    // Series approximation, sufficient for |x| up to ~20.
    double sum = 1.0;
    double term = 1.0;
    const double xx = (x * x) * 0.25;
    for (int k = 1; k < 50; ++k) {
        term *= xx / static_cast<double>(k * k);
        sum += term;
        if (term < 1e-18 * sum) {
            break;
        }
    }
    return sum;
}

double TruePeakMeter::kaiser(double n, double N, double beta) noexcept {
    // Standard Kaiser window over n ∈ [0, N-1].
    const double r = (2.0 * n / (N - 1.0)) - 1.0;
    const double arg = beta * std::sqrt(1.0 - r * r);
    return besselI0(arg) / besselI0(beta);
}

void TruePeakMeter::generateCoefficients() noexcept {
    // Prototype FIR: windowed sinc, designed at the 4× output rate with
    // cutoff at the Nyquist of the original rate (i.e. fs/2 → ω = π/4 in
    // the 4× domain). Centre delay = (kFilterTaps - 1) / 2 = 23.5.
    constexpr uint32_t L = kFilterTaps;
    constexpr double centre = (L - 1.0) * 0.5;
    constexpr double cutoff = 1.0 / static_cast<double>(kOversampleFactor); // π × cutoff

    // Generate prototype with windowing.
    double proto[L];
    double sum = 0.0;
    for (uint32_t n = 0; n < L; ++n) {
        const double t = static_cast<double>(n) - centre;
        double sinc;
        if (std::abs(t) < 1e-12) {
            sinc = cutoff;
        } else {
            const double arg = kPi * cutoff * t;
            sinc = std::sin(arg) / (kPi * t);
        }
        const double w = kaiser(static_cast<double>(n), static_cast<double>(L), kKaiserBeta);
        proto[n] = sinc * w;
        sum += proto[n];
    }

    (void)sum; // global sum no longer used (we per-phase normalise below)

    // Polyphase decomposition: e_k[m] = h[k + kOversampleFactor * m].
    // History is "newest first" (hist[0] = x[n], hist[1] = x[n-1], ...),
    // so when convolving y = Σ e_k[m] · x[n-m] we can index coeffs[tap] *
    // hist[tap] directly.
    //
    // Normalise EACH polyphase sub-filter so its taps sum to 1.0. This
    // guarantees unity DC gain at every fractional phase — without it the
    // oversampled signal can drift several percent above/below |x| on DC,
    // which would corrupt the true-peak measurement.
    for (uint32_t phase = 0; phase < kOversampleFactor; ++phase) {
        double phaseSum = 0.0;
        for (uint32_t tap = 0; tap < kTapsPerPhase; ++tap) {
            const uint32_t protoIdx = phase + kOversampleFactor * tap;
            phaseSum += proto[protoIdx];
        }
        const double phaseScale = phaseSum != 0.0 ? 1.0 / phaseSum : 1.0;
        for (uint32_t tap = 0; tap < kTapsPerPhase; ++tap) {
            const uint32_t protoIdx = phase + kOversampleFactor * tap;
            m_polyphase[phase][tap] = static_cast<float>(proto[protoIdx] * phaseScale);
        }
    }
}

void TruePeakMeter::processStereo(const float* interleavedStereo, uint32_t numFrames) noexcept {
    if (!interleavedStereo || numFrames == 0) {
        return;
    }
    for (uint32_t i = 0; i < numFrames; ++i) {
        const float l = interleavedStereo[i * 2 + 0];
        const float r = interleavedStereo[i * 2 + 1];

        const float al = l < 0.0f ? -l : l;
        const float ar = r < 0.0f ? -r : r;
        if (al > m_samplePeakL) m_samplePeakL = al;
        if (ar > m_samplePeakR) m_samplePeakR = ar;
        if (al > m_truePeakL)   m_truePeakL = al; // sample is one of the 4× outputs (phase 0 ≈ identity at centre)
        if (ar > m_truePeakR)   m_truePeakR = ar;

        pushHistory(m_historyL, l);
        pushHistory(m_historyR, r);

        updateTruePeakFromHistory(m_historyL, m_truePeakL);
        updateTruePeakFromHistory(m_historyR, m_truePeakR);
    }
}

void TruePeakMeter::processMono(const float* mono, uint32_t numFrames) noexcept {
    if (!mono || numFrames == 0) {
        return;
    }
    for (uint32_t i = 0; i < numFrames; ++i) {
        const float s = mono[i];
        const float a = s < 0.0f ? -s : s;
        if (a > m_samplePeakL) m_samplePeakL = a;
        if (a > m_truePeakL)   m_truePeakL = a;
        pushHistory(m_historyL, s);
        updateTruePeakFromHistory(m_historyL, m_truePeakL);
    }
    // Mirror to R so getMax... helpers behave for mono callers too.
    if (m_samplePeakL > m_samplePeakR) m_samplePeakR = m_samplePeakL;
    if (m_truePeakL  > m_truePeakR)    m_truePeakR  = m_truePeakL;
}

} // namespace Audio
} // namespace Aestra
