// AestraVerb consistency probe — hunts for inconsistency/offness beyond the
// existing safety checks. Hard invariants (bypass parity, NaN rejection,
// silence tail, state roundtrip) assert and fail the process; the remaining
// measurements print diagnostics for review (Low Cut mapping, freeze stability,
// sample-rate consistency, mode-switch clicks, early stereo correlation, mono
// L/R balance).

#include "AestraVerb.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraVerb;

namespace {

int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << (cond ? "[ ok ] " : "[FAIL] ") << what << "\n";
    if (!cond) ++g_failures;
}

struct Stereo {
    std::vector<float> l, r;
};

// Render a stereo input through the verb in fixed blocks.
Stereo render(AestraVerb& v, const std::vector<float>& inL, const std::vector<float>& inR, uint32_t block) {
    const size_t n = inL.size();
    Stereo out{std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f)};
    for (size_t off = 0; off < n; off += block) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(block, n - off));
        const float* ins[2] = {inL.data() + off, inR.data() + off};
        float* outs[2] = {out.l.data() + off, out.r.data() + off};
        v.process(ins, outs, 2, 2, frames);
    }
    return out;
}

Stereo impulseIn(size_t n) {
    Stereo s{std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f)};
    s.l[0] = 1.0f;
    s.r[0] = 1.0f;
    return s;
}

std::vector<float> whiteNoise(size_t n, uint32_t seed, float amp = 0.25f) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-amp, amp);
    std::vector<float> v(n);
    for (auto& x : v) x = d(rng);
    return v;
}

double rms(const std::vector<float>& v, size_t from, size_t to) {
    double acc = 0.0;
    size_t cnt = 0;
    for (size_t i = from; i < to && i < v.size(); ++i) { acc += double(v[i]) * v[i]; ++cnt; }
    return cnt ? std::sqrt(acc / double(cnt)) : 0.0;
}

double toDb(double lin) { return 20.0 * std::log10(std::max(lin, 1e-12)); }

void configureHall(AestraVerb& v) {
    v.setParameter(AestraVerb::kMode, AestraVerb::modeParam(AestraVerb::Mode::Hall));
    v.setParameter(AestraVerb::kDecay, 0.7f);
    v.setParameter(AestraVerb::kSize, 0.7f);
    v.setParameter(AestraVerb::kDiffusion, 0.7f);
    v.setParameter(AestraVerb::kModRate, 0.4f);
    v.setParameter(AestraVerb::kModDepth, 0.15f);
    v.setParameter(AestraVerb::kMix, 1.0f);
    v.setParameter(AestraVerb::kWidth, 0.7f);
    v.setParameter(AestraVerb::kLowCut, 0.0f);
    v.setParameter(AestraVerb::kHighCut, 1.0f);
}

// ---------------------------------------------------------------------------
// Hard invariants
// ---------------------------------------------------------------------------

void probeBypassParity() {
    AestraVerb v;
    v.initialize(48000.0, 256);
    configureHall(v);
    v.setParameter(AestraVerb::kBypass, 1.0f);
    v.activate();
    auto nL = whiteNoise(2048, 1), nR = whiteNoise(2048, 2);
    auto out = render(v, nL, nR, 128);
    float maxDiff = 0.0f;
    for (size_t i = 0; i < nL.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(out.l[i] - nL[i]));
        maxDiff = std::max(maxDiff, std::abs(out.r[i] - nR[i]));
    }
    check(maxDiff == 0.0f, "bypass is a bit-exact dry passthrough (maxDiff=" + std::to_string(maxDiff) + ")");
}

void probeNaNRejection() {
    AestraVerb v;
    v.initialize(48000.0, 256);
    configureHall(v);
    v.activate();
    std::vector<float> inL(512, 0.0f), inR(512, 0.0f);
    inL[10] = std::numeric_limits<float>::quiet_NaN();
    inR[20] = std::numeric_limits<float>::infinity();
    inL[30] = -std::numeric_limits<float>::infinity();
    auto out = render(v, inL, inR, 64);
    bool allFinite = true;
    for (size_t i = 0; i < out.l.size(); ++i)
        allFinite &= std::isfinite(out.l[i]) && std::isfinite(out.r[i]);
    check(allFinite, "NaN/Inf input produces only finite output");
}

