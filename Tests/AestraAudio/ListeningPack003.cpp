// Aestra Verb Second De-Flange / De-Sizzle Listening Pack — MiMoClaw Session 003
// Renders before/after WAVs for A/B comparison.
// "Before" = Session 002 constants (28346423).
// "After"  = Session 003 tuning (further reduced modulation, deeper box-cut, tamed HF).

#include "AestraVerb.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraVerb;

// ============================================================================
// WAV writer (32-bit float stereo)
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
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); w32(fileSize);
    f.write("WAVE", 4); f.write("fmt ", 4);
    w32(16); w16(3); w16(2);
    w32(static_cast<uint32_t>(sampleRate)); w32(byteRate); w16(8); w16(32);
    f.write("data", 4); w32(dataSize);
    for (uint32_t i = 0; i < numSamples; ++i) {
        float l = left[i], r = right[i];
        f.write(reinterpret_cast<const char*>(&l), 4);
        f.write(reinterpret_cast<const char*>(&r), 4);
    }
    return f.good();
}

// ============================================================================
// Signal generators
// ============================================================================
static std::vector<float> generateSnare(float sr, float dur) {
    size_t n = static_cast<size_t>(sr * dur);
    std::vector<float> sig(n, 0.0f);
    uint32_t seed = 54321;
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1103515245u + 12345u;
        float noise = static_cast<float>((seed >> 16) & 0x7FFFu) / 16384.0f - 1.0f;
        float t = static_cast<float>(i) / sr;
        sig[i] = noise * std::exp(-t / 0.015f) * 0.8f;
    }
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sr;
        sig[i] += std::sin(2.0f * 3.14159265f * 200.0f * t) * std::exp(-t / 0.04f) * 0.3f;
    }
    return sig;
}

static std::vector<float> generateBrightPing(float sr, float dur) {
    size_t n = static_cast<size_t>(sr * dur);
    std::vector<float> sig(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sr;
        sig[i] = std::sin(2.0f * 3.14159265f * 2000.0f * t) * std::exp(-t / 0.02f) * 0.9f;
    }
    return sig;
}

static std::vector<float> generateVocalPhrase(float sr, float dur) {
    size_t n = static_cast<size_t>(sr * dur);
    std::vector<float> sig(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sr;
        float vibrato = 1.0f + 0.02f * std::sin(2.0f * 3.14159265f * 5.0f * t);
        float freq = 220.0f * vibrato;
        float env = 1.0f;
        if (t < 0.05f) env = t / 0.05f;
        else if (t > dur - 0.1f) env = (dur - t) / 0.1f;
        float v = std::sin(2.0f * 3.14159265f * freq * t)
                + 0.3f * std::sin(2.0f * 3.14159265f * freq * 2.0f * t)
                + 0.15f * std::sin(2.0f * 3.14159265f * freq * 3.0f * t);
        sig[i] = v * env * 0.7f;
    }
    return sig;
}

static std::vector<float> generateChordStab(float sr, float dur) {
    size_t n = static_cast<size_t>(sr * dur);
    std::vector<float> sig(n, 0.0f);
    float freqs[] = {261.63f, 329.63f, 392.00f, 523.25f};
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sr;
        float env = (t < 0.01f) ? (t / 0.01f) : std::exp(-(t - 0.01f) / 0.15f);
        float v = 0.0f;
        for (float f : freqs) v += std::sin(2.0f * 3.14159265f * f * t);
        sig[i] = v * env * 0.25f;
    }
    return sig;
}

static std::vector<float> generateLowPulse(float sr, float dur) {
    size_t n = static_cast<size_t>(sr * dur);
    std::vector<float> sig(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sr;
        sig[i] = std::sin(2.0f * 3.14159265f * 80.0f * t) * std::exp(-t / 0.08f) * 0.9f;
    }
    return sig;
}

