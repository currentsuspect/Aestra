// © 2026 Aestra Studios — All Rights Reserved.
#include "DSP/ClipPrefilter.h"

#include "DSP/Interpolators.h" // PI, sinc(), kaiserWindow()

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {
namespace ClipPrefilter {

namespace {
constexpr double kAttenDb = 100.0;
constexpr double kPassbandFraction = 0.9; // passband edge as a fraction of dst Nyquist
} // namespace

std::vector<double> design(uint32_t sourceRate, uint32_t targetRate) {
    std::vector<double> h;
    if (!isNeeded(sourceRate, targetRate)) {
        return h;
    }
    const double passHz = kPassbandFraction * 0.5 * static_cast<double>(targetRate);
    const double stopHz = 0.5 * static_cast<double>(targetRate);

    // Textbook Kaiser design: beta = 0.1102(A - 8.7) for A > 50 dB,
    // N ~= (A - 7.95) / (2.285 * transitionWidthRadians), odd length so the group
    // delay (taps-1)/2 is an integer and apply() can compensate it exactly.
    const double dOmega = 2.0 * Interpolators::PI * (stopHz - passHz) / static_cast<double>(sourceRate);
    int taps = static_cast<int>(std::ceil((kAttenDb - 7.95) / (2.285 * dOmega))) + 1;
    if ((taps % 2) == 0) {
        ++taps;
    }
    const double beta = 0.1102 * (kAttenDb - 8.7);
    const double fc = 0.5 * (passHz + stopHz) / static_cast<double>(sourceRate); // cycles/sample
    const int mid = (taps - 1) / 2;

    h.resize(static_cast<size_t>(taps));
    double sum = 0.0;
    for (int i = 0; i < taps; ++i) {
        const double x = static_cast<double>(i - mid);
        const double v = 2.0 * fc * Interpolators::sinc(2.0 * fc * x) *
                         Interpolators::kaiserWindow(static_cast<double>(i), static_cast<double>(taps), beta);
        h[static_cast<size_t>(i)] = v;
        sum += v;
    }
    for (double& v : h) {
        v /= sum; // unity DC gain, exactly (matches the legacy kernel's normalization policy)
    }
    return h;
}

void apply(const float* in, float* out, uint64_t frames, uint32_t channels, const std::vector<double>& h) {
    if (in == nullptr || out == nullptr || frames == 0 || channels == 0 || h.empty()) {
        return;
    }
    const int taps = static_cast<int>(h.size());
    const int mid = (taps - 1) / 2;
    const int64_t n = static_cast<int64_t>(frames);
    for (int64_t i = 0; i < n; ++i) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            double acc = 0.0;
            for (int k = 0; k < taps; ++k) {
                const int64_t j = i + mid - k; // delay-compensated read
                if (j < 0 || j >= n) {
                    continue;
                }
                acc += h[static_cast<size_t>(k)] *
                       static_cast<double>(in[(static_cast<uint64_t>(j) * channels) + ch]);
            }
            out[(static_cast<uint64_t>(i) * channels) + ch] = static_cast<float>(acc);
        }
    }
}

} // namespace ClipPrefilter
} // namespace Audio
} // namespace Aestra
