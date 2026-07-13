// AestraVerb Stereo Characterization Lab (F5/F6 re-measurement)
//
// Re-measures AestraVerb's stereo image against the CURRENT engine (post SIMD
// ring-fix and post F4 modulation rewrite), because the review's F5/F6 findings
// ("early correlation 1.000", "Room goes strongly negatively correlated on
// dense material") were measured on mislabeled modes and the old modulator.
//
// Measures, per mode:
//   - Early (onset) inter-channel correlation on an impulse   [F5]
//   - Late-tail inter-channel correlation on dense noise       [F6]
//   - Width proxy (side/mid RMS)
//   - Mono fold-down retention (mono RMS / L RMS; <1 => cancellation)
//   - Inter-channel time/level structure of the early reflections
//
// Also reports the STATIC injection/output/decorrelation geometry the image is
// built from, so the code-level cause of any measured weakness is visible.
//
// Output: labs/reverb/stereo/stereo_characterization.md / .json

#include "AestraVerb.h"

#include <algorithm>
#include <array>
#include <cmath>
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

double pearson(const std::vector<float>& a, const std::vector<float>& b, size_t start, size_t end) {
    end = std::min(end, std::min(a.size(), b.size()));
    if (end <= start + 1) return 0.0;
    const size_t n = end - start;
    double sa = 0, sb = 0;
    for (size_t i = start; i < end; ++i) { sa += a[i]; sb += b[i]; }
    const double ma = sa / n, mb = sb / n;
    double cov = 0, va = 0, vb = 0;
    for (size_t i = start; i < end; ++i) {
        const double da = a[i] - ma, db = b[i] - mb;
        cov += da * db; va += da * da; vb += db * db;
    }
    const double d = std::sqrt(va * vb);
    return d > 1e-20 ? cov / d : 0.0;
}

double rms(const std::vector<float>& x, size_t start, size_t end) {
    end = std::min(end, x.size());
    if (end <= start) return 0.0;
    double s = 0;
    for (size_t i = start; i < end; ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / (end - start));
}

struct StereoResult {
    std::string mode;
    float earlyCorr = 0.0f;      // impulse, onset window
    float lateCorr = 0.0f;       // dense noise, tail window
    float widthProxy = 0.0f;     // side RMS / mid RMS on dense tail
    float monoRetentionDb = 0.0f;// 20log10(monoRMS / L RMS) on dense tail; 0=no loss, <0=cancellation
    float onsetLagSamples = 0.0f;// best cross-correlation lag L->R in the early window
    float earlyLevelDiffDb = 0.0f;// L vs R early-window RMS difference
};