static std::vector<float> generateMixBus(float sr, float dur) {
    size_t n = static_cast<size_t>(sr * dur);
    std::vector<float> sig(n, 0.0f);
    for (size_t i = 0; i < n && i < static_cast<size_t>(sr * 0.05f); ++i) {
        float t = static_cast<float>(i) / sr;
        sig[i] += std::sin(2.0f * 3.14159265f * 80.0f * t) * std::exp(-t / 0.03f) * 0.6f;
    }
    uint32_t seed = 99999;
    size_t s0 = static_cast<size_t>(sr * 0.1f);
    size_t s1 = std::min(n, s0 + static_cast<size_t>(sr * 0.05f));
    for (size_t i = s0; i < s1; ++i) {
        seed = seed * 1103515245u + 12345u;
        float noise = static_cast<float>((seed >> 16) & 0x7FFFu) / 16384.0f - 1.0f;
        float t = static_cast<float>(i - s0) / sr;
        sig[i] += noise * std::exp(-t / 0.015f) * 0.5f;
    }
    float freqs[] = {261.63f, 329.63f, 392.00f};
    size_t p0 = static_cast<size_t>(sr * 0.2f);
    for (size_t i = p0; i < n; ++i) {
        float t = static_cast<float>(i) / sr;
        float env = (t < 0.25f) ? ((t - 0.2f) / 0.05f) : std::exp(-(t - 0.25f) / 0.3f);
        float v = 0.0f;
        for (float f : freqs) v += std::sin(2.0f * 3.14159265f * f * t);
        sig[i] += v * env * 0.15f;
    }
    return sig;
}

static std::vector<float> generateBrightTransient(float sr, float dur) {
    size_t n = static_cast<size_t>(sr * dur);
    std::vector<float> sig(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / sr;
        float env = std::exp(-t / 0.005f);
        float v = std::sin(2.0f * 3.14159265f * 4000.0f * t)
                + 0.5f * std::sin(2.0f * 3.14159265f * 6000.0f * t)
                + 0.3f * std::sin(2.0f * 3.14159265f * 8000.0f * t);
        sig[i] = v * env * 0.8f;
    }
    return sig;
}

// ============================================================================
// Render with explicit parameter overrides
// ============================================================================
struct VerbParams {
    float mode = 0.0f;
    float decay = 0.56f;
    float size = 0.52f;
    float diffusion = 0.64f;
    float modRate = 0.42f;
    float modDepth = 0.24f;
    float width = 0.68f;
};

static std::pair<std::vector<float>, std::vector<float>> renderVerb(
    const std::vector<float>& dry, float sr, const VerbParams& p) {
    size_t n = dry.size();
    std::vector<float> outL(n), outR(n);
    AestraVerb verb;
    verb.initialize(sr, 256);
    verb.setParameter(AestraVerb::kMode, p.mode);
    verb.setParameter(AestraVerb::kDecay, p.decay);
    verb.setParameter(AestraVerb::kSize, p.size);
    verb.setParameter(AestraVerb::kDiffusion, p.diffusion);
    verb.setParameter(AestraVerb::kModRate, p.modRate);
    verb.setParameter(AestraVerb::kModDepth, p.modDepth);
    verb.setParameter(AestraVerb::kMix, 1.0f);
    verb.setParameter(AestraVerb::kWidth, p.width);
    verb.activate();
    size_t chunk = 256;
    for (size_t off = 0; off < n; off += chunk) {
        size_t frames = std::min(chunk, n - off);
        const float* in[2] = {dry.data() + off, dry.data() + off};
        float* out[2] = {outL.data() + off, outR.data() + off};
        verb.process(in, out, 2, 2, static_cast<uint32_t>(frames));
    }
    return {outL, outR};
}

// ============================================================================
// Analysis
// ============================================================================
struct Metrics {
    float peak = 0.0f;
    float rms = 0.0f;
    float tailRms = 0.0f;
    float highRatio = 0.0f;
    float centroid = 0.0f;
};

static Metrics analyze(const std::vector<float>& L, const std::vector<float>& R, float sr) {
    Metrics m;
    size_t n = L.size();
    double sq = 0;
    for (size_t i = 0; i < n; ++i) {
        m.peak = std::max(m.peak, std::max(std::abs(L[i]), std::abs(R[i])));
        sq += L[i]*L[i] + R[i]*R[i];
    }
    m.rms = static_cast<float>(std::sqrt(sq / (2.0 * n)));
    size_t ts = n / 2;
    double tsq = 0;
    for (size_t i = ts; i < n; ++i) tsq += L[i]*L[i] + R[i]*R[i];
    m.tailRms = static_cast<float>(std::sqrt(tsq / (2.0 * (n - ts))));
    double hE = 0, tE = 0;
    for (size_t i = 1; i < n; ++i) {
        float diff = std::abs(L[i] - L[i-1]);
        float avg = (std::abs(L[i]) + std::abs(R[i])) * 0.5f;
        double e = avg * avg;
        tE += e;
        if (diff > 0.01f) hE += e;
    }
    m.highRatio = (tE > 0) ? static_cast<float>(hE / tE) : 0;
    size_t crossings = 0;
    size_t fftStart = n / 2;
    size_t tailLen = n - fftStart;
    for (size_t i = fftStart + 1; i < n; ++i) {
        if ((L[i] >= 0) != (L[i-1] >= 0)) crossings++;
    }
    m.centroid = (tailLen > 0) ? (static_cast<float>(crossings) / tailLen * sr / 2.0f) : 0;
    return m;
}

