// AestraVerb Listening Pack generator.
//
// Renders a set of dry musical sources through AestraVerb at a musical mix and
// writes equal-loudness (RMS-matched) stereo WAVs plus a KEY, so the reverb can
// be auditioned blind (compare files without level bias). This is an evaluation
// aid for a manual listening session, not an automated pass/fail test.
//
// Output: labs/reverb/listening/*.wav  +  labs/reverb/listening/KEY.md

#include "AestraVerb.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraVerb;

namespace {
constexpr float kPi = 3.14159265358979323846f;

bool writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, float sr) {
    if (l.size() != r.size() || l.empty()) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint32_t n = static_cast<uint32_t>(l.size());
    const uint32_t dataSize = n * 8, fileSize = 36 + dataSize, byteRate = static_cast<uint32_t>(sr) * 8;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(fileSize); f.write("WAVE", 4); f.write("fmt ", 4);
    u32(16); u16(3); u16(2); u32(static_cast<uint32_t>(sr)); u32(byteRate); u16(8); u16(32);
    f.write("data", 4); u32(dataSize);
    for (uint32_t i = 0; i < n; ++i) { f.write(reinterpret_cast<const char*>(&l[i]), 4); f.write(reinterpret_cast<const char*>(&r[i]), 4); }
    return f.good();
}

float rngf(uint32_t& s) { s = s * 1103515245u + 12345u; return static_cast<float>((s >> 16) & 0x7FFF) / 16384.0f - 1.0f; }

// --- Musical dry sources (mono, returned as a single vector) ---
std::vector<float> vocalAhh(float sr, float dur) {
    size_t n = size_t(sr * dur); std::vector<float> s(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float t = i / sr;
        float vib = 1.0f + 0.02f * std::sin(2 * kPi * 5.0f * t);
        float f = 220.0f * vib;
        float env = t < 0.05f ? t / 0.05f : (t > dur - 0.2f ? (dur - t) / 0.2f : 1.0f);
        float v = std::sin(2 * kPi * f * t) + 0.3f * std::sin(2 * kPi * 2 * f * t) + 0.15f * std::sin(2 * kPi * 3 * f * t);
        s[i] = v * env * 0.5f;
    }
    return s;
}
std::vector<float> snare(float sr, float dur) {
    size_t n = size_t(sr * dur); std::vector<float> s(n, 0.0f); uint32_t seed = 54321;
    for (size_t i = 0; i < n; ++i) {
        float t = i / sr;
        s[i] = rngf(seed) * std::exp(-t / 0.015f) * 0.8f + std::sin(2 * kPi * 200 * t) * std::exp(-t / 0.04f) * 0.3f;
    }
    return s;
}
std::vector<float> pianoNote(float sr, float dur) {
    size_t n = size_t(sr * dur); std::vector<float> s(n, 0.0f);
    const float f0 = 261.63f; // C4
    for (size_t i = 0; i < n; ++i) {
        float t = i / sr;
        float env = std::exp(-t / 0.8f) * (1.0f - std::exp(-t / 0.003f));
        float v = std::sin(2 * kPi * f0 * t) + 0.5f * std::sin(2 * kPi * 2 * f0 * t)
                + 0.28f * std::sin(2 * kPi * 3 * f0 * t) + 0.12f * std::sin(2 * kPi * 4 * f0 * t);
        s[i] = v * env * 0.3f;
    }
    return s;
}
std::vector<float> guitarPluck(float sr, float dur) {
    // Karplus-Strong-ish pluck at ~196 Hz (G3).
    size_t n = size_t(sr * dur); std::vector<float> s(n, 0.0f);
    const int p = std::max(2, int(sr / 196.0f));
    std::vector<float> buf(p); uint32_t seed = 13337;
    for (int i = 0; i < p; ++i) buf[i] = rngf(seed);
    int idx = 0;
    for (size_t i = 0; i < n; ++i) {
        float cur = buf[idx];
        int nxt = (idx + 1) % p;
        buf[idx] = 0.5f * (cur + buf[nxt]) * 0.996f;
        s[i] = cur * 0.5f;
        idx = nxt;
    }
    return s;
}
std::vector<float> padChord(float sr, float dur) {
    size_t n = size_t(sr * dur); std::vector<float> s(n, 0.0f);
    const float freqs[] = { 261.63f, 329.63f, 392.00f, 523.25f };
    for (size_t i = 0; i < n; ++i) {
        float t = i / sr;
        float env = t < 0.3f ? t / 0.3f : (t > dur - 0.4f ? (dur - t) / 0.4f : 1.0f);
        float v = 0.0f;
        for (float f : freqs) v += std::sin(2 * kPi * f * t) + 0.2f * std::sin(2 * kPi * 2 * f * t);
        s[i] = v * env * 0.12f;
    }
    return s;
}
std::vector<float> drumMix(float sr, float dur) {
    size_t n = size_t(sr * dur); std::vector<float> s(n, 0.0f); uint32_t seed = 99999;
    auto kick = [&](size_t at) { for (size_t i = 0; i < size_t(sr * 0.12f) && at + i < n; ++i) { float t = i / sr; s[at + i] += std::sin(2 * kPi * (90.0f - 40.0f * t / 0.12f) * t) * std::exp(-t / 0.05f) * 0.7f; } };
    auto sn = [&](size_t at) { for (size_t i = 0; i < size_t(sr * 0.08f) && at + i < n; ++i) { float t = i / sr; s[at + i] += rngf(seed) * std::exp(-t / 0.02f) * 0.5f; } };
    kick(0); kick(size_t(sr * 0.5f)); sn(size_t(sr * 0.25f)); sn(size_t(sr * 0.75f));
    return s;
}

