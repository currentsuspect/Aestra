// AestraVerb Modulation Characterization Lab (F4)
//
// Characterizes the REAL modulator by tapping the exact per-line delay-read
// offsets the engine applies (via AESTRA_REVERB_MOD_TRACE), not by inferring
// motion from the summed output. Sweeps sample rate x Mod Rate x Mod Depth x
// modulation character, plus a per-mode excursion pass.
//
// Measures, per config: actual modulation frequency (Hz), sample-rate
// consistency, whether Mod Rate is monotonic and material, per-line phase
// correlation, modulation bandwidth / HF energy, delay excursion (samples & ms),
// and sample-to-sample discontinuities.
//
// Outputs:
//   - labs/reverb/modulation/modulation_baseline.md
//   - labs/reverb/modulation/modulation_baseline.json

// The trace hook is compiled directly into this TU's header instantiation.
#define AESTRA_REVERB_MOD_TRACE 1
#include "AestraVerb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraVerb;

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr size_t kLines = 8; // AestraVerb::kFDNLineCount

// Iterative radix-2 FFT (in place).
void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    if (n <= 1) return;
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * kPi / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

size_t largestPow2LEQ(size_t n) {
    size_t p = 1;
    while ((p << 1) <= n) p <<= 1;
    return p;
}

// Nearest power of two to n (capped so it never exceeds `cap`). Using nearest
// rather than floor keeps the FFT window duration (and thus Hz resolution)
// consistent across sample rates whose sr*T lands just below a power of two.
size_t nearestPow2(size_t n, size_t cap) {
    size_t lo = largestPow2LEQ(std::max<size_t>(n, 1));
    size_t hi = lo << 1;
    size_t pick = (n - lo <= hi - n) ? lo : hi;
    while (pick > cap) pick >>= 1;
    return std::max<size_t>(pick, 1);
}

// Spectral summary of a per-line offset signal. The FFT window is chosen for a
// fixed ~1.4 s DURATION (not a fixed sample count) so the Hz resolution is the
// same at every sample rate — otherwise SR-consistency comparisons are corrupted
// by resolution changes. Reports: peakHz (dominant modulation frequency),
// hfFraction (energy >= 20 Hz / total), and hfPeakHz (dominant frequency above
// 20 Hz, 0 if none — surfaces any audio-rate / ultrasonic carrier).
void spectral(const std::vector<float>& x, float sr, float& peakHz, float& hfFraction,
              float& hfPeakHz, float lowCutHz = 0.30f) {
    peakHz = 0.0f;
    hfFraction = 0.0f;
    hfPeakHz = 0.0f;
    const size_t target = static_cast<size_t>(sr * 1.4f); // fixed duration
    size_t win = nearestPow2(target, x.size());
    if (win < 64) return;
    std::vector<std::complex<float>> buf(win);
    for (size_t i = 0; i < win; ++i) {
        const float w = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(win - 1)));
        buf[i] = std::complex<float>(x[i] * w, 0.0f);
    }
    fft(buf);
    const float binHz = sr / static_cast<float>(win);
    double total = 0.0, high = 0.0, peakMag = 0.0, hfPeakMag = 0.0;
    for (size_t b = 1; b <= win / 2; ++b) {
        const float hz = static_cast<float>(b) * binHz;
        const double e = std::norm(buf[b]);
        if (hz < lowCutHz) continue; // ignore DC / ultra-slow drift
        total += e;
        if (e > peakMag) { peakMag = e; peakHz = hz; }
        if (hz >= 20.0f) {
            high += e;
            if (e > hfPeakMag) { hfPeakMag = e; hfPeakHz = hz; }
        }
    }
    hfFraction = total > 1e-20 ? static_cast<float>(high / total) : 0.0f;
    // Only report an above-20 Hz peak when there is meaningful HF energy;
    // otherwise the "peak" is just the loudest noise-floor bin (which lands on
    // sr/4 for near-DC signals) and would be misleading.
    if (hfFraction < 0.005f) hfPeakHz = 0.0f;
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
    const double denom = std::sqrt(va * vb);
    return denom > 1e-20 ? static_cast<float>(cov / denom) : 0.0f;
}

struct ModMetrics {
    bool enabled = false;
    std::array<float, kLines> lineFreqHz{};
    float dominantFreqHz = 0.0f;   // peak across lines
    float zcFreqHz = 0.0f;         // mean zero-crossing rate over full render (robust for slow LFOs)
    float hfFraction = 0.0f;       // mean over lines
    float hfPeakHz = 0.0f;         // dominant frequency above 20 Hz (max over lines)
    float excursionPeakSamples = 0.0f;
    float excursionP2PSamples = 0.0f;
    float excursionPeakMs = 0.0f;
    float meanOffDiagCorr = 0.0f;
    float maxOffDiagCorr = 0.0f;
    float maxStepSamples = 0.0f;   // largest |offset[i]-offset[i-1]|
};