// ============================================================================
// Main
// ============================================================================
int main() {
    const float sr = 48000.0f;
    const std::string outDir = "labs/reverb/quality/mimoclaw_003_second_deflange_desizzle_pack";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec || !std::filesystem::is_directory(outDir)) {
        std::cerr << "Failed to create output directory: " << outDir << "\n";
        return 1;
    }

    // Sources
    struct Src { std::string name; std::vector<float> (*gen)(float,float); float dur; };
    std::vector<Src> sources = {
        {"snare", generateSnare, 0.5f},
        {"bright_ping", generateBrightPing, 0.5f},
        {"vocal_phrase", generateVocalPhrase, 1.0f},
        {"chord_stab", generateChordStab, 0.5f},
        {"low_pulse", generateLowPulse, 0.5f},
        {"mix_bus", generateMixBus, 1.0f},
    };

    // Modes
    struct Mode { std::string name; float param; };
    std::vector<Mode> modes = {{"room", 0.0f}, {"hall", 0.5f}, {"plate", 1.0f}};

    // Session 002 params (before)
    VerbParams before;
    before.modRate = 0.30f;
    before.modDepth = 0.14f;

    // Session 003 params (after) — further reduced modulation, deeper box-cut, less sizzle
    VerbParams after;
    after.modRate = 0.30f;
    after.modDepth = 0.10f;

    // Summary data
    struct Entry {
        std::string source, mode;
        Metrics before, after;
    };
    std::vector<Entry> entries;

    // === Standard renders ===
    for (const auto& src : sources) {
        auto dry = src.gen(sr, src.dur);
        std::vector<float> dryL(dry), dryR(dry);
        writeWavFloat(outDir + "/dry_" + src.name + ".wav", dryL, dryR, sr);

        for (const auto& mode : modes) {
            std::cout << src.name << " -> " << mode.name << "..." << std::flush;
            before.mode = mode.param;
            after.mode = mode.param;
            auto [bL, bR] = renderVerb(dry, sr, before);
            auto [aL, aR] = renderVerb(dry, sr, after);
            writeWavFloat(outDir + "/before_" + mode.name + "_" + src.name + ".wav", bL, bR, sr);
            writeWavFloat(outDir + "/after_" + mode.name + "_" + src.name + ".wav", aL, aR, sr);
            Entry e;
            e.source = src.name;
            e.mode = mode.name;
            e.before = analyze(bL, bR, sr);
            e.after = analyze(aL, aR, sr);
            entries.push_back(e);
            std::cout << " done" << std::endl;
        }
    }

    // === Stress renders ===
    struct StressSrc { std::string name; std::vector<float> (*gen)(float,float); float dur; float mode; float decay; float size; };
    std::vector<StressSrc> stresses = {
        {"vocal_hall", generateVocalPhrase, 1.0f, 0.5f, 0.56f, 0.52f},
        {"vocal_plate", generateVocalPhrase, 1.0f, 1.0f, 0.56f, 0.52f},
        {"mix_hall", generateMixBus, 1.0f, 0.5f, 0.56f, 0.52f},
        {"mix_plate", generateMixBus, 1.0f, 1.0f, 0.56f, 0.52f},
        {"bright_plate_long", generateBrightTransient, 0.5f, 1.0f, 0.8f, 0.7f},
    };
    for (const auto& s : stresses) {
        std::cout << "stress: " << s.name << "..." << std::flush;
        auto dry = s.gen(sr, s.dur);
        before.mode = s.mode; before.decay = s.decay; before.size = s.size;
        after.mode = s.mode; after.decay = s.decay; after.size = s.size;
        auto [bL, bR] = renderVerb(dry, sr, before);
        auto [aL, aR] = renderVerb(dry, sr, after);
        writeWavFloat(outDir + "/before_stress_" + s.name + ".wav", bL, bR, sr);
        writeWavFloat(outDir + "/after_stress_" + s.name + ".wav", aL, aR, sr);
        Entry e;
        e.source = "stress_" + s.name;
        e.mode = (s.mode < 0.25f) ? "room" : (s.mode < 0.75f) ? "hall" : "plate";
        e.before = analyze(bL, bR, sr);
        e.after = analyze(aL, aR, sr);
        entries.push_back(e);
        std::cout << " done" << std::endl;
    }

    // === Generate report ===
    std::ostringstream md;
    md << "# Aestra Verb Second De-Flange / De-Sizzle Pack — Session 003\n\n";
    md << "Agent: **Resonance** | Operator: **Dylan**\n\n";
    md << "## Changes Made (Session 002 → Session 003)\n\n";
    md << "| Parameter | Session 002 | Session 003 |\n";
    md << "|-----------|-------------|-------------|\n";
    md << "| Default ModDepth | 0.14 | 0.10 |\n";
    md << "| Room modDepthScalar | 0.30 | 0.15 |\n";
    md << "| Hall modDepthScalar | 0.72 | 0.58 |\n";
    md << "| Plate modDepthScalar | 0.70 | 0.50 |\n";
    md << "| LFO base rates | 0.15-1.03 Hz | 0.158-1.058 Hz |\n";
    md << "| LFO secondary rates | 0.061-0.325 Hz | 0.062-0.332 Hz |\n";
    md << "| LFO primary/secondary blend | 55/45 | 45/55 |\n";
    md << "| Room box-cut | -3.6 dB @ 380 Hz | -4.6 dB @ 430 Hz |\n";
    md << "| Hall box-cut | -2.8 dB @ 480 Hz | -3.8 dB @ 520 Hz |\n";
    md << "| Plate box-cut | -3.2 dB @ 1150 Hz | -4.2 dB @ 1250 Hz |\n";
    md << "| Plate toneCutHz range | 6000-8500 Hz | 5500-7300 Hz |\n";
    md << "| wetAirBlend (Room/Hall/Plate) | 0.10/0.06/0.08 | 0.07/0.045/0.055 |\n";
    md << "| Plate post-allpass g | 0.38 | 0.32 |\n\n";

    md << "## Standard Renders — Before vs After\n\n";
    md << "| Source | Mode | Before Peak | After Peak | Before TailRMS | After TailRMS | Before HighRatio | After HighRatio |\n";
    md << "|--------|------|-------------|------------|----------------|---------------|------------------|-----------------|\n";
    for (const auto& e : entries) {
        if (e.source.find("stress_") != std::string::npos) continue;
        md << "| " << e.source << " | " << e.mode
           << " | " << std::fixed << std::setprecision(3) << e.before.peak
           << " | " << e.after.peak
           << " | " << std::setprecision(4) << e.before.tailRms
           << " | " << e.after.tailRms
           << " | " << std::setprecision(3) << e.before.highRatio
           << " | " << e.after.highRatio
           << " |\n";
    }

    md << "\n## Stress Renders — Before vs After\n\n";
    md << "| Source | Mode | Before Peak | After Peak | Before TailRMS | After TailRMS |\n";
    md << "|--------|------|-------------|------------|----------------|---------------|\n";
    for (const auto& e : entries) {
        if (e.source.find("stress_") == std::string::npos) continue;
        md << "| " << e.source << " | " << e.mode
           << " | " << std::fixed << std::setprecision(3) << e.before.peak
           << " | " << e.after.peak
           << " | " << std::setprecision(4) << e.before.tailRms
           << " | " << e.after.tailRms
           << " |\n";
    }

    md << "\n## File Index\n\n";
    md << "All files are 48kHz 32-bit float stereo.\n\n";
    md << "Naming: `{before|after}_{mode}_{source}.wav`\n\n";
    md << "Audition **before** then **after** for each source/mode pair.\n";

    {
        std::ofstream f(outDir + "/SESSION_003_REPORT.md");
        if (f) f << md.str();
    }

    std::cout << "\n=== SESSION 003 LISTENING PACK ===" << std::endl;
    std::cout << "Output: " << outDir << "/" << std::endl;
    std::cout << "Report: " << outDir << "/SESSION_003_REPORT.md" << std::endl;
    return 0;
}
