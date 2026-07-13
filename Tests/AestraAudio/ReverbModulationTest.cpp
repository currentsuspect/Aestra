// AestraVerb F4 modulation regression test.
//
// Locks in the modulation-rewrite invariants by tapping the real per-line
// delay-read offsets (AESTRA_REVERB_MOD_TRACE) and asserting the F4 acceptance
// criteria directly on the modulator signal — not on the summed output:
//
//   1. Modulation frequency is equivalent in Hz across sample rates.
//   2. Mod Rate is monotonic and materially controls the oscillator.
//   3. No sr/4 or ultrasonic modulation component (energy stays sub-audio).
//   4. Lines are not identically phased (no full per-line correlation).
//   5. Delay excursion stays bounded well within the delay-line safety margin.
//
// Covers all three modulation characters (Random / Chorus / Chaotic).

#define AESTRA_REVERB_MOD_TRACE 1
#include "AestraVerb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraVerb;

namespace {

constexpr size_t kLines = 8; // AestraVerb::kFDNLineCount
int g_failures = 0;

void check(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    }
}

struct Tap {
    std::array<std::vector<float>, kLines> line;
    size_t frames = 0;
};

// Render `seconds` of silence and capture the per-line modulation offset.
Tap tapModulation(float sr, float rateParam, float depthParam, int character, float seconds) {
    const size_t frames = static_cast<size_t>(sr * seconds);
    AestraVerb verb;
    verb.initialize(sr, 512);
    verb.setParameter(AestraVerb::kMode, AestraVerb::modeParam(AestraVerb::Mode::Hall));
    verb.setParameter(AestraVerb::kModRate, rateParam);
    verb.setParameter(AestraVerb::kModDepth, depthParam);
    verb.setParameter(AestraVerb::kModCharacter, character * 0.5f);
    verb.setParameter(AestraVerb::kMix, 1.0f);
    verb.activate();

    std::vector<float> trace(frames * kLines, 0.0f);
    verb.beginModTrace(trace.data(), trace.size());

    std::vector<float> silence(frames, 0.0f), outL(frames), outR(frames);
    const float* in[2] = { silence.data(), silence.data() };
    float* out[2] = { outL.data(), outR.data() };
    verb.process(in, out, 2, 2, static_cast<uint32_t>(frames));

    // The trace hook must actually be live; otherwise a non-trace COMDAT was
    // linked from the engine library and this test would silently pass on zeros.
    if (verb.modTraceCount() != trace.size()) {
        std::cerr << "[FATAL] mod-trace hook inactive (captured " << verb.modTraceCount()
                  << " of " << trace.size() << ")\n";
        std::exit(3);
    }

    Tap t;
    t.frames = frames;
    for (size_t l = 0; l < kLines; ++l) t.line[l].resize(frames);
    for (size_t f = 0; f < frames; ++f)
        for (size_t l = 0; l < kLines; ++l)
            t.line[l][f] = trace[f * kLines + l];
    return t;
}

// Mean zero-crossing rate (Hz) of the per-line offsets: a robust oscillation
// speed estimate for sub-Hz signals below FFT resolution.
float meanZeroCrossHz(const Tap& t, float sr) {
    if (t.frames < 2) return 0.0f;
    const float dur = static_cast<float>(t.frames) / sr;
    double sum = 0.0;
    for (size_t l = 0; l < kLines; ++l) {
        double acc = 0.0;
        for (float v : t.line[l]) acc += v;
        const float mean = static_cast<float>(acc / t.frames);
        int cross = 0;
        float prev = t.line[l][0] - mean;
        for (size_t f = 1; f < t.frames; ++f) {
            const float cur = t.line[l][f] - mean;
            if ((prev < 0.0f && cur >= 0.0f) || (prev > 0.0f && cur <= 0.0f)) ++cross;
            prev = cur;
        }
        sum += static_cast<double>(cross) / (2.0 * dur);
    }
    return static_cast<float>(sum / kLines);
}

// Largest single-sample offset step across all lines — a discontinuity / audio-
// rate indicator. A sub-Hz LFO at a few samples of excursion moves << 0.01
// samples per step; an audio-rate carrier moves a large fraction per step.
float maxStep(const Tap& t) {
    float m = 0.0f;
    for (size_t l = 0; l < kLines; ++l)
        for (size_t f = 1; f < t.frames; ++f)
            m = std::max(m, std::abs(t.line[l][f] - t.line[l][f - 1]));
    return m;
}

float peakExcursion(const Tap& t) {
    float m = 0.0f;
    for (size_t l = 0; l < kLines; ++l)
        for (float v : t.line[l]) m = std::max(m, std::abs(v));
    return m;
}

float pearson(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = std::min(a.size(), b.size());
    if (n < 2) return 0.0f;
    double sa = 0, sb = 0;
    for (size_t i = 0; i < n; ++i) { sa += a[i]; sb += b[i]; }
    const double ma = sa / n, mb = sb / n;
    double cov = 0, va = 0, vb = 0;
    for (size_t i = 0; i < n; ++i) {
        const double da = a[i] - ma, db = b[i] - mb;
        cov += da * db; va += da * da; vb += db * db;
    }
    const double d = std::sqrt(va * vb);
    return d > 1e-20 ? static_cast<float>(cov / d) : 0.0f;
}