void probeSilenceTail() {
    AestraVerb v;
    v.initialize(48000.0, 256);
    configureHall(v);
    v.activate();
    // 0.5 s excitation, then 6 s of silence.
    const size_t exc = 24000, sil = 288000;
    std::vector<float> inL(exc + sil, 0.0f), inR(exc + sil, 0.0f);
    auto nL = whiteNoise(exc, 7), nR = whiteNoise(exc, 8);
    for (size_t i = 0; i < exc; ++i) { inL[i] = nL[i]; inR[i] = nR[i]; }
    auto out = render(v, inL, inR, 128);
    const double tailDb = toDb(rms(out.l, out.l.size() - 24000, out.l.size()));
    std::cout << "       silence-tail residual (last 0.5s): " << tailDb << " dBFS\n";
    check(tailDb < -80.0, "output decays into silence (no self-oscillation/denormal buzz)");
}

void probeStateRoundtrip() {
    AestraVerb a;
    a.initialize(48000.0, 256);
    configureHall(a);
    a.setParameter(AestraVerb::kLowCut, 0.4f);
    a.setParameter(AestraVerb::kHighCut, 0.6f);
    a.setParameter(AestraVerb::kMode, AestraVerb::modeParam(AestraVerb::Mode::Plate));
    const auto blob = a.saveState();

    AestraVerb b;
    b.initialize(48000.0, 256);
    check(b.loadState(blob), "loadState accepts a saved blob");
    a.activate();
    b.activate();
    auto imp = impulseIn(48000);
    auto oa = render(a, imp.l, imp.r, 128);
    auto ob = render(b, imp.l, imp.r, 128);
    float maxDiff = 0.0f;
    for (size_t i = 0; i < oa.l.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(oa.l[i] - ob.l[i]));
        maxDiff = std::max(maxDiff, std::abs(oa.r[i] - ob.r[i]));
    }
    check(maxDiff == 0.0f, "save/load reproduces identical output (maxDiff=" + std::to_string(maxDiff) + ")");
}

// ---------------------------------------------------------------------------
// Diagnostics (printed; do not fail the build)
// ---------------------------------------------------------------------------

// Measure post-EQ attenuation of a steady tone vs a reference instance that
// has the given cut parameter at its neutral position.
double measureCutAtten(uint32_t cutParam, float neutral, float knob, float hz) {
    auto tone = [&](AestraVerb& verb) {
        std::vector<float> in(48000);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.5f * std::sin(2.0 * M_PI * hz * double(i) / 48000.0);
        auto out = render(verb, in, in, 128);
        return rms(out.l, 24000, 48000); // steady-state second half
    };
    AestraVerb ref;  ref.initialize(48000.0, 256); configureHall(ref);
    ref.setParameter(cutParam, neutral); ref.activate();
    AestraVerb cut;  cut.initialize(48000.0, 256); configureHall(cut);
    cut.setParameter(cutParam, knob); cut.activate();
    return toDb(tone(cut) / std::max(tone(ref), 1e-9));
}

void probeLowCutMapping() {
    std::cout << "\n--- Low Cut: displayed cutoff and measured attenuation ---\n";
    std::cout << "  knob   displayHz   100Hz-atten   500Hz-atten\n";
    double at100[5], at500[5];
    const float knobs[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (int k = 0; k < 5; ++k) {
        const float v = knobs[k];
        const double displayHz = v < 0.001f ? 20.0 : 20.0 * std::pow(100.0, v);
        at100[k] = measureCutAtten(AestraVerb::kLowCut, 0.0f, v, 100.0f);
        at500[k] = measureCutAtten(AestraVerb::kLowCut, 0.0f, v, 500.0f);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "  %.2f   %8.1f    %+7.1f dB    %+7.1f dB\n",
                      v, displayHz, at100[k], at500[k]);
        std::cout << buf;
    }
    // knob 0.5 = 200 Hz: one-pole HP is ~-7 dB at 100 Hz, ~-0.7 dB at 500 Hz.
    check(at100[2] < -4.0 && at100[2] > -10.0, "Low Cut @0.5 attenuates 100 Hz like a 200 Hz one-pole HP");
    check(at500[2] > -3.0, "Low Cut @0.5 leaves 500 Hz nearly untouched (clean HP shape)");
    // The top of the range must keep moving (no clamped dead zone).
    check(at100[4] < at100[3] - 3.0, "Low Cut keeps steepening from 0.75 to 1.0 (no dead zone)");
}