// Render an impulse and a dense noise burst through one mode; measure the image.
StereoResult measureMode(const std::string& name, AestraVerb::Mode mode, float sr) {
    StereoResult r;
    r.mode = name;
    const size_t frames = static_cast<size_t>(sr * 2.5f);

    auto render = [&](const std::vector<float>& inL, const std::vector<float>& inR,
                      std::vector<float>& outL, std::vector<float>& outR) {
        AestraVerb verb;
        verb.initialize(sr, 512);
        verb.setParameter(AestraVerb::kMode, AestraVerb::modeParam(mode));
        verb.setParameter(AestraVerb::kDecay, 0.7f);
        verb.setParameter(AestraVerb::kSize, 0.6f);
        verb.setParameter(AestraVerb::kDiffusion, 0.8f);
        verb.setParameter(AestraVerb::kModRate, 0.5f);
        verb.setParameter(AestraVerb::kModDepth, 0.3f);
        verb.setParameter(AestraVerb::kWidth, 0.7f);
        verb.setParameter(AestraVerb::kMix, 1.0f);
        verb.activate();
        outL.assign(frames, 0.0f);
        outR.assign(frames, 0.0f);
        const float* in[2] = { inL.data(), inR.data() };
        float* out[2] = { outL.data(), outR.data() };
        verb.process(in, out, 2, 2, static_cast<uint32_t>(frames));
    };

    // --- Impulse: early-onset image (F5) ---
    std::vector<float> impL(frames, 0.0f), impR(frames, 0.0f), eL, eR;
    impL[0] = 1.0f; impR[0] = 1.0f;
    render(impL, impR, eL, eR);

    // Find onset (first sample above -60 dB of the early peak) and analyze the
    // 20 ms window after it — the perceptually critical early-reflection cloud.
    const size_t win20 = static_cast<size_t>(sr * 0.02f);
    float earlyPeak = 0.0f;
    const size_t scanEnd = std::min(frames, static_cast<size_t>(sr * 0.15f));
    for (size_t i = 1; i < scanEnd; ++i) earlyPeak = std::max(earlyPeak, std::abs(eL[i]) + std::abs(eR[i]));
    size_t onset = 1;
    const float onsetThresh = earlyPeak * 0.001f;
    for (size_t i = 1; i < scanEnd; ++i) {
        if (std::abs(eL[i]) + std::abs(eR[i]) > onsetThresh) { onset = i; break; }
    }
    const size_t earlyEnd = std::min(frames, onset + win20);
    r.earlyCorr = static_cast<float>(pearson(eL, eR, onset, earlyEnd));

    // Onset lag: cross-correlation argmax of L vs R over +/- 2 ms in the window.
    const int maxLag = static_cast<int>(sr * 0.002f);
    double bestC = -1e18; int bestLag = 0;
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        double c = 0;
        for (size_t i = onset; i < earlyEnd; ++i) {
            const long j = static_cast<long>(i) + lag;
            if (j >= 0 && static_cast<size_t>(j) < frames) c += static_cast<double>(eL[i]) * eR[j];
        }
        if (c > bestC) { bestC = c; bestLag = lag; }
    }
    r.onsetLagSamples = static_cast<float>(bestLag);
    const double eLrms = rms(eL, onset, earlyEnd), eRrms = rms(eR, onset, earlyEnd);
    r.earlyLevelDiffDb = static_cast<float>(20.0 * std::log10(std::max(eLrms, 1e-12) / std::max(eRrms, 1e-12)));

    // --- Dense noise: late-tail correlation, width, mono retention (F6) ---
    std::vector<float> nL(frames, 0.0f), nR(frames, 0.0f), dL, dR;
    uint32_t s1 = 22222, s2 = 99991; // decorrelated L/R noise, 300 ms burst
    const size_t burst = static_cast<size_t>(sr * 0.3f);
    for (size_t i = 0; i < burst && i < frames; ++i) {
        s1 = s1 * 1103515245u + 12345u;
        s2 = s2 * 1103515245u + 12345u;
        nL[i] = (static_cast<float>((s1 >> 16) & 0x7FFF) / 16384.0f - 1.0f) * 0.5f;
        nR[i] = (static_cast<float>((s2 >> 16) & 0x7FFF) / 16384.0f - 1.0f) * 0.5f;
    }
    render(nL, nR, dL, dR);

    // Tail window: after the source stops, last portion of the render.
    const size_t tailStart = std::min(frames - 1, burst + static_cast<size_t>(sr * 0.3f));
    r.lateCorr = static_cast<float>(pearson(dL, dR, tailStart, frames));

    double midSq = 0, sideSq = 0, monoSq = 0, lSq = 0;
    for (size_t i = tailStart; i < frames; ++i) {
        const float mid = (dL[i] + dR[i]) * 0.5f;
        const float side = (dL[i] - dR[i]) * 0.5f;
        midSq += static_cast<double>(mid) * mid;
        sideSq += static_cast<double>(side) * side;
        monoSq += static_cast<double>(dL[i] + dR[i]) * (dL[i] + dR[i]) * 0.25f; // mono fold = (L+R)/2
        lSq += static_cast<double>(dL[i]) * dL[i];
    }
    const size_t tn = frames - tailStart;
    const double midR = std::sqrt(midSq / tn), sideR = std::sqrt(sideSq / tn);
    const double monoR = std::sqrt(monoSq / tn), lR = std::sqrt(lSq / tn);
    r.widthProxy = static_cast<float>(midR > 1e-20 ? sideR / midR : 0.0);
    r.monoRetentionDb = static_cast<float>(20.0 * std::log10(std::max(monoR, 1e-12) / std::max(lR, 1e-12)));

    return r;
}

} // namespace