// character: 0=Random, 1=Chorus, 2=Chaotic (maps to kModCharacter param 0/0.5/1)
ModMetrics measure(float sr, float rateParam, float depthParam, int character,
                   AestraVerb::Mode mode, float analysisSeconds) {
    ModMetrics m;

    size_t frames = static_cast<size_t>(sr * analysisSeconds);
    frames = std::min<size_t>(frames, 800000);
    frames = std::max<size_t>(frames, 8192);

    AestraVerb verb;
    verb.initialize(sr, 512);
    verb.setParameter(AestraVerb::kMode, AestraVerb::modeParam(mode));
    verb.setParameter(AestraVerb::kModRate, rateParam);
    verb.setParameter(AestraVerb::kModDepth, depthParam);
    verb.setParameter(AestraVerb::kModCharacter, character * 0.5f);
    verb.setParameter(AestraVerb::kFreeze, 0.0f);
    verb.setParameter(AestraVerb::kMix, 1.0f);
    verb.activate();

    std::vector<float> trace(frames * kLines, 0.0f);
    verb.beginModTrace(trace.data(), trace.size());

    // Modulation is input-independent; feed silence so the tapped offset is the
    // pure modulator signal with no input contamination.
    std::vector<float> silence(frames, 0.0f), outL(frames), outR(frames);
    const float* in[2] = { silence.data(), silence.data() };
    float* out[2] = { outL.data(), outR.data() };
    verb.process(in, out, 2, 2, static_cast<uint32_t>(frames));

    // Self-check: the trace code must actually have run (guards against a
    // non-trace COMDAT being linked from the engine lib).
    if (verb.modTraceCount() != trace.size()) {
        std::cerr << "FATAL: mod-trace captured " << verb.modTraceCount() << " of "
                  << trace.size() << " expected — trace hook not active in this build\n";
        std::exit(3);
    }

    // Deinterleave into per-line offset signals.
    std::array<std::vector<float>, kLines> line;
    for (size_t l = 0; l < kLines; ++l) line[l].resize(frames);
    float globalPeakAbs = 0.0f, globalMin = 0.0f, globalMax = 0.0f;
    bool anyNonZero = false;
    for (size_t f = 0; f < frames; ++f) {
        for (size_t l = 0; l < kLines; ++l) {
            const float v = trace[f * kLines + l];
            line[l][f] = v;
            if (v != 0.0f) anyNonZero = true;
            globalPeakAbs = std::max(globalPeakAbs, std::abs(v));
            globalMin = std::min(globalMin, v);
            globalMax = std::max(globalMax, v);
        }
    }
    m.enabled = anyNonZero;
    m.excursionPeakSamples = globalPeakAbs;
    m.excursionP2PSamples = globalMax - globalMin;
    m.excursionPeakMs = globalPeakAbs / sr * 1000.0f;

    // Per-line spectral + discontinuity + zero-crossing rate.
    const float durationSec = static_cast<float>(frames) / sr;
    double hfSum = 0.0, zcSum = 0.0;
    for (size_t l = 0; l < kLines; ++l) {
        float peakHz = 0.0f, hf = 0.0f, hfPeak = 0.0f;
        spectral(line[l], sr, peakHz, hf, hfPeak);
        m.lineFreqHz[l] = peakHz;
        hfSum += hf;
        m.dominantFreqHz = std::max(m.dominantFreqHz, peakHz);
        m.hfPeakHz = std::max(m.hfPeakHz, hfPeak);

        // Mean-subtracted zero crossings → mean oscillation frequency. This
        // resolves the sub-Hz LFOs (below FFT resolution) and cleanly shows
        // whether Mod Rate speeds the oscillator up.
        double sum = 0.0;
        for (size_t f = 0; f < frames; ++f) sum += line[l][f];
        const float mean = static_cast<float>(sum / std::max<size_t>(frames, 1));
        int crossings = 0;
        float prev = line[l][0] - mean;
        for (size_t f = 1; f < frames; ++f) {
            const float cur = line[l][f] - mean;
            if ((prev < 0.0f && cur >= 0.0f) || (prev > 0.0f && cur <= 0.0f)) ++crossings;
            prev = cur;
            m.maxStepSamples = std::max(m.maxStepSamples, std::abs(line[l][f] - line[l][f - 1]));
        }
        zcSum += (durationSec > 0.0f) ? (static_cast<double>(crossings) / (2.0 * durationSec)) : 0.0;
    }
    m.hfFraction = static_cast<float>(hfSum / kLines);
    m.zcFreqHz = static_cast<float>(zcSum / kLines);

    // Per-line correlation (off-diagonal of the 8x8 Pearson matrix).
    double corrSum = 0.0;
    int corrCount = 0;
    for (size_t a = 0; a < kLines; ++a) {
        for (size_t b = a + 1; b < kLines; ++b) {
            const float c = std::abs(pearson(line[a], line[b]));
            corrSum += c;
            m.maxOffDiagCorr = std::max(m.maxOffDiagCorr, c);
            ++corrCount;
        }
    }
    m.meanOffDiagCorr = corrCount > 0 ? static_cast<float>(corrSum / corrCount) : 0.0f;

    return m;
}