double rms(const std::vector<float>& x) { double a = 0; for (float v : x) a += (double)v * v; return std::sqrt(a / std::max<size_t>(x.size(), 1)); }

} // namespace

int main() {
    const float sr = 48000.0f;
    const std::string dir = "labs/reverb/listening";
    std::error_code ec; std::filesystem::create_directories(dir, ec);

    struct Src { const char* name; std::vector<float>(*gen)(float, float); float dur; };
    const Src sources[] = {
        {"vocal", vocalAhh, 3.0f}, {"snare", snare, 2.5f}, {"piano", pianoNote, 3.5f},
        {"guitar", guitarPluck, 3.5f}, {"pad", padChord, 4.0f}, {"drums", drumMix, 3.0f},
    };
    struct ModeDef { const char* name; AestraVerb::Mode mode; };
    const ModeDef modes[] = {
        {"room", AestraVerb::Mode::Room}, {"hall", AestraVerb::Mode::Hall},
        {"plate", AestraVerb::Mode::Plate}, {"cathedral", AestraVerb::Mode::Cathedral},
        {"chamber", AestraVerb::Mode::Chamber}, {"bright_hall", AestraVerb::Mode::BrightHall},
        {"ambience", AestraVerb::Mode::Ambience}, {"scoring", AestraVerb::Mode::Scoring},
        {"smooth_plate", AestraVerb::Mode::SmoothPlate},
    };

    // Target loudness for equal-loudness comparison (RMS-normalize each output).
    const double targetRms = 0.10;

    std::ofstream key(dir + "/KEY.md");
    key << "# AestraVerb Listening Pack\n\n"
        << "Equal-loudness (RMS-matched to " << targetRms << ") stereo renders for blind A/B.\n"
        << "Musical mix (35% wet), 48 kHz. Dry references included per source.\n\n"
        << "| File | Source | Mode |\n|------|--------|------|\n";

    for (const auto& src : sources) {
        auto dry = src.gen(sr, src.dur);
        // Tail room so the reverb decays fully within the file.
        const size_t n = dry.size() + size_t(sr * 3.0f);
        std::vector<float> inL(n, 0.0f), inR(n, 0.0f);
        for (size_t i = 0; i < dry.size(); ++i) { inL[i] = dry[i]; inR[i] = dry[i]; }

        // Dry reference (RMS-normalized over the source region).
        {
            std::vector<float> dl(dry.begin(), dry.end()), dr(dry.begin(), dry.end());
            double g = targetRms / std::max(rms(dl), 1e-9);
            for (auto& v : dl) v = float(v * g); for (auto& v : dr) v = float(v * g);
            writeWav(dir + "/" + src.name + "_dry.wav", dl, dr, sr);
            key << "| " << src.name << "_dry.wav | " << src.name << " | (dry) |\n";
        }

        for (const auto& m : modes) {
            AestraVerb v; v.initialize(sr, 256);
            v.setParameter(AestraVerb::kMode, AestraVerb::modeParam(m.mode));
            v.setParameter(AestraVerb::kDecay, 0.6f);
            v.setParameter(AestraVerb::kSize, 0.6f);
            v.setParameter(AestraVerb::kDiffusion, 0.8f);
            v.setParameter(AestraVerb::kModRate, 0.5f);
            v.setParameter(AestraVerb::kModDepth, 0.3f);
            v.setParameter(AestraVerb::kWidth, 0.7f);
            v.setParameter(AestraVerb::kMix, 0.35f); // musical wet/dry
            v.activate();
            std::vector<float> outL(n), outR(n);
            const float* in[2] = { inL.data(), inR.data() };
            float* out[2] = { outL.data(), outR.data() };
            const size_t block = 256;
            for (size_t off = 0; off < n; off += block) {
                const size_t f = std::min(block, n - off);
                const float* bi[2] = { inL.data() + off, inR.data() + off };
                float* bo[2] = { outL.data() + off, outR.data() + off };
                v.process(bi, bo, 2, 2, static_cast<uint32_t>(f));
            }
            const double g = targetRms / std::max(rms(outL), 1e-9);
            for (auto& s : outL) s = float(s * g); for (auto& s : outR) s = float(s * g);
            const std::string fn = std::string(src.name) + "_" + m.name + ".wav";
            writeWav(dir + "/" + fn, outL, outR, sr);
            key << "| " << fn << " | " << src.name << " | " << m.name << " |\n";
        }
    }

    std::ofstream(dir + "/README.md")
        << "# AestraVerb Listening Pack\n\nEqual-loudness renders for a manual A/B session. "
           "See KEY.md for the file map. Compare `<source>_dry.wav` against each `<source>_<mode>.wav`, "
           "and modes against each other. Levels are RMS-matched so differences you hear are timbre/space, "
           "not loudness.\n";

    std::printf("Listening pack written to %s/ (%zu sources x %zu modes + dry)\n",
                dir.c_str(), sizeof(sources)/sizeof(sources[0]), sizeof(modes)/sizeof(modes[0]));
    return 0;
}
