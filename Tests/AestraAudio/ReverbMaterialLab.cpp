// AestraVerb Material Validation Lab
// Generates synthetic listening material, renders through Room/Hall/Plate,
// and produces analytical reports.
//
// Outputs:
//   - labs/reverb/quality/session_017a_material/*.wav
//   - labs/reverb/quality/session_017a_material_report.json
//   - labs/reverb/quality/session_017a_material_report.md

#include "AestraVerb.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraVerb;

// ============================================================================
// WAV file writer (32-bit float stereo)
// ============================================================================
static bool writeWavFloat(const std::string& path, const std::vector<float>& left,
                          const std::vector<float>& right, float sampleRate) {
    if (left.size() != right.size() || left.empty()) return false;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    uint32_t numSamples = static_cast<uint32_t>(left.size());
    uint32_t byteRate = static_cast<uint32_t>(sampleRate) * 8;
    uint32_t dataSize = numSamples * 8;
    uint32_t fileSize = 36 + dataSize;

    auto writeU32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4);
    writeU32(fileSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    writeU32(16);
    writeU16(3);
    writeU16(2);
    writeU32(static_cast<uint32_t>(sampleRate));
    writeU32(byteRate);
    writeU16(8);
    writeU16(32);
    f.write("data", 4);
    writeU32(dataSize);

    for (uint32_t i = 0; i < numSamples; ++i) {
        float l = left[i];
        float r = right[i];
        f.write(reinterpret_cast<const char*>(&l), 4);
        f.write(reinterpret_cast<const char*>(&r), 4);
    }
    return f.good();
}

// ============================================================================
// Signal generators
// ============================================================================
static std::vector<float> generateSnare(float sampleRate, float duration) {
    size_t n = static_cast<size_t>(sampleRate * duration);
    std::vector<float> sig(n, 0.0f);
    uint32_t seed = 54321;
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1103515245u + 12345u;
        float noise = static_cast<float>((seed >> 16) & 0x7FFFu) / 16384.0f - 1.0f;
        float t = static_cast<float>(i) / sampleRate;
        float env = std::exp(-t / 0.015f); // 15ms decay
        sig[i] = noise * env * 0.8f;
    }
    // Add body tone (200 Hz)
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float env = std::exp(-t / 0.04f);
        sig[i] += std::sin(2.0f * 3.14159265f * 200.0f * t) * env * 0.3f;
    }
    return sig;
}

static std::vector<float> generateBrightPing(float sampleRate, float duration) {
    size_t n = static_cast<size_t>(sampleRate * duration);
    std::vector<float> sig(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float env = std::exp(-t / 0.02f);
        sig[i] = std::sin(2.0f * 3.14159265f * 2000.0f * t) * env * 0.9f;
    }
    return sig;
}

static std::vector<float> generateVocalPhrase(float sampleRate, float duration) {
    size_t n = static_cast<size_t>(sampleRate * duration);
    std::vector<float> sig(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        // Simple "ahh": fundamental 220Hz + vibrato + formant-like shaping
        float vibrato = 1.0f + 0.02f * std::sin(2.0f * 3.14159265f * 5.0f * t);
        float freq = 220.0f * vibrato;
        float env = 1.0f;
        if (t < 0.05f) env = t / 0.05f;
        else if (t > duration - 0.1f) env = (duration - t) / 0.1f;
        float v = std::sin(2.0f * 3.14159265f * freq * t);
        v += 0.3f * std::sin(2.0f * 3.14159265f * freq * 2.0f * t);
        v += 0.15f * std::sin(2.0f * 3.14159265f * freq * 3.0f * t);
        sig[i] = v * env * 0.7f;
    }
    return sig;
}

