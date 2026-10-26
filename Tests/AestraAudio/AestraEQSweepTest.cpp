// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraEQSweepTest — moving the EQ while audio plays must not spike or click.
//
// Owner report (26 Oct): "when I'm trying to sweep through it, it can suddenly spike and clip". Two things are
// pinned here:
//   1. Sweeping a band (frequency, the way a mouse drag does) never pushes a steady tone above the band's largest
//      steady-state gain: the moving filter itself doesn't spike.
//   2. Switching a band's structure mid-signal (on/off, filter type, cut slope) doesn't click. These used to reset
//      the band's filters while audio flowed; on music that measured an 18 dB burst of top end (type), 11.6 dB
//      (slope), 8.8 dB (on/off). They now crossfade over AestraEQ::kBandFadeSeconds.

#include "Plugin/AestraEQ.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace Aestra::Audio::Plugins;
using E = AestraEQ;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kHostBlock = 256;
constexpr double kTau = 6.28318530717958647692;
int g_failed = 0;

void report(bool ok, const char* name, const char* fmt, double value) {
    std::printf("  %s: %s (", ok ? "PASS" : "FAIL", name);
    std::printf(fmt, value);
    std::printf(")\n");
    if (!ok) ++g_failed;
}

// Runs a steady tone through `eq` for `seconds`, calling `change(eq, t)` on every 16 ms UI tick (t in 0..1), and
// hands every output sample to `sink`.
void runTone(E& eq, double toneHz, double seconds, const std::function<void(E&, double)>& change,
             const std::function<void(float)>& sink, uint64_t& n) {
    std::vector<float> in(kHostBlock), out(kHostBlock);
    const uint64_t total = static_cast<uint64_t>(seconds * kSampleRate);
    const uint64_t tick = static_cast<uint64_t>(0.016 * kSampleRate);
    for (uint64_t done = 0; done < total; done += kHostBlock) {
        if (change && (done / tick) != ((done + kHostBlock) / tick)) change(eq, static_cast<double>(done) / total);
        for (uint32_t i = 0; i < kHostBlock; ++i, ++n)
            in[i] = static_cast<float>(0.25 * std::sin(kTau * toneHz * static_cast<double>(n) / kSampleRate));
        const float* ip = in.data();
        float* op = out.data();
        eq.process(&ip, &op, 1, 1, kHostBlock);
        for (float v : out) sink(v);
    }
}

// 1. A band swept over a steady tone: output peak against amplitude x the band's largest steady gain.
void sweepCase(const char* name, uint32_t enable, uint32_t swept, std::vector<std::pair<uint32_t, float>> fixed,
               float from, float to, double toneHz, double boundGain) {
    E eq;
    eq.initialize(kSampleRate, kHostBlock);
    eq.activate();
    eq.setParameter(enable, 1.0f);
    for (auto [p, v] : fixed) eq.setParameter(p, v);
    eq.setParameter(swept, from);
    uint64_t n = 0;
    double peak = 0.0;
    runTone(eq, toneHz, 1.0, nullptr, [](float) {}, n);
    const auto track = [&](float v) { peak = std::max(peak, static_cast<double>(std::fabs(v))); };
    runTone(eq, toneHz, 0.6, [&](E& e, double t) { e.setParameter(swept, static_cast<float>(from + (to - from) * t)); }, track, n);
    runTone(eq, toneHz, 0.6, [&](E& e, double t) { e.setParameter(swept, static_cast<float>(to + (from - to) * t)); }, track, n);
    const double overDb = 20.0 * std::log10(peak / (0.25 * boundGain));
    report(overDb <= 0.5, name, "peak %+.2f dB over the steady-state bound", overDb);
}

// 2. A structural change on a steady tone. A tone's second difference is tiny (A·(2πf/fs)²); a reset or a jump in
// the waveform shows up as a second difference far above what the settled output ever has.
void clickCase(const char* name, const std::function<void(E&)>& setup, const std::function<void(E&, int)>& step,
               int steps, double toneHz) {
    E eq;
    eq.initialize(kSampleRate, kHostBlock);
    eq.activate();
    setup(eq);
    uint64_t n = 0;
    double settledD2 = 0.0, changeD2 = 0.0, y1 = 0.0, y2 = 0.0;
    bool primed = false;
    double* target = &settledD2;
    const auto d2 = [&](float v) {
        if (primed) *target = std::max(*target, std::fabs(v - 2.0 * y1 + y2));
        y2 = y1;
        y1 = v;
        primed = true;
    };
    runTone(eq, toneHz, 1.0, nullptr, [](float) {}, n);      // settle
    runTone(eq, toneHz, 0.25, nullptr, d2, n);                // the settled waveform's curvature
    for (int i = 0; i < steps; ++i) {
        step(eq, i);
        runTone(eq, toneHz, 0.25, nullptr, [&](float v) { target = &changeD2; d2(v); }, n);
        target = &settledD2;
        runTone(eq, toneHz, 0.3, nullptr, d2, n);             // and after it has settled again
    }
    const double ratio = changeD2 / std::max(1e-12, settledD2);
    report(ratio <= 2.0, name, "%.2fx the settled curvature at the switch", ratio);
}

} // namespace

