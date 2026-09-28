// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SamplerKeepLengthTest
// Keep-length pitch mode: a note plays at its pitch WITHOUT the resampling speed
// change. The sampler's resampler still applies the pitch; a pre-rendered,
// time-stretched copy per semitone restores the source's length.
//   1. prewarmed: exact pitch, and the note decays like the source (not slower/faster);
//   2. Resample mode is unchanged (a -7 note still lasts ~1.5x as long);
//   3. a pitch that isn't rendered yet plays resampled once, then keep-length;
//   4. replacing the sample never plays a copy stretched from the old sample;
//   5. the mode survives save/load, and state without it loads as Resample.
// Drives SamplerPlugin directly; the render service runs on its own thread.

#include "Plugin/PluginHost.h"
#include "Plugin/SamplerPlugin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using Aestra::Audio::Plugins::SamplerPlugin;

constexpr uint32_t kRate = 48000;
constexpr uint32_t kBlock = 256;
constexpr uint8_t kRoot = 60;
constexpr double kPi = 3.14159265358979323846;

int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) ++failures;
}

// A piano-ish one-shot: 8 harmonics of `hz`, exponential decay (half-life ~0.46 s).
std::vector<float> pluck(double hz, double seconds = 2.0) {
    std::vector<float> s(static_cast<size_t>(seconds * kRate));
    for (size_t i = 0; i < s.size(); ++i) {
        double v = 0.0;
        for (int k = 1; k <= 8; ++k) v += std::sin(2.0 * kPi * hz * k * static_cast<double>(i) / kRate) / k;
        s[i] = static_cast<float>(0.25 * v * std::exp(-1.5 * static_cast<double>(i) / kRate));
    }
    return s;
}

std::unique_ptr<SamplerPlugin> makeSampler(const std::vector<float>& mono) {
    auto s = std::make_unique<SamplerPlugin>();
    if (!s->initialize(kRate, kBlock) || !s->loadSampleData("pluck", mono, kRate, 1)) {
        std::cerr << "setup failed\n";
        std::exit(2);
    }
    s->activate();
    s->setEnvelope(0.001f, 0.001f, 1.0f, 0.010f); // the sample's own decay is what we measure
    return s;
}

// One note, never released (one-shot plays to its end). Left channel.
std::vector<float> play(SamplerPlugin& s, uint8_t note, double seconds = 4.0) {
    const uint32_t frames = static_cast<uint32_t>(seconds * kRate);
    std::vector<float> out(frames), l(kBlock), r(kBlock);
    float* ch[2] = {l.data(), r.data()};
    for (uint32_t start = 0; start < frames; start += kBlock) {
        const uint32_t n = std::min(kBlock, frames - start);
        Aestra::Audio::MidiBuffer midi;
        if (start == 0) {
            const uint8_t on[3] = {0x90, note, 110};
            midi.addEvent(0, on, 3);
        }
        s.process(nullptr, ch, 0, 2, n, &midi, nullptr);
        std::copy(l.begin(), l.begin() + n, out.begin() + start);
    }
    s.requestHardResetVoices();
    return out;
}

double peakHz(const std::vector<float>& x, double want) {
    double best = -1.0, hz = 0.0;
    const uint32_t a = static_cast<uint32_t>(0.1 * kRate), b = static_cast<uint32_t>(0.5 * kRate);
    for (double f = want * 0.97; f < want * 1.03; f += 0.02) {
        const double w = 2.0 * kPi * f / kRate;
        double s1 = 0.0, s2 = 0.0;
        for (uint32_t i = a; i < b; ++i) {
            const double s0 = x[i] + 2.0 * std::cos(w) * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double p = s1 * s1 + s2 * s2 - 2.0 * std::cos(w) * s1 * s2;
        if (p > best) {
            best = p;
            hz = f;
        }
    }
    return hz;
}

// Seconds for the level to fall to half of its 50-70 ms level.
double halfLife(const std::vector<float>& x) {
    const auto windowPeak = [&](size_t a) {
        float m = 0.0f;
        for (size_t i = a; i < a + 960 && i < x.size(); ++i) m = std::max(m, std::abs(x[i]));
        return m;
    };
    const size_t ref = static_cast<size_t>(0.05 * kRate);
    const float level = windowPeak(ref);
    for (size_t a = ref; a + 960 < x.size(); a += 480)
        if (windowPeak(a) < 0.5f * level) return static_cast<double>(a - ref) / kRate;
    return -1.0;
}

double cents(double got, double want) { return 1200.0 * std::log2(got / want); }

// Level of one frequency over 0.1-0.5 s (Goertzel).
double levelAt(const std::vector<float>& x, double f) {
    const uint32_t a = static_cast<uint32_t>(0.1 * kRate), b = static_cast<uint32_t>(0.5 * kRate);
    const double w = 2.0 * kPi * f / kRate;
    double s1 = 0.0, s2 = 0.0;
    for (uint32_t i = a; i < b; ++i) {
        const double s0 = x[i] + 2.0 * std::cos(w) * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - 2.0 * std::cos(w) * s1 * s2));
}

} // namespace