static std::vector<float> generateChordStab(float sampleRate, float duration) {
    size_t n = static_cast<size_t>(sampleRate * duration);
    std::vector<float> sig(n, 0.0f);
    float freqs[] = {261.63f, 329.63f, 392.00f, 523.25f};
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float env = 1.0f;
        if (t < 0.01f) env = t / 0.01f;
        else env = std::exp(-(t - 0.01f) / 0.15f);
        float v = 0.0f;
        for (float f : freqs) {
            v += std::sin(2.0f * 3.14159265f * f * t);
        }
        sig[i] = v * env * 0.25f;
    }
    return sig;
}

static std::vector<float> generateLowPulse(float sampleRate, float duration) {
    size_t n = static_cast<size_t>(sampleRate * duration);
    std::vector<float> sig(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float env = std::exp(-t / 0.08f);
        sig[i] = std::sin(2.0f * 3.14159265f * 80.0f * t) * env * 0.9f;
    }
    return sig;
}

static std::vector<float> generateMixBus(float sampleRate, float duration) {
    size_t n = static_cast<size_t>(sampleRate * duration);
    std::vector<float> sig(n, 0.0f);
    // Kick: 80Hz pulse at 0ms
    for (size_t i = 0; i < n && i < static_cast<size_t>(sampleRate * 0.05f); ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float env = std::exp(-t / 0.03f);
        sig[i] += std::sin(2.0f * 3.14159265f * 80.0f * t) * env * 0.6f;
    }
    // Snare: noise at 100ms
    uint32_t seed = 99999;
    size_t snareStart = static_cast<size_t>(sampleRate * 0.1f);
    size_t snareEnd = std::min(n, snareStart + static_cast<size_t>(sampleRate * 0.05f));
    for (size_t i = snareStart; i < snareEnd; ++i) {
        seed = seed * 1103515245u + 12345u;
        float noise = static_cast<float>((seed >> 16) & 0x7FFFu) / 16384.0f - 1.0f;
        float t = static_cast<float>(i - snareStart) / sampleRate;
        float env = std::exp(-t / 0.015f);
        sig[i] += noise * env * 0.5f;
    }
    // Pad: slow chord from 200ms
    float freqs[] = {261.63f, 329.63f, 392.00f};
    size_t padStart = static_cast<size_t>(sampleRate * 0.2f);
    for (size_t i = padStart; i < n; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float env = 1.0f;
        if (t < 0.25f) env = (t - 0.2f) / 0.05f;
        else env = std::exp(-(t - 0.25f) / 0.3f);
        float v = 0.0f;
        for (float f : freqs) v += std::sin(2.0f * 3.14159265f * f * t);
        sig[i] += v * env * 0.15f;
    }
    return sig;
}

// ============================================================================
// Render through AestraVerb
// ============================================================================
struct MaterialResult {
    std::string name;
    std::string modeName;
    float peak = 0.0f;
    float rms = 0.0f;
    float crestFactor = 0.0f;
    float spectralCentroid = 0.0f;
    float lowRatio = 0.0f;
    float midRatio = 0.0f;
    float highRatio = 0.0f;
    float corrLate = 0.0f;
    float monoPeak = 0.0f;
    float monoRms = 0.0f;
    float metallicPeakHz = 0.0f;
    float metallicPeakDb = 0.0f;
    std::string metallicSeverity;
    float earlyDensityScore = 0.0f;
    float tailRms = 0.0f;
    // Clamp diagnostics (Session 017a)
    float preClampPeak = 0.0f;
    float postClampPeak = 0.0f;
    float wetPreClampPeak = 0.0f;
    uint64_t clampSampleCount = 0;
    uint64_t totalSamples = 0;
    float sourcePeak = 0.0f;
    std::string clampStatus;  // "clamped" | "not clamped" | "pre-existing vocal clamp case"
};