float maxOffDiagCorr(const Tap& t) {
    float m = 0.0f;
    for (size_t a = 0; a < kLines; ++a)
        for (size_t b = a + 1; b < kLines; ++b)
            m = std::max(m, std::abs(pearson(t.line[a], t.line[b])));
    return m;
}

const char* charName(int c) { return c == 0 ? "Random" : (c == 1 ? "Chorus" : "Chaotic"); }

void testCharacter(int character) {
    const std::string cn = charName(character);
    const float seconds = 4.0f;

    // --- 1. Sample-rate equivalence: zero-crossing rate must match in Hz ---
    const float zc48 = meanZeroCrossHz(tapModulation(48000.0f, 1.0f, 1.0f, character, seconds), 48000.0f);
    const float zc96 = meanZeroCrossHz(tapModulation(96000.0f, 1.0f, 1.0f, character, seconds), 96000.0f);
    const float zc192 = meanZeroCrossHz(tapModulation(192000.0f, 1.0f, 1.0f, character, seconds), 192000.0f);
    check(zc48 > 0.05f, cn + ": modulation active at rate 1.0 (zc48=" + std::to_string(zc48) + ")");
    // Allow 15% tolerance for windowing/quantization; the broken chaotic scaled
    // 2x per octave (100% per SR doubling), so this cleanly rejects it.
    check(std::abs(zc96 - zc48) <= 0.15f * zc48 + 0.05f,
          cn + ": zero-cross Hz equivalent 48k->96k (" + std::to_string(zc48) + " vs " + std::to_string(zc96) + ")");
    check(std::abs(zc192 - zc48) <= 0.15f * zc48 + 0.05f,
          cn + ": zero-cross Hz equivalent 48k->192k (" + std::to_string(zc48) + " vs " + std::to_string(zc192) + ")");

    // --- 2. Mod Rate monotonic and material (mid < max, by a real margin) ---
    const float zcMid = meanZeroCrossHz(tapModulation(48000.0f, 0.5f, 1.0f, character, seconds), 48000.0f);
    const float zcMax = meanZeroCrossHz(tapModulation(48000.0f, 1.0f, 1.0f, character, seconds), 48000.0f);
    check(zcMax > zcMid * 1.2f,
          cn + ": Mod Rate materially speeds oscillation (mid=" + std::to_string(zcMid) +
              " max=" + std::to_string(zcMax) + ")");

    // --- 3. No audio-rate / ultrasonic component: per-sample step stays tiny ---
    // A sub-2 Hz LFO at <10 samples excursion steps far below 0.05 smp/sample.
    // The broken chaotic (238 Hz at 48k) stepped ~0.18 smp/sample.
    const Tap tMax = tapModulation(48000.0f, 1.0f, 1.0f, character, seconds);
    const float step = maxStep(tMax);
    check(step < 0.05f, cn + ": no audio-rate content (maxStep=" + std::to_string(step) + " smp/sample)");

    // --- 4. Lines not identically phased ---
    const float corr = maxOffDiagCorr(tMax);
    check(corr < 0.98f, cn + ": lines decorrelated (maxCorr=" + std::to_string(corr) + ")");

    // --- 5. Excursion bounded well inside the delay-line safety margin (28) ---
    const float exc = peakExcursion(tMax);
    check(exc < 12.0f, cn + ": excursion bounded (" + std::to_string(exc) + " smp < 12, margin 28)");

    std::cout << "  " << cn << ": zc48=" << zc48 << " zc96=" << zc96 << " zc192=" << zc192
              << " | rateMid=" << zcMid << " rateMax=" << zcMax
              << " | maxStep=" << step << " corr=" << corr << " exc=" << exc << "\n";
}

// Modulation must be fully disabled at Depth 0 or Rate 0 (no offset at all).
void testDisableConditions() {
    for (int c = 0; c < 3; ++c) {
        const Tap depthZero = tapModulation(48000.0f, 1.0f, 0.0f, c, 0.5f);
        check(peakExcursion(depthZero) == 0.0f,
              std::string(charName(c)) + ": Depth 0 => no modulation");
        const Tap rateZero = tapModulation(48000.0f, 0.0f, 1.0f, c, 0.5f);
        check(peakExcursion(rateZero) == 0.0f,
              std::string(charName(c)) + ": Rate 0 => no modulation");
    }
}

// Determinism: identical configuration must produce a bit-identical trace.
void testDeterminism() {
    const Tap a = tapModulation(48000.0f, 0.7f, 0.8f, 2, 1.0f);
    const Tap b = tapModulation(48000.0f, 0.7f, 0.8f, 2, 1.0f);
    bool identical = a.frames == b.frames;
    for (size_t l = 0; l < kLines && identical; ++l)
        for (size_t f = 0; f < a.frames; ++f)
            if (a.line[l][f] != b.line[l][f]) { identical = false; break; }
    check(identical, "Chaotic modulation is deterministic across identical runs");
}

} // namespace

int main() {
    std::cout << "AestraVerb F4 Modulation Regression Test\n";
    std::cout << "========================================\n";
    for (int c = 0; c < 3; ++c) testCharacter(c);
    testDisableConditions();
    testDeterminism();

    if (g_failures != 0) {
        std::cerr << "\n" << g_failures << " modulation check(s) FAILED.\n";
        return 1;
    }
    std::cout << "\nAll F4 modulation checks passed.\n";
    return 0;
}