int main() {
    const double hz = 220.0;
    const auto source = pluck(hz);
    const double sourceHalfLife = halfLife(source);
    std::cout << "source half-life " << sourceHalfLife << " s\n";

    std::cout << "1. keep-length, prewarmed: exact pitch, the source's decay\n";
    {
        auto s = makeSampler(source);
        s->setPitchMode(SamplerPlugin::PitchMode::KeepLength);
        check(s->prewarmKeepLength({kRoot - 7, kRoot + 5}, true), "prewarm finishes");
        for (int shift : {-7, 5}) {
            const auto x = play(*s, static_cast<uint8_t>(kRoot + shift));
            const double want = hz * std::pow(2.0, shift / 12.0);
            const double c = cents(peakHz(x, want), want);
            const double hl = halfLife(x);
            check(std::abs(c) < 1.0, "shift " + std::to_string(shift) + ": " + std::to_string(c) + " cents");
            check(std::abs(hl / sourceHalfLife - 1.0) < 0.06,
                  "shift " + std::to_string(shift) + ": decays like the source (" + std::to_string(hl) + " s vs " +
                      std::to_string(sourceHalfLife) + " s)");
        }
    }

    std::cout << "2. Resample mode is unchanged\n";
    {
        auto s = makeSampler(source);
        const double hl = halfLife(play(*s, kRoot - 7));
        const double expect = sourceHalfLife * std::pow(2.0, 7.0 / 12.0);
        check(std::abs(hl / expect - 1.0) < 0.06,
              "-7 resampled lasts ~1.5x (" + std::to_string(hl) + " s, expected " + std::to_string(expect) + ")");
    }

    std::cout << "3. a pitch not rendered yet plays resampled once, then keep-length\n";
    {
        auto s = makeSampler(source);
        s->setPitchMode(SamplerPlugin::PitchMode::KeepLength);
        const double first = halfLife(play(*s, kRoot - 5));
        check(first > sourceHalfLife * 1.2, "first hit falls back to resampling (" + std::to_string(first) + " s)");
        check(s->prewarmKeepLength({kRoot - 5}, true), "the first hit queued the render; it finishes");
        const double second = halfLife(play(*s, kRoot - 5));
        check(std::abs(second / sourceHalfLife - 1.0) < 0.06,
              "later hits keep the length (" + std::to_string(second) + " s)");
    }

    std::cout << "4. replacing the sample never plays a copy of the old one\n";
    {
        auto s = makeSampler(source);
        s->setPitchMode(SamplerPlugin::PitchMode::KeepLength);
        check(s->prewarmKeepLength({kRoot - 7}, true), "old sample's -7 rendered");
        const double newHz = 330.0;
        if (!s->loadSampleData("pluck2", pluck(newHz), kRate, 1)) std::exit(2);
        const auto x = play(*s, kRoot - 7); // immediately: the old render must be gone
        const double want = newHz * std::pow(2.0, -7.0 / 12.0);
        const double staleHz = hz * std::pow(2.0, -7.0 / 12.0);
        check(std::abs(cents(peakHz(x, want), want)) < 1.0, "plays the NEW sample's pitch");
        check(levelAt(x, staleHz) < 0.05 * levelAt(x, want),
              "no energy at the old sample's pitch (" + std::to_string(levelAt(x, staleHz) / levelAt(x, want)) +
                  " of the new pitch's level)");
    }

    std::cout << "5. save / load\n";
    {
        auto a = makeSampler(source);
        a->setPitchMode(SamplerPlugin::PitchMode::KeepLength);
        const auto state = a->saveState();
        auto b = makeSampler(source);
        check(b->loadState(state) && b->getPitchMode() == SamplerPlugin::PitchMode::KeepLength,
              "keep-length survives save/load");
        std::string json(state.begin(), state.end());
        const auto at = json.find("\"pitchMode\"");
        check(at != std::string::npos, "state carries pitchMode");
        if (at != std::string::npos) {
            json.replace(at, 11, "\"unusedKey\""); // an older project: no pitchMode field
            auto c = makeSampler(source);
            c->setPitchMode(SamplerPlugin::PitchMode::KeepLength);
            check(c->loadState(std::vector<uint8_t>(json.begin(), json.end())) &&
                      c->getPitchMode() == SamplerPlugin::PitchMode::Resample,
                  "state without pitchMode loads as Resample");
        }
    }

    std::cout << (failures == 0 ? "[PASS] SamplerKeepLengthTest\n"
                                : "[FAIL] SamplerKeepLengthTest: " + std::to_string(failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