static MaterialResult analyzeMaterial(const std::string& name, const std::string& modeName,
                                      const std::vector<float>& left, const std::vector<float>& right,
                                      float sampleRate, size_t sourceFrames) {
    MaterialResult r;
    r.name = name;
    r.modeName = modeName;
    size_t n = left.size();
    if (n == 0 || right.size() != n) return r;

    // The reverb tail is everything AFTER the source stops feeding the input.
    // Sources like the vocal and mix bus sustain for their whole duration, so a
    // naive "last 50% of the buffer" window still contained live source energy
    // and made tail measurements meaningless. Anchor the tail to the end of the
    // source instead. Fall back to the buffer midpoint if the source somehow
    // fills the render.
    size_t tailStart = (sourceFrames < n) ? sourceFrames : n / 2;

    // Peak and RMS
    double sumSq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        r.peak = std::max(r.peak, std::max(std::abs(left[i]), std::abs(right[i])));
        sumSq += left[i] * left[i] + right[i] * right[i];
    }
    r.rms = static_cast<float>(std::sqrt(sumSq / (2.0 * n)));
    r.crestFactor = r.peak / std::max(r.rms, 1e-20f);

    // Spectral balance (very simple: energy in low/mid/high from sample differences)
    double lowE = 0.0, midE = 0.0, highE = 0.0;
    for (size_t i = 1; i < n; ++i) {
        float diff = left[i] - left[i-1];
        float energy = diff * diff;
        // Rough frequency proxy from difference magnitude
        float avgSample = (std::abs(left[i]) + std::abs(right[i])) * 0.5f;
        if (avgSample > 0.0f) {
            // Use zero-crossing rate approximation
            if (std::abs(diff) < 0.001f) lowE += avgSample * avgSample;
            else if (std::abs(diff) < 0.01f) midE += avgSample * avgSample;
            else highE += avgSample * avgSample;
        }
    }
    double totalE = lowE + midE + highE;
    if (totalE > 0.0) {
        r.lowRatio = static_cast<float>(lowE / totalE);
        r.midRatio = static_cast<float>(midE / totalE);
        r.highRatio = static_cast<float>(highE / totalE);
    }

    // Late correlation (last 20%)
    size_t lateStart = n * 4 / 5;
    double sumL = 0.0, sumR = 0.0, sumLR = 0.0, sumL2 = 0.0, sumR2 = 0.0;
    for (size_t i = lateStart; i < n; ++i) {
        sumL += left[i]; sumR += right[i];
        sumLR += left[i] * right[i];
        sumL2 += left[i] * left[i];
        sumR2 += right[i] * right[i];
    }
    size_t lateCount = n - lateStart;
    double meanL = sumL / lateCount;
    double meanR = sumR / lateCount;
    double cov = (sumLR / lateCount) - meanL * meanR;
    double varL = (sumL2 / lateCount) - meanL * meanL;
    double varR = (sumR2 / lateCount) - meanR * meanR;
    r.corrLate = static_cast<float>(cov / std::max(std::sqrt(varL * varR), 1e-20));

    // Mono peak and RMS
    for (size_t i = 0; i < n; ++i) {
        float mono = (left[i] + right[i]) * 0.5f;
        r.monoPeak = std::max(r.monoPeak, std::abs(mono));
    }

    // Tail RMS (post-source region only)
    double tailSumSq = 0.0;
    size_t tailCount = (n > tailStart) ? (n - tailStart) : 0;
    for (size_t i = tailStart; i < n; ++i) {
        tailSumSq += left[i] * left[i] + right[i] * right[i];
    }
    r.tailRms = tailCount > 0 ? static_cast<float>(std::sqrt(tailSumSq / (2.0 * tailCount))) : 0.0f;

    // Metallic peak (simplified) — analyze inside the post-source tail.
    size_t start = tailStart;
    size_t fftSize = 2048;
    if (start + fftSize > n) start = (n > fftSize) ? (n - fftSize) : 0;
    // Very rough: just find max energy band in late tail using zero-crossing approximation
    float maxBand = 0.0f;
    float totalBand = 0.0f;
    for (size_t i = start + 1; i < start + fftSize && i < n; ++i) {
        float diff = std::abs(left[i] - left[i-1]);
        totalBand += diff;
        if (diff > maxBand) maxBand = diff;
    }
    if (totalBand > 0.0f) {
        r.metallicPeakDb = 20.0f * std::log10(maxBand / (totalBand / fftSize));
        r.metallicSeverity = (r.metallicPeakDb > 20.0f) ? "medium" : "low";
    } else {
        r.metallicSeverity = "none";
    }

    return r;
}