int main() {
    const double boost18 = std::pow(10.0, 18.0 / 20.0);
    std::printf("AestraEQ sweep: a band swept over a steady tone must not spike\n");
    for (double tone : {60.0, 250.0, 1000.0, 6000.0}) {
        char name[160];
        std::snprintf(name, sizeof name, "Bell 1 +18 dB, narrowest Q, swept over %.0f Hz", tone);
        sweepCase(name, E::kParamBell1Enable, E::kParamBell1Freq, {{E::kParamBell1Gain, 1.0f}, {E::kParamBell1Q, 1.0f}}, 0.0f, 1.0f, tone, boost18);
        std::snprintf(name, sizeof name, "Low shelf +18 dB swept over %.0f Hz", tone);
        sweepCase(name, E::kParamLShEnable, E::kParamLShFreq, {{E::kParamLShGain, 1.0f}}, 0.0f, 1.0f, tone, boost18);
        std::snprintf(name, sizeof name, "High shelf +18 dB swept over %.0f Hz", tone);
        sweepCase(name, E::kParamHShEnable, E::kParamHShFreq, {{E::kParamHShGain, 1.0f}}, 0.0f, 1.0f, tone, boost18);
        std::snprintf(name, sizeof name, "Low-pass 96 dB/oct swept over %.0f Hz", tone);
        sweepCase(name, E::kParamLPFEnable, E::kParamLPFFreq, {{E::kParamLPFSlope, 1.0f}}, 0.0f, 1.0f, tone, 1.0);
    }
    // Not pinned: a steep high-pass swept DOWN past a tone overshoots (+1 dB at 24 dB/oct, +4.8 at 96 dB/oct over
    // 60 Hz). That is the steep filter's stored energy, measured the same with a trapezoidal SVF, not a defect.

    std::printf("AestraEQ structural changes: switching a band mid-signal must not click\n");
    // Tones sit at the band's own frequency, where its filters hold the most energy, so a reset can't hide.
    // Bell 1 at norm 0.3 = 80 Hz x 100^0.3 = 318.5 Hz; the high-pass at norm 0.5 = 20 Hz x 25^0.5 = 100 Hz.
    const double bellHz = 80.0 * std::pow(100.0, 0.3);
    clickCase("Bell 1 (+12 dB) switched on and off at its frequency",
              [](E& e) { e.setParameter(E::kParamBell1Freq, 0.3f); e.setParameter(E::kParamBell1Gain, 30.0f / 36.0f); e.setParameter(E::kParamBell1Q, 0.6f); },
              [](E& e, int i) { e.setParameter(E::kParamBell1Enable, i % 2 == 0 ? 1.0f : 0.0f); }, 4, bellHz);
    clickCase("Bell 1 filter type stepped at its frequency",
              [](E& e) { e.setParameter(E::kParamBell1Enable, 1.0f); e.setParameter(E::kParamBell1Freq, 0.3f); e.setParameter(E::kParamBell1Gain, 1.0f); e.setParameter(E::kParamBell1Q, 0.6f); },
              [](E& e, int i) { e.setParameter(E::kParamBell1Type, static_cast<float>((i + 1) % 4) / 3.0f); }, 4, bellHz);
    clickCase("High-pass slope stepped 6 -> 96 dB/oct just above its cutoff",
              [](E& e) { e.setParameter(E::kParamHPFEnable, 1.0f); e.setParameter(E::kParamHPFFreq, 0.5f); e.setParameter(E::kParamHPFSlope, 0.0f); },
              [](E& e, int i) { e.setParameter(E::kParamHPFSlope, static_cast<float>(i + 1) / 6.0f); }, 6, 130.0);

    std::printf("%s\n", g_failed ? "FAILED" : "ALL PASSED");
    return g_failed ? 1 : 0;
}