int main() {
    const float sr = 48000.0f;
    const std::string outDir = "labs/reverb/stereo";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    const std::array<std::pair<const char*, AestraVerb::Mode>, 9> modes = {{
        {"room", AestraVerb::Mode::Room}, {"hall", AestraVerb::Mode::Hall},
        {"plate", AestraVerb::Mode::Plate}, {"cathedral", AestraVerb::Mode::Cathedral},
        {"chamber", AestraVerb::Mode::Chamber}, {"bright_hall", AestraVerb::Mode::BrightHall},
        {"ambience", AestraVerb::Mode::Ambience}, {"scoring", AestraVerb::Mode::Scoring},
        {"smooth_plate", AestraVerb::Mode::SmoothPlate},
    }};

    std::cout << "AestraVerb Stereo Characterization (F5/F6 re-measurement)\n";
    std::cout << "========================================================\n\n";

    std::vector<StereoResult> results;
    for (const auto& m : modes) {
        results.push_back(measureMode(m.first, m.second, sr));
        const auto& r = results.back();
        std::cout << r.mode
                  << ": earlyCorr=" << std::fixed << std::setprecision(3) << r.earlyCorr
                  << " lateCorr=" << r.lateCorr
                  << " width=" << r.widthProxy
                  << " monoRet=" << std::setprecision(2) << r.monoRetentionDb << "dB"
                  << " onsetLag=" << std::setprecision(1) << r.onsetLagSamples << "smp"
                  << " earlyLvlDiff=" << std::setprecision(2) << r.earlyLevelDiffDb << "dB\n";
    }

    // Static geometry (the review's F5/F6 code-level claims).
    auto vecCorr = [](const std::array<float, 8>& a, const std::array<float, 8>& b) {
        double sa = 0, sb = 0;
        for (int i = 0; i < 8; ++i) { sa += a[i]; sb += b[i]; }
        const double ma = sa / 8, mb = sb / 8;
        double cov = 0, va = 0, vb = 0;
        for (int i = 0; i < 8; ++i) { cov += (a[i]-ma)*(b[i]-mb); va += (a[i]-ma)*(a[i]-ma); vb += (b[i]-mb)*(b[i]-mb); }
        const double d = std::sqrt(va * vb);
        return d > 1e-20 ? cov / d : 0.0;
    };
    const double injCorr = vecCorr(AestraVerb::kInjectL, AestraVerb::kInjectR);
    const double outCorr = vecCorr(AestraVerb::kOutputL, AestraVerb::kOutputR);
    std::cout << "\nStatic geometry: injectL/R corr=" << std::setprecision(3) << injCorr
              << " outputL/R corr=" << outCorr << "\n";
    std::cout << "Early taps: R delay = L delay + ((tap%3)+1) samples (1-3 smp, ~0.02-0.06 ms)\n";
    std::cout << "Early cross-mix: earlyL += 0.28*R, earlyR += -0.22*L (per tap)\n";

    // --- Reports ---
    std::ostringstream md;
    md << "# AestraVerb Stereo Characterization (F5/F6 re-measurement)\n\n";
    md << "Measured on the current engine (post SIMD ring-fix, post F4 modulation rewrite). "
          "Supersedes the review's F5/F6 numbers, which were taken on mislabeled modes and the "
          "old modulator. 48 kHz, Width 0.7, Decay 0.7, Size 0.6.\n\n";
    md << "## Per-mode image\n\n";
    md << "| Mode | EarlyCorr | LateCorr | Width | MonoRet(dB) | OnsetLag(smp) | EarlyLvlDiff(dB) |\n";
    md << "|------|-----------|----------|-------|-------------|---------------|------------------|\n";
    for (const auto& r : results) {
        md << "| " << r.mode
           << " | " << std::fixed << std::setprecision(3) << r.earlyCorr
           << " | " << r.lateCorr
           << " | " << r.widthProxy
           << " | " << std::setprecision(2) << r.monoRetentionDb
           << " | " << std::setprecision(1) << r.onsetLagSamples
           << " | " << std::setprecision(2) << r.earlyLevelDiffDb
           << " |\n";
    }
    md << "\n**Column meaning.** EarlyCorr: inter-channel correlation over the 20 ms after "
          "onset (impulse). 1.0 = mono onset (the F5 concern). LateCorr: correlation of the "
          "reverb tail on decorrelated dense noise; strongly negative risks mono cancellation "
          "(the F6 concern). Width: side/mid RMS on the tail. MonoRet: mono fold-down RMS "
          "relative to L; 0 dB = no loss, negative = cancellation on mono sum. OnsetLag: best "
          "L->R cross-correlation lag. EarlyLvlDiff: L vs R early RMS.\n\n";
    md << "## Static geometry (code-level cause)\n\n";
    md << "- FDN injection vectors L/R correlation: **" << std::setprecision(3) << injCorr << "**\n";
    md << "- FDN output vectors L/R correlation: **" << outCorr << "**\n";
    md << "- Early reflections: R tap delay = L tap delay + `((tap%3)+1)` samples "
          "(1-3 smp, ~0.02-0.06 ms at 48 kHz); same gain both channels; cross-mix "
          "`earlyL += 0.28*R`, `earlyR += -0.22*L` per tap.\n";

    std::ostringstream js;
    js << "{\n  \"sampleRate\": " << static_cast<int>(sr)
       << ",\n  \"injectCorr\": " << injCorr << ",\n  \"outputCorr\": " << outCorr
       << ",\n  \"modes\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        js << "    {\"mode\": \"" << r.mode << "\", \"earlyCorr\": " << r.earlyCorr
           << ", \"lateCorr\": " << r.lateCorr << ", \"widthProxy\": " << r.widthProxy
           << ", \"monoRetentionDb\": " << r.monoRetentionDb
           << ", \"onsetLagSamples\": " << r.onsetLagSamples
           << ", \"earlyLevelDiffDb\": " << r.earlyLevelDiffDb << "}"
           << (i + 1 < results.size() ? "," : "") << "\n";
    }
    js << "  ]\n}\n";

    { std::ofstream f(outDir + "/stereo_characterization.md"); if (f) f << md.str(); }
    { std::ofstream f(outDir + "/stereo_characterization.json"); if (f) f << js.str(); }

    std::cout << "\nStereo characterization written to " << outDir << "/\n";
    return 0;
}