// ============================================================================
// Report generation
// ============================================================================
static std::string generateJsonReport(const std::vector<MaterialResult>& results) {
    std::ostringstream j;
    j << "{\n";
    j << "  \"results\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        j << "    {\n";
        j << "      \"name\": \"" << r.name << "\",\n";
        j << "      \"mode\": \"" << r.modeName << "\",\n";
        j << "      \"peak\": " << r.peak << ",\n";
        j << "      \"rms\": " << r.rms << ",\n";
        j << "      \"crestFactor\": " << r.crestFactor << ",\n";
        j << "      \"lowRatio\": " << r.lowRatio << ",\n";
        j << "      \"midRatio\": " << r.midRatio << ",\n";
        j << "      \"highRatio\": " << r.highRatio << ",\n";
        j << "      \"corrLate\": " << r.corrLate << ",\n";
        j << "      \"monoPeak\": " << r.monoPeak << ",\n";
        j << "      \"tailRms\": " << r.tailRms << ",\n";
        j << "      \"metallicPeakDb\": " << r.metallicPeakDb << ",\n";
        j << "      \"metallicSeverity\": \"" << r.metallicSeverity << "\",\n";
        j << "      \"sourcePeak\": " << r.sourcePeak << ",\n";
        j << "      \"preClampPeak\": " << r.preClampPeak << ",\n";
        j << "      \"postClampPeak\": " << r.postClampPeak << ",\n";
        j << "      \"wetPreClampPeak\": " << r.wetPreClampPeak << ",\n";
        j << "      \"clampSampleCount\": " << r.clampSampleCount << ",\n";
        j << "      \"totalSamples\": " << r.totalSamples << ",\n";
        j << "      \"clampStatus\": \"" << r.clampStatus << "\"\n";
        j << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
    }
    j << "  ]\n";
    j << "}\n";
    return j.str();
}

