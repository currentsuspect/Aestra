// DCBlockerTest.cpp — unit tests for the master-bus DC blocker (DSP/DCBlocker.h).
//
// Verifies the one-pole DC blocker actually removes a DC offset, leaves the
// audible band essentially untouched, keeps silence silent, and clears its
// state on reset(). This is the DSP that backs the Audio Settings "DC Removal"
// toggle (#376).

#include "DSP/DCBlocker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using Aestra::Audio::DCBlocker;

namespace {

// M_PI is not defined by MSVC without _USE_MATH_DEFINES; use a local constant so
// the test builds identically across GCC/Clang/MSVC.
constexpr double kPi = 3.14159265358979323846;

int g_failures = 0;

void check(bool cond, const char* msg) {
    if (cond) {
        std::printf("[PASS] %s\n", msg);
    } else {
        std::printf("[FAIL] %s\n", msg);
        ++g_failures;
    }
}

// Feed a constant offset for `n` samples; return the last output sample.
double lastOutForDC(DCBlocker& b, double dc, int n) {
    double y = 0.0;
    for (int i = 0; i < n; ++i)
        y = b.process(dc);
    return y;
}

// Removes a steady DC offset: output converges toward zero.
void test_removes_dc() {
    DCBlocker b;
    const double dc = 0.5;
    const double y = lastOutForDC(b, dc, 48000); // ~1s at 48k
    check(std::fabs(y) < 1e-3, "steady 0.5 DC is driven below 1e-3 after ~1s");

    DCBlocker b2;
    const double yNeg = lastOutForDC(b2, -0.25, 48000);
    check(std::fabs(yNeg) < 1e-3, "steady -0.25 DC is driven below 1e-3 after ~1s");
}

// A pure sine (well above the corner) passes with negligible amplitude loss.
void test_preserves_audio_band() {
    DCBlocker b;
    const double sr = 48000.0;
    const double freq = 1000.0;
    const int n = static_cast<int>(sr); // 1s, whole number of cycles-ish
    double peakIn = 0.0;
    double peakOut = 0.0;
    // Skip the first 100 ms so the filter transient settles before measuring.
    const int settle = 4800;
    for (int i = 0; i < n; ++i) {
        const double x = std::sin(2.0 * kPi * freq * static_cast<double>(i) / sr);
        const double y = b.process(x);
        if (i >= settle) {
            peakIn = std::max(peakIn, std::fabs(x));
            peakOut = std::max(peakOut, std::fabs(y));
        }
    }
    const double loss = std::fabs(peakOut - peakIn) / peakIn;
    check(loss < 0.01, "1 kHz sine passes with <1% peak change");
}

// DC + audio: the DC is removed while the AC content remains centered on zero.
void test_dc_plus_sine_recenters() {
    DCBlocker b;
    const double sr = 48000.0;
    const double freq = 500.0;
    const double dc = 0.3;
    const int n = 2 * static_cast<int>(sr); // 2 s
    // Settle a full second so the DC residual has fully decayed (corner is only a
    // few Hz), then measure over exactly 500 cycles of the 500 Hz tone so the AC
    // content sums to zero and only a leftover DC offset could move the mean.
    const int settle = static_cast<int>(sr);
    double sum = 0.0;
    int counted = 0;
    for (int i = 0; i < n; ++i) {
        const double x = dc + 0.4 * std::sin(2.0 * kPi * freq * static_cast<double>(i) / sr);
        const double y = b.process(x);
        if (i >= settle) {
            sum += y;
            ++counted;
        }
    }
    const double mean = sum / static_cast<double>(counted);
    check(std::fabs(mean) < 1e-3, "DC+sine output mean is ~0 (offset removed, AC kept)");
}

// Silence in, silence out — never generates energy from nothing.
void test_silence_stays_silent() {
    DCBlocker b;
    double maxAbs = 0.0;
    for (int i = 0; i < 4800; ++i)
        maxAbs = std::max(maxAbs, std::fabs(b.process(0.0)));
    check(maxAbs == 0.0, "silence in -> exact silence out");
}

// reset() clears state so a re-enabled blocker starts from a known-zero point.
void test_reset_clears_state() {
    DCBlocker b;
    lastOutForDC(b, 0.8, 1000); // build up state
    b.reset();
    // First sample after reset with a fresh DC step equals the input (x - 0 + 0).
    const double first = b.process(0.2);
    check(std::fabs(first - 0.2) < 1e-12, "after reset(), first output equals first input");
}

} // namespace

int main() {
    std::printf("========================================\n");
    std::printf("  DCBlocker DSP Tests\n");
    std::printf("========================================\n\n");

    test_removes_dc();
    test_preserves_audio_band();
    test_dc_plus_sine_recenters();
    test_silence_stays_silent();
    test_reset_clears_state();

    std::printf("\n========================================\n");
    if (g_failures == 0)
        std::printf("  All DCBlocker tests passed.\n");
    else
        std::printf("  %d DCBlocker test(s) failed.\n", g_failures);
    std::printf("========================================\n");
    return g_failures == 0 ? 0 : 1;
}
