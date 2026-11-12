// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AudioKeyDetectTest — V83-KEY1 (FD-76): the harmony service guesses the key of audio.
//   1. known musical answers: chord loops in a major and a minor key, mono and stereo, 44.1 and 48 kHz;
//   2. a sample tuned off A = 440 Hz still lands on the right key and reports the offset;
//   3. a single held note (an 808) names its own pitch class;
//   4. noise and silence are not called a key: low pitchedness, or no key at all.

#include "Harmony/AudioKeyDetect.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio::Harmony;

namespace {
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << what << "\n";
    if (!cond)
        ++g_failures;
}

constexpr double kPi = 3.14159265358979323846;

struct Chord {
    std::vector<int> midi;
    double seconds;
};

/** Each note is a decaying stack of six harmonics, like a plucked or keyed instrument. */
std::vector<float> render(const std::vector<Chord>& chords, uint32_t rate, uint32_t channels, double cents = 0.0) {
    std::vector<float> out;
    for (const Chord& c : chords) {
        const size_t n = static_cast<size_t>(c.seconds * rate);
        const size_t base = out.size();
        out.resize(base + n * channels, 0.0f);
        for (int note : c.midi) {
            const double hz = 440.0 * std::pow(2.0, (note - 69.0 + cents / 100.0) / 12.0);
            for (size_t i = 0; i < n; ++i) {
                const double t = static_cast<double>(i) / rate;
                double v = 0;
                for (int h = 1; h <= 6; ++h)
                    v += std::sin(2.0 * kPi * hz * h * t) / h;
                v *= 0.08 * std::exp(-1.5 * t);
                for (uint32_t ch = 0; ch < channels; ++ch)
                    out[base + i * channels + ch] += static_cast<float>(v);
            }
        }
    }
    return out;
}

std::vector<float> noise(double seconds, uint32_t rate) {
    std::vector<float> out(static_cast<size_t>(seconds * rate));
    uint32_t s = 0x1234567u;
    for (float& v : out) {
        s = s * 1664525u + 1013904223u;
        v = (static_cast<float>(s >> 8) / 8388608.0f - 1.0f) * 0.3f;
    }
    return out;
}

AudioKeyResult detect(const std::vector<float>& a, uint32_t rate, uint32_t channels) {
    return detectAudioKey(a.data(), a.size() / channels, channels, rate);
}
std::string describe(const AudioKeyResult& r) {
    return r.key.name() + ", confidence " + std::to_string(r.key.confidence) + ", pitched " +
           std::to_string(r.pitchedness) + ", tuning " + std::to_string(r.tuningCents);
}

// D major: D - G - A - D, twice. A bass note under a triad.
const std::vector<Chord> kDMajor = {{{38, 62, 66, 69}, 1.5}, {{43, 62, 67, 71}, 1.5}, {{45, 61, 64, 69}, 1.5},
                                    {{38, 62, 66, 69}, 1.5}, {{38, 62, 66, 69}, 1.5}, {{43, 62, 67, 71}, 1.5},
                                    {{45, 61, 64, 69}, 1.5}, {{38, 62, 66, 69}, 1.5}};
// A minor: Am - Dm - Em - Am, twice.
const std::vector<Chord> kAMinor = {{{45, 60, 64, 69}, 1.5}, {{38, 62, 65, 69}, 1.5}, {{40, 59, 64, 67}, 1.5},
                                    {{45, 60, 64, 69}, 1.5}, {{45, 60, 64, 69}, 1.5}, {{38, 62, 65, 69}, 1.5},
                                    {{40, 59, 64, 67}, 1.5}, {{45, 60, 64, 69}, 1.5}};
} // namespace

int main() {
    std::cout << "[1] known answers\n";
    {
        const AudioKeyResult r = detect(render(kDMajor, 44100, 1), 44100, 1);
        check(r.key.tonic == 2 && r.key.mode == KeyMode::Major,
              "D-G-A-D mono 44.1 kHz is D major (" + describe(r) + ")");
        check(r.pitched(), "a chord loop counts as pitched");
        check(std::fabs(r.tuningCents) < 10.0, "tuned to A = 440 reports about 0 cents");
        double total = 0;
        for (double v : r.chroma)
            total += v;
        check(std::fabs(total - 1.0) < 1e-6, "the chroma sums to 1");
    }
    {
        const AudioKeyResult r = detect(render(kAMinor, 48000, 2), 48000, 2);
        check(r.key.tonic == 9 && r.key.mode == KeyMode::Minor,
              "Am-Dm-Em-Am stereo 48 kHz is A minor (" + describe(r) + ")");
        check(r.pitched(), "the minor loop counts as pitched");
    }
    {
        // The same loop every semitone up: the answer must move with it, not stick to one favourite key.
        int right = 0;
        for (int shift = 0; shift < 12; ++shift) {
            std::vector<Chord> moved = kAMinor;
            for (Chord& c : moved)
                for (int& m : c.midi)
                    m += shift;
            const AudioKeyResult r = detect(render(moved, 22050, 1), 22050, 1);
            if (r.key.tonic == (9 + shift) % 12 && r.key.mode == KeyMode::Minor)
                ++right;
        }
        check(right == 12, "the minor loop is found in all twelve keys (" + std::to_string(right) + "/12)");
    }

    std::cout << "[2] off-pitch samples\n";
    {
        const AudioKeyResult r = detect(render(kDMajor, 44100, 1, 35.0), 44100, 1);
        check(r.key.tonic == 2 && r.key.mode == KeyMode::Major,
              "35 cents sharp is still D major (" + describe(r) + ")");
        check(r.tuningCents > 20.0 && r.tuningCents < 50.0, "and reports that it is sharp");
    }
    {
        const AudioKeyResult r = detect(render(kDMajor, 44100, 1, -35.0), 44100, 1);
        check(r.key.tonic == 2 && r.key.mode == KeyMode::Major, "35 cents flat is still D major (" + describe(r) + ")");
        check(r.tuningCents < -20.0 && r.tuningCents > -50.0, "and reports that it is flat");
    }

    std::cout << "[3] one note\n";
    {
        const AudioKeyResult r = detect(render({{{41}, 0.6}}, 44100, 1), 44100, 1); // F2, an 808-length hit
        check(r.key.tonic == 5, "a short F2 note names F (" + describe(r) + ")");
        check(r.pitched(), "a held note counts as pitched");
    }

    std::cout << "[4] not a key\n";
    {
        const AudioKeyResult r = detect(noise(6.0, 44100), 44100, 1);
        check(!r.pitched(), "white noise is not shown as a key (" + describe(r) + ")");
    }
    {
        const std::vector<float> silent(44100 * 3, 0.0f);
        const AudioKeyResult r = detect(silent, 44100, 1);
        check(!r.key.valid() && !r.pitched(), "silence has no key");
    }
    {
        const std::vector<float> tiny(2000, 0.1f);
        const AudioKeyResult r = detect(tiny, 44100, 1);
        check(!r.key.valid(), "45 ms of audio has no key");
        const AudioKeyResult none = detectAudioKey(nullptr, 0, 2, 44100);
        check(!none.key.valid(), "no audio has no key");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}