static std::string generateMarkdownReport(const std::vector<MaterialResult>& results) {
    std::ostringstream md;
    md << "# AestraVerb Synthetic Material Validation Report\n\n";
    md << "Generated by AestraReverbMaterialLab. Tail metrics are measured over the "
          "post-source region (silence appended after each source so the reverb decay "
          "is actually captured).\n\n";

    md << "## Summary Table\n\n";
    md << "| Source | Mode | Peak | RMS | Crest | Low | Mid | High | LateCorr | MonoPeak | TailRMS | Metallic |\n";
    md << "|--------|------|------|-----|-------|-----|-----|------|----------|----------|---------|----------|\n";
    for (const auto& r : results) {
        md << "| " << r.name << " | " << r.modeName
           << " | " << std::fixed << std::setprecision(3) << r.peak
           << " | " << std::setprecision(4) << r.rms
           << " | " << std::setprecision(1) << r.crestFactor
           << " | " << std::setprecision(2) << r.lowRatio
           << " | " << std::setprecision(2) << r.midRatio
           << " | " << std::setprecision(2) << r.highRatio
           << " | " << std::setprecision(3) << r.corrLate
           << " | " << std::setprecision(3) << r.monoPeak
           << " | " << std::setprecision(4) << r.tailRms
           << " | " << std::setprecision(1) << r.metallicPeakDb << " dB (" << r.metallicSeverity << ")"
           << " |\n";
    }

    // Clamp diagnostics table
    md << "\n## Clamp Diagnostics\n\n";
    md << "| Source | Mode | SourcePeak | PreClampPeak | PostClampPeak | WetPrePeak | ClampSamples | TotalSamples | ClampPct | Status |\n";
    md << "|--------|------|-----------|-------------|--------------|-----------|-------------|-------------|---------|--------|\n";
    for (const auto& r : results) {
        float pct = r.totalSamples > 0
            ? (100.0f * static_cast<float>(r.clampSampleCount) / static_cast<float>(r.totalSamples))
            : 0.0f;
        md << "| " << r.name << " | " << r.modeName
           << " | " << std::fixed << std::setprecision(4) << r.sourcePeak
           << " | " << std::setprecision(4) << r.preClampPeak
           << " | " << std::setprecision(4) << r.postClampPeak
           << " | " << std::setprecision(4) << r.wetPreClampPeak
           << " | " << r.clampSampleCount
           << " | " << r.totalSamples
           << " | " << std::setprecision(2) << pct << "%"
           << " | " << r.clampStatus
           << " |\n";
    }

    // Summarize clamped cases
    md << "\n### Clamp Summary\n\n";
    int clampedCount = 0;
    int vocalClampCount = 0;
    int nonVocalClampCount = 0;
    for (const auto& r : results) {
        if (r.clampSampleCount > 0) {
            ++clampedCount;
            if (r.name == "vocal_phrase") ++vocalClampCount;
            else ++nonVocalClampCount;
        }
    }
    md << "- Total clamped source/mode pairs: **" << clampedCount << "** / " << results.size() << "\n";
    md << "- Pre-existing vocal clamps: **" << vocalClampCount << "**\n";
    md << "- Non-vocal clamps: **" << nonVocalClampCount << "**\n";
    if (nonVocalClampCount > 0) {
        md << "\n> ⚠️ **WARNING**: Non-vocal source(s) are clamping unexpectedly.\n";
    }

    // Current Plate/Room tail-energy ratios (post-source window).
    md << "\n## Plate vs Room Tail Energy\n\n";
    md << "> The Session 015/016 Plate baselines that used to sit here were measured "
          "against mislabeled modes (Hall/Plate decoded to Chamber/SmoothPlate) and a "
          "tail window that still contained the live source. Both bugs are fixed, so "
          "those baselines are no longer comparable and have been removed rather than "
          "presented as an apples-to-apples before/after. The values below are the "
          "current, correctly-measured ratios over the post-source tail.\n\n";
    md << "| Source | Plate tailRMS | Room tailRMS | Plate % of Room |\n";
    md << "|--------|---------------|--------------|-----------------|\n";
    for (const char* source : {"snare", "bright_ping", "chord_stab"}) {
        float plateTail = 0.0f, roomTail = 0.0f;
        for (const auto& r : results) {
            if (r.name == source && r.modeName == "plate") plateTail = r.tailRms;
            if (r.name == source && r.modeName == "room") roomTail = r.tailRms;
        }
        float pctOfRoom = (roomTail > 0.0f) ? (plateTail / roomTail * 100.0f) : 0.0f;
        md << "| " << source
           << " | " << std::fixed << std::setprecision(5) << plateTail
           << " | " << std::setprecision(5) << roomTail
           << " | " << std::setprecision(1) << pctOfRoom << "%"
           << " |\n";
    }

    md << "\n## Analysis\n\n";

    // Group by source
    for (const auto& source : {"snare", "bright_ping", "vocal_phrase", "chord_stab", "low_pulse", "mix_bus"}) {
        md << "### " << source << "\n\n";
        for (const auto& r : results) {
            if (r.name == source) {
                md << "- **" << r.modeName << "**: crest=" << std::setprecision(1) << r.crestFactor
                   << " lateCorr=" << std::setprecision(3) << r.corrLate
                   << " tailRMS=" << std::setprecision(4) << r.tailRms
                   << " metallic=" << std::setprecision(1) << r.metallicPeakDb << "dB\n";
            }
        }
        md << "\n";
    }

    md << "## Conclusions\n\n";
    md << "1. **Plate tail energy**: Compare tailRMS across modes for each source.\n";
    md << "2. **Stereo width**: Late correlation should be healthy (>0.5 for wide modes).\n";
    md << "3. **Metallic ringing**: Check if any source shows high severity.\n";
    md << "4. **Mono safety**: Mono peak should not exceed ~0.5 for most sources.\n";

    return md.str();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    const float sampleRate = 48000.0f;
    const std::string outDir = "labs/reverb/quality/session_017a_material";
    (void)std::system(("mkdir -p " + outDir).c_str());

    struct SourceDef {
        std::string name;
        std::vector<float> (*generator)(float, float);
        float duration;
        float modeParam;
        float decay;
        float size;
        float diffusion;
    };

    std::vector<SourceDef> sources = {
        {"snare", generateSnare, 0.5f, 0.0f, 0.6f, 0.5f, 0.7f},
        {"bright_ping", generateBrightPing, 0.5f, 0.0f, 0.6f, 0.5f, 0.7f},
        {"vocal_phrase", generateVocalPhrase, 1.0f, 0.0f, 0.6f, 0.5f, 0.7f},
        {"chord_stab", generateChordStab, 0.5f, 0.0f, 0.6f, 0.5f, 0.7f},
        {"low_pulse", generateLowPulse, 0.5f, 0.0f, 0.6f, 0.5f, 0.7f},
        {"mix_bus", generateMixBus, 1.0f, 0.0f, 0.6f, 0.5f, 0.7f},
    };

    struct ModeDef {
        std::string name;
        float param;
    };
    // Select each mode via the canonical AestraVerb::modeParam() so the labeled
    // mode is the mode actually rendered. Hardcoded 0.5/1.0 previously decoded
    // to Chamber (mode 4) and SmoothPlate (mode 8), not Hall and Plate.
    std::vector<ModeDef> modes = {
        {"room", AestraVerb::modeParam(AestraVerb::Mode::Room)},
        {"hall", AestraVerb::modeParam(AestraVerb::Mode::Hall)},
        {"plate", AestraVerb::modeParam(AestraVerb::Mode::Plate)},
    };

    std::vector<MaterialResult> results;

    for (const auto& src : sources) {
        auto dry = src.generator(sampleRate, src.duration);
        size_t sourceFrames = dry.size();

        // Append silence so the reverb tail is actually rendered instead of being
        // truncated at the moment the source stops. Tail length scales with decay.
        const float tailSeconds = std::clamp(1.0f + src.decay * 4.0f, 1.0f, 6.0f);
        const size_t tailFrames = static_cast<size_t>(sampleRate * tailSeconds);
        const size_t n = sourceFrames + tailFrames;

        std::vector<float> dryL(n, 0.0f), dryR(n, 0.0f);
        for (size_t i = 0; i < sourceFrames; ++i) { dryL[i] = dry[i]; dryR[i] = dry[i]; }
        writeWavFloat(outDir + "/dry_" + src.name + ".wav", dryL, dryR, sampleRate);

        // Compute source peak for diagnostics
        float srcPeak = 0.0f;
        for (size_t i = 0; i < sourceFrames; ++i) {
            srcPeak = std::max(srcPeak, std::abs(dry[i]));
        }

        for (const auto& mode : modes) {
            std::cout << "Rendering " << src.name << " -> " << mode.name << "..." << std::flush;

            AestraVerb verb;
            verb.initialize(sampleRate, 256);
            verb.setParameter(AestraVerb::kMode, mode.param);
            verb.setParameter(AestraVerb::kDecay, src.decay);
            verb.setParameter(AestraVerb::kSize, src.size);
            verb.setParameter(AestraVerb::kDiffusion, src.diffusion);
            verb.setParameter(AestraVerb::kModRate, 0.5f);
            verb.setParameter(AestraVerb::kModDepth, 0.3f);
            verb.setParameter(AestraVerb::kMix, 1.0f);
            verb.setParameter(AestraVerb::kWidth, 0.7f);
            verb.activate();

            // Reset diagnostics before render (requires AESTRA_REVERB_DIAGNOSTICS)
#ifdef AESTRA_REVERB_DIAGNOSTICS
            verb.resetDiagnostics();
            verb.setSourcePeak(srcPeak);
#endif

            std::vector<float> outL(n), outR(n);
            size_t chunkSize = 256;
            for (size_t offset = 0; offset < n; offset += chunkSize) {
                size_t frames = std::min(chunkSize, n - offset);
                const float* inPtrs[2] = {dryL.data() + offset, dryR.data() + offset};
                float* outPtrs[2] = {outL.data() + offset, outR.data() + offset};
                verb.process(inPtrs, outPtrs, 2, 2, static_cast<uint32_t>(frames));
            }

            writeWavFloat(outDir + "/" + mode.name + "_" + src.name + ".wav", outL, outR, sampleRate);

            // Hard invariant: rendered output must be finite. This is the one
            // correctness gate this lab enforces; everything else is reported
            // characterization (see the checks section in main()).
            for (size_t i = 0; i < n; ++i) {
                if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) {
                    std::cerr << "\nFATAL: non-finite output in " << mode.name << " " << src.name
                              << " at sample " << i << "\n";
                    return 2;
                }
            }

            auto res = analyzeMaterial(src.name, mode.name, outL, outR, sampleRate, sourceFrames);

            // Populate clamp diagnostics (requires AESTRA_REVERB_DIAGNOSTICS)
#ifdef AESTRA_REVERB_DIAGNOSTICS
            {
                auto diag = verb.getClampDiagnostics();
                res.preClampPeak = diag.preClampPeak;
                res.postClampPeak = diag.postClampPeak;
                res.wetPreClampPeak = diag.wetPreClampPeak;
                res.clampSampleCount = diag.clampSampleCount;
                res.totalSamples = diag.totalSamples;
                res.sourcePeak = diag.sourcePeak;

                // Determine clamp status
                if (diag.clampSampleCount > 0) {
                    if (src.name == "vocal_phrase") {
                        res.clampStatus = "pre-existing vocal clamp case";
                    } else {
                        res.clampStatus = "clamped";
                    }
                } else {
                    res.clampStatus = "not clamped";
                }
            }
#else
            res.clampStatus = "diagnostics disabled";
#endif

            results.push_back(res);

            // Print clamp info inline
#ifdef AESTRA_REVERB_DIAGNOSTICS
            if (res.clampSampleCount > 0) {
                float pct = res.totalSamples > 0
                    ? (100.0f * static_cast<float>(res.clampSampleCount) / static_cast<float>(res.totalSamples))
                    : 0.0f;
                std::cout << " CLAMPED (preClampPeak=" << std::fixed << std::setprecision(4) << res.preClampPeak
                          << " clampSamples=" << res.clampSampleCount
                          << " " << std::setprecision(2) << pct << "%)";
            }
#endif
            std::cout << " done" << std::endl;
        }
    }

    // Write reports
    {
        std::ofstream f("labs/reverb/quality/session_017a_material_report.json");
        if (f) f << generateJsonReport(results);
    }
    {
        std::ofstream f("labs/reverb/quality/session_017a_material_report.md");
        if (f) f << generateMarkdownReport(results);
    }

    // ========================================================================
    // Characterization report + invariant checks
    // ========================================================================
    // NOTE ON BASELINES: the absolute tail-RMS thresholds and the snare Room
    // target below were calibrated in Sessions 015/016/023 against measurements
    // that are now known to be wrong — the labs decoded Hall/Plate to the wrong
    // modes (F1) and measured the "tail" over a window that still contained the
    // live source (N5). Both were fixed in this pass, which invalidates those
    // reference numbers. Rather than retune the reverb to satisfy a broken
    // metric (explicitly out of bounds for this task) or silently re-pin the
    // thresholds to whatever the code does today (which would fake a
    // validation), the tail ratios are REPORTED here and no longer gate. The
    // real metallic-ring / spectral-purity regression is enforced by
    // ReverbConsistencyProbe's probeModulationSpectralPurity() with a true-FFT
    // assertion. This lab's one hard gate is finite output (checked at render).
    // Owner re-baselining is required before any of these become gates again.
    int regressionFailures = 0;

    auto findResult = [&](const std::string& name, const std::string& mode) -> const MaterialResult* {
        for (const auto& r : results) {
            if (r.name == name && r.modeName == mode) return &r;
        }
        return nullptr;
    };

    // Plate metallic severity (crude difference-crest proxy, reported only).
    std::cout << "\n--- Characterization (reported, non-gating) ---\n";
    for (const auto& r : results) {
        if (r.modeName == "plate") {
            std::cout << "  Plate metallic severity for " << r.name
                      << ": " << r.metallicSeverity
                      << " (" << std::fixed << std::setprecision(1) << r.metallicPeakDb << " dB proxy)\n";
        }
    }

    // Plate/Room tail-energy ratio (reported; baselines invalidated, see note).
    for (const char* source : {"snare", "bright_ping", "chord_stab"}) {
        const auto* plate = findResult(source, "plate");
        const auto* room = findResult(source, "room");
        if (plate && room && room->tailRms > 0.0f) {
            float ratio = plate->tailRms / room->tailRms;
            std::cout << "  " << source << " Plate/Room tail ratio = "
                      << std::fixed << std::setprecision(1) << ratio * 100.0f << "%"
                      << " (plate tailRMS=" << std::setprecision(5) << plate->tailRms
                      << ", room tailRMS=" << room->tailRms << ")\n";
        }
    }
    {
        const auto* room = findResult("snare", "room");
        if (room) {
            std::cout << "  snare Room tailRMS = " << std::fixed << std::setprecision(5)
                      << room->tailRms << " (post-source window)\n";
        }
    }

    // Clamp audit — check ALL source/mode pairs using diagnostics
    // vocal_phrase is known to clamp in all modes (pre-existing). Report but do not fail.
    // Non-vocal sources that clamp are unexpected and should fail.
    std::cout << "\n--- Clamp Audit ---\n";
    for (const auto& r : results) {
        if (r.clampSampleCount > 0) {
            float pct = r.totalSamples > 0
                ? (100.0f * static_cast<float>(r.clampSampleCount) / static_cast<float>(r.totalSamples))
                : 0.0f;
            if (r.name == "vocal_phrase") {
                std::cout << "KNOWN: " << r.modeName << " " << r.name
                          << " preClampPeak=" << std::fixed << std::setprecision(4) << r.preClampPeak
                          << " wetPrePeak=" << r.wetPreClampPeak
                          << " clampSamples=" << r.clampSampleCount
                          << " (" << std::setprecision(2) << pct << "%)"
                          << " — pre-existing vocal clamp case\n";
            } else {
                std::cerr << "FAIL: " << r.modeName << " " << r.name
                          << " preClampPeak=" << std::fixed << std::setprecision(4) << r.preClampPeak
                          << " wetPrePeak=" << r.wetPreClampPeak
                          << " clampSamples=" << r.clampSampleCount
                          << " (" << std::setprecision(2) << pct << "%)"
                          << " — unexpected non-vocal clamp\n";
                ++regressionFailures;
            }
        } else {
            std::cout << "PASS: " << r.modeName << " " << r.name
                      << " preClampPeak=" << std::fixed << std::setprecision(4) << r.preClampPeak
                      << " — not clamped\n";
        }
    }

    if (regressionFailures > 0) {
        std::cerr << "\n" << regressionFailures << " regression check(s) FAILED.\n";
        return 1;
    }

    std::cout << "\nAll regression checks passed.\n";
    std::cout << "Material validation complete.\n";
    std::cout << "Artifacts written to: " << outDir << "/\n";
    std::cout << "Report: labs/reverb/quality/session_017a_material_report.md\n";
    return 0;
}