void probeHighCutDeadKnob() {
    std::cout << "\n--- High Cut: measured attenuation of a 6 kHz tone ---\n";
    std::cout << "  knob   displayHz   6kHz-atten\n";
    double at6k[3];
    const float knobs[3] = {1.0f, 0.75f, 0.5f};
    for (int k = 0; k < 3; ++k) {
        const float v = knobs[k];
        const double displayHz = v > 0.999f ? 20000.0 : 200.0 * std::pow(100.0, v);
        at6k[k] = measureCutAtten(AestraVerb::kHighCut, 1.0f, v, 6000.0f);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "  %.2f   %8.1f    %+7.1f dB\n", v, displayHz, at6k[k]);
        std::cout << buf;
    }
    // knob 0.5 = 2 kHz: a one-pole LP is ~-10 dB at 6 kHz. The old blend
    // heuristic made the knob a no-op above ~1.8 kHz (0 dB here).
    check(at6k[2] < -5.0, "High Cut @0.5 (2 kHz) actually attenuates 6 kHz (knob not dead)");
    check(at6k[1] < -1.0, "High Cut @0.75 (6.3 kHz) has audible effect at 6 kHz");
}

void probeFreezeStability() {
    std::cout << "\n--- Freeze stability: tail RMS over time (want ~flat) ---\n";
    // Realistic usage: excite with freeze OFF (input must reach the loop),
    // engage freeze right after the burst, then measure the held tail.
    AestraVerb v;
    v.initialize(48000.0, 256);
    configureHall(v);
    v.activate();
    const size_t exc = 12000, total = 48000 * 8;
    std::vector<float> inL(total, 0.0f), inR(total, 0.0f);
    auto nL = whiteNoise(exc, 11), nR = whiteNoise(exc, 12);
    for (size_t i = 0; i < exc; ++i) { inL[i] = nL[i]; inR[i] = nR[i]; }
    Stereo out{std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
    bool frozen = false;
    for (size_t off = 0; off < total; off += 128) {
        if (!frozen && off >= exc) { // engage on the first block after the burst
            v.setParameter(AestraVerb::kFreeze, 1.0f);
            frozen = true;
        }
        const float* ins[2] = {inL.data() + off, inR.data() + off};
        float* outs[2] = {out.l.data() + off, out.r.data() + off};
        v.process(ins, outs, 2, 2, 128);
    }
    const double r1 = rms(out.l, 48000 * 1, 48000 * 1 + 24000);
    const double r3 = rms(out.l, 48000 * 3, 48000 * 3 + 24000);
    const double r7 = rms(out.l, 48000 * 7, 48000 * 7 + 24000);
    char buf[160];
    const double drift = toDb(r7) - toDb(r1);
    std::snprintf(buf, sizeof(buf), "  t=1s: %.1f dB   t=3s: %.1f dB   t=7s: %.1f dB   (7s-1s drift: %+.1f dB)\n",
                  toDb(r1), toDb(r3), toDb(r7), drift);
    std::cout << buf;
    // A frozen tail must sustain, not fade: before the lossless-loop fix the
    // in-loop damping filters bled ~22 dB over this window.
    check(drift > -4.0 && drift < 4.0, "freeze sustains the tail (|6s drift| < 4 dB)");
}

double measureT60(double sr) {
    AestraVerb v;
    v.initialize(sr, 256);
    configureHall(v);
    v.setParameter(AestraVerb::kDecay, 0.7f);
    v.activate();
    const size_t n = size_t(sr * 6.0);
    std::vector<float> inL(n, 0.0f), inR(n, 0.0f);
    inL[0] = 1.0f; inR[0] = 1.0f;
    auto out = render(v, inL, inR, 128);
    // Peak windowed RMS then time to fall 60 dB below it.
    const size_t win = size_t(sr * 0.05);
    double peak = 0.0;
    for (size_t i = 0; i + win < n; i += win) peak = std::max(peak, rms(out.l, i, i + win));
    const double thresh = peak * std::pow(10.0, -60.0 / 20.0);
    double t60 = 0.0;
    for (size_t i = 0; i + win < n; i += win)
        if (rms(out.l, i, i + win) > thresh) t60 = double(i) / sr;
    return t60;
}

void probeSampleRateConsistency() {
    std::cout << "\n--- Sample-rate consistency: T60 at fixed params (want ~equal) ---\n";
    const double t441 = measureT60(44100.0);
    const double t48 = measureT60(48000.0);
    const double t96 = measureT60(96000.0);
    const double spread =
        100.0 * (std::max({t441, t48, t96}) - std::min({t441, t48, t96})) / std::max(t48, 1e-6);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "  44.1k: %.3fs   48k: %.3fs   96k: %.3fs   (spread: %.1f%%)\n",
                  t441, t48, t96, spread);
    std::cout << buf;
    // Before SR-normalizing the damping pole the 96k T60 drifted ~25% long.
    check(spread < 10.0, "T60 is sample-rate independent (spread < 10%)");
}