const char* charName(int c) { return c == 0 ? "Random" : (c == 1 ? "Chorus" : "Chaotic"); }

} // namespace

int main() {
    const std::string outDir = "labs/reverb/modulation";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    const std::array<float, 4> sampleRates = { 44100.0f, 48000.0f, 96000.0f, 192000.0f };
    const std::array<float, 3> rateParams = { 0.0f, 0.5f, 1.0f };   // min / mid / max
    const std::array<float, 3> depthParams = { 0.0f, 0.5f, 1.0f };  // min / mid / max
    const std::array<int, 3> characters = { 0, 1, 2 };              // Random / Chorus / Chaotic
    const float analysisSeconds = 4.0f;

    std::cout << "AestraVerb Modulation Characterization Lab (F4)\n";
    std::cout << "===================================================\n\n";

    std::ostringstream md, js;
    md << "# AestraVerb Modulation Characterization (F4)\n\n";
    md << "Regenerated by AestraReverbModulationLab. For the pre-rewrite state of "
          "these numbers, see git history (the F4 baseline commit).\n\n";
    md << "Measured by tapping the real per-line delay-read offsets. Silence input; "
          "the modulator is input-independent. Analysis window " << analysisSeconds << " s.\n\n";
    js << "{\n  \"analysisSeconds\": " << analysisSeconds << ",\n  \"configs\": [\n";
    bool firstJs = true;

    // ---- Sweep 1: SR x Rate x Depth x Character (mode fixed = Hall) ----
    md << "## Sweep: sample rate x Mod Rate x Mod Depth x character (mode = Hall)\n\n";
    md << "Columns: **ZC-Hz** = mean zero-crossing rate of the per-line offset over the full "
          "render (robust oscillation-speed estimate for the sub-Hz LFOs); **HFpeakHz** = "
          "dominant modulation frequency above 20 Hz, 0 if none (surfaces any audio-rate / "
          "ultrasonic carrier); **HF%** = fraction of modulation energy above 20 Hz (wow/flutter "
          "should be ~0); **MeanCorr/MaxCorr** = per-line phase correlation (1.0 = all lines move "
          "identically); **Exc** = peak delay excursion; **MaxStep** = largest per-sample offset "
          "jump (a discontinuity / audio-rate indicator).\n\n";
    md << "| SR | Rate | Depth | Char | Enabled | ZC-Hz | HFpeakHz | HF% | MeanCorr | MaxCorr | ExcSmp | ExcMs | MaxStep |\n";
    md << "|----|------|-------|------|---------|-------|----------|-----|----------|---------|--------|-------|---------|\n";

    for (int c : characters) {
        for (float rate : rateParams) {
            for (float depth : depthParams) {
                for (float sr : sampleRates) {
                    const ModMetrics m = measure(sr, rate, depth, c, AestraVerb::Mode::Hall, analysisSeconds);
                    md << "| " << static_cast<int>(sr)
                       << " | " << std::fixed << std::setprecision(1) << rate
                       << " | " << depth
                       << " | " << charName(c)
                       << " | " << (m.enabled ? "yes" : "NO")
                       << " | " << std::setprecision(3) << m.zcFreqHz
                       << " | " << std::setprecision(1) << m.hfPeakHz
                       << " | " << std::setprecision(1) << (m.hfFraction * 100.0f)
                       << " | " << std::setprecision(3) << m.meanOffDiagCorr
                       << " | " << m.maxOffDiagCorr
                       << " | " << std::setprecision(3) << m.excursionPeakSamples
                       << " | " << m.excursionPeakMs
                       << " | " << std::setprecision(4) << m.maxStepSamples
                       << " |\n";

                    js << (firstJs ? "    " : ",\n    ") << "{";
                    firstJs = false;
                    js << "\"sr\": " << static_cast<int>(sr)
                       << ", \"rateParam\": " << rate
                       << ", \"depthParam\": " << depth
                       << ", \"character\": \"" << charName(c) << "\""
                       << ", \"enabled\": " << (m.enabled ? "true" : "false")
                       << ", \"dominantHz\": " << m.dominantFreqHz
                       << ", \"zcFreqHz\": " << m.zcFreqHz
                       << ", \"hfPeakHz\": " << m.hfPeakHz
                       << ", \"hfFraction\": " << m.hfFraction
                       << ", \"meanOffDiagCorr\": " << m.meanOffDiagCorr
                       << ", \"maxOffDiagCorr\": " << m.maxOffDiagCorr
                       << ", \"excursionPeakSamples\": " << m.excursionPeakSamples
                       << ", \"excursionPeakMs\": " << m.excursionPeakMs
                       << ", \"maxStepSamples\": " << m.maxStepSamples
                       << ", \"lineFreqHz\": [";
                    for (size_t l = 0; l < kLines; ++l) js << (l ? ", " : "") << m.lineFreqHz[l];
                    js << "]}";

                    std::cout << static_cast<int>(sr) << "Hz rate=" << rate << " depth=" << depth
                              << " " << charName(c) << ": " << (m.enabled ? "" : "[disabled] ")
                              << "zcHz=" << std::fixed << std::setprecision(3) << m.zcFreqHz
                              << " hfPeak=" << std::setprecision(1) << m.hfPeakHz << "Hz"
                              << " HF=" << std::setprecision(1) << (m.hfFraction * 100.0f) << "%"
                              << " corr=" << std::setprecision(3) << m.meanOffDiagCorr
                              << " exc=" << m.excursionPeakSamples << "smp/" << m.excursionPeakMs << "ms\n";
                }
            }
        }
    }

    // ---- Sweep 2: per-mode excursion (fixed sr=48k, rate=max, depth=max, Random) ----
    md << "\n## Per-mode excursion (48 kHz, Rate=max, Depth=max, Random)\n\n";
    md << "Modes share one modulation topology; only the per-mode depth scalar differs, "
          "so excursion is the meaningful per-mode variable.\n\n";
    md << "| Mode | ExcSmp | ExcMs | DominantHz |\n|------|--------|-------|-----------|\n";
    const std::array<AestraVerb::Mode, 9> modes = {
        AestraVerb::Mode::Room, AestraVerb::Mode::Hall, AestraVerb::Mode::Plate,
        AestraVerb::Mode::Cathedral, AestraVerb::Mode::Chamber, AestraVerb::Mode::BrightHall,
        AestraVerb::Mode::Ambience, AestraVerb::Mode::Scoring, AestraVerb::Mode::SmoothPlate
    };
    const char* modeNames[9] = { "room", "hall", "plate", "cathedral", "chamber",
                                 "bright_hall", "ambience", "scoring", "smooth_plate" };
    js << "\n  ],\n  \"perMode\": [\n";
    for (size_t i = 0; i < modes.size(); ++i) {
        const ModMetrics m = measure(48000.0f, 1.0f, 1.0f, 0, modes[i], analysisSeconds);
        md << "| " << modeNames[i]
           << " | " << std::fixed << std::setprecision(3) << m.excursionPeakSamples
           << " | " << m.excursionPeakMs
           << " | " << std::setprecision(2) << m.dominantFreqHz << " |\n";
        js << (i ? ",\n    " : "    ") << "{\"mode\": \"" << modeNames[i]
           << "\", \"excursionPeakSamples\": " << m.excursionPeakSamples
           << ", \"excursionPeakMs\": " << m.excursionPeakMs
           << ", \"dominantHz\": " << m.dominantFreqHz << "}";
        std::cout << "mode " << modeNames[i] << ": exc=" << std::fixed << std::setprecision(3)
                  << m.excursionPeakSamples << "smp domHz=" << std::setprecision(2) << m.dominantFreqHz << "\n";
    }
    js << "\n  ]\n}\n";

    { std::ofstream f(outDir + "/modulation_baseline.md"); if (f) f << md.str(); }
    { std::ofstream f(outDir + "/modulation_baseline.json"); if (f) f << js.str(); }

    std::cout << "\nModulation baseline written to " << outDir << "/\n";
    return 0;
}