void diagModeSwitchClick() {
    std::cout << "\n--- Mode-switch discontinuity (max |sample delta| around switch) ---\n";
    AestraVerb v;
    v.initialize(48000.0, 64);
    configureHall(v);
    v.activate();
    // Build a steady tail, then switch mode mid-render and watch the seam.
    auto noise = whiteNoise(48000, 21);
    auto baselineMaxDelta = [](const std::vector<float>& x, size_t from, size_t to) {
        float m = 0.0f;
        for (size_t i = from + 1; i < to && i < x.size(); ++i) m = std::max(m, std::abs(x[i] - x[i - 1]));
        return m;
    };
    std::vector<float> outL(96000, 0.0f), outR(96000, 0.0f);
    std::vector<float> inL(96000, 0.0f), inR(96000, 0.0f);
    for (size_t i = 0; i < 48000; ++i) { inL[i] = noise[i] * 0.3f; inR[i] = noise[i] * 0.3f; }
    size_t switchAt = 60000;
    for (size_t off = 0; off < 96000; off += 64) {
        if (off == switchAt) v.setParameter(AestraVerb::kMode, AestraVerb::modeParam(AestraVerb::Mode::Plate));
        const uint32_t frames = 64;
        const float* ins[2] = {inL.data() + off, inR.data() + off};
        float* outs[2] = {outL.data() + off, outR.data() + off};
        v.process(ins, outs, 2, 2, frames);
    }
    const float preDelta = baselineMaxDelta(outL, switchAt - 4096, switchAt - 64);
    const float atDelta = baselineMaxDelta(outL, switchAt - 64, switchAt + 512);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "  pre-switch max delta: %.5f   at-switch max delta: %.5f   (ratio: %.2fx)\n",
                  preDelta, atDelta, atDelta / std::max(preDelta, 1e-9f));
    std::cout << buf;
}

double pearson(const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to) {
    double ma = 0, mb = 0; size_t n = 0;
    for (size_t i = from; i < to && i < a.size(); ++i) { ma += a[i]; mb += b[i]; ++n; }
    if (!n) return 0; ma /= n; mb /= n;
    double num = 0, da = 0, db = 0;
    for (size_t i = from; i < to && i < a.size(); ++i) {
        num += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) * (a[i] - ma); db += (b[i] - mb) * (b[i] - mb);
    }
    return num / std::sqrt(std::max(da * db, 1e-18));
}

void diagStereoCorrelationAndBalance() {
    std::cout << "\n--- Early stereo correlation + mono-in L/R balance ---\n";
    AestraVerb v;
    v.initialize(48000.0, 256);
    configureHall(v);
    v.activate();
    auto imp = impulseIn(96000);
    auto out = render(v, imp.l, imp.r, 128);
    const double earlyCorr = pearson(out.l, out.r, 0, 960);   // first 20 ms
    const double lateCorr = pearson(out.l, out.r, 24000, 48000);
    const double balL = toDb(rms(out.l, 0, out.l.size()));
    const double balR = toDb(rms(out.r, 0, out.r.size()));
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "  early (0-20ms) L/R corr: %.3f   late (0.5-1s) corr: %.3f   mono-in L/R RMS: %+.2f/%+.2f dB (imbalance %.2f dB)\n",
                  earlyCorr, lateCorr, balL, balR, balL - balR);
    std::cout << buf;
    std::cout << "  (early corr near 1.0 = mono onset; nonzero L/R imbalance = asymmetric injection/output)\n";
}

} // namespace

int main() {
    std::cout << "AestraVerb Consistency Probe\n============================\n";
    probeBypassParity();
    probeNaNRejection();
    probeSilenceTail();
    probeStateRoundtrip();

    probeLowCutMapping();
    probeHighCutDeadKnob();
    probeFreezeStability();
    probeSampleRateConsistency();
    diagModeSwitchClick();
    diagStereoCorrelationAndBalance();

    std::cout << "\n" << (g_failures ? std::to_string(g_failures) + " hard invariant(s) FAILED\n"
                                     : "All hard invariants held\n");
    return g_failures ? 1 : 0;
}
