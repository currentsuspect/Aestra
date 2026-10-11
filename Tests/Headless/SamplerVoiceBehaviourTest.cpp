// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SamplerVoiceBehaviourTest
// How the built-in sampler behaves when a producer layers one-shots:
//   - pitching: a note plays the sample at the right pitch, including a
//     44.1 kHz file on a 48 kHz engine, and a chord keeps every note's pitch;
//   - note ownership: each note-off ends exactly one note, so overlapping notes
//     of the same pitch (every drum hit sits on the root) don't cut each other,
//     in poly, cut-self and mono modes;
//   - a "square" envelope (no attack, full sustain, no release) stops the sound
//     where the note ends: no tail.
// Drives SamplerPlugin directly: no engine, no files, no devices.

#include "Plugin/PluginHost.h"
#include "Plugin/SamplerPlugin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using Aestra::Audio::Plugins::SamplerPlugin;

constexpr uint32_t kEngineRate = 48000;
constexpr uint32_t kBlockSize = 256;
constexpr uint8_t kNoteOn = 0x90;
constexpr uint8_t kNoteOff = 0x80;
constexpr uint8_t kRoot = 60;
constexpr uint8_t kVelocity = 110;
constexpr double kPi = 3.14159265358979323846;
constexpr float kAudible = 1.0e-3f;
constexpr float kSilence = 1.0e-4f;

int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) ++failures;
}

std::vector<float> sine(double seconds, double hz, uint32_t rate) {
    std::vector<float> s(static_cast<size_t>(seconds * rate));
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = 0.5f * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
    return s;
}

struct Midi {
    uint32_t frame;
    uint8_t status;
    uint8_t note;
    uint32_t id = 0; // the scheduler's note id (0 = live MIDI, no id)
};

uint32_t ms(double t) { return static_cast<uint32_t>(t * kEngineRate / 1000.0); }

// Left channel, one sample per output frame.
std::vector<float> render(SamplerPlugin& s, uint32_t frames, const std::vector<Midi>& events) {
    std::vector<float> out(frames, 0.0f), l(kBlockSize), r(kBlockSize);
    float* ch[2] = {l.data(), r.data()};
    for (uint32_t start = 0; start < frames; start += kBlockSize) {
        const uint32_t n = std::min(kBlockSize, frames - start);
        Aestra::Audio::MidiBuffer midi;
        for (const auto& e : events) {
            if (e.frame >= start && e.frame < start + n) {
                const uint8_t d[3] = {e.status, e.note, static_cast<uint8_t>(e.status == kNoteOn ? kVelocity : 0)};
                midi.addEvent(e.frame - start, d, 3, e.id);
            }
        }
        s.process(nullptr, ch, 0, 2, n, &midi, nullptr);
        std::copy(l.begin(), l.begin() + n, out.begin() + start);
    }
    return out;
}

float peak(const std::vector<float>& x, uint32_t from, uint32_t to) {
    float p = 0.0f;
    for (uint32_t i = from; i < std::min<uint32_t>(to, static_cast<uint32_t>(x.size())); ++i)
        p = std::max(p, std::abs(x[i]));
    return p;
}

// Frequency of a clean tone from its rising zero crossings.
double zeroCrossingHz(const std::vector<float>& x, uint32_t from, uint32_t to) {
    int first = -1, last = -1, count = 0;
    for (uint32_t i = from + 1; i < to && i < x.size(); ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (first < 0) first = static_cast<int>(i);
            last = static_cast<int>(i);
            ++count;
        }
    }
    return count > 1 ? (count - 1) * static_cast<double>(kEngineRate) / (last - first) : 0.0;
}

// Magnitude of one frequency (Goertzel), normalised to a unit sine's amplitude.
double toneLevel(const std::vector<float>& x, uint32_t from, uint32_t to, double hz) {
    const double w = 2.0 * kPi * hz / kEngineRate;
    double s1 = 0.0, s2 = 0.0;
    for (uint32_t i = from; i < to; ++i) {
        const double s0 = x[i] + 2.0 * std::cos(w) * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - 2.0 * std::cos(w) * s1 * s2;
    return 2.0 * std::sqrt(std::max(0.0, power)) / (to - from);
}

// Last frame above the audible threshold.
uint32_t lastAudibleFrame(const std::vector<float>& x) {
    for (uint32_t i = static_cast<uint32_t>(x.size()); i-- > 0;)
        if (std::abs(x[i]) > kAudible) return i;
    return 0;
}

std::unique_ptr<SamplerPlugin> makeSampler(std::vector<float> mono, uint32_t sourceRate) {
    auto s = std::make_unique<SamplerPlugin>();
    if (!s->initialize(kEngineRate, kBlockSize) || !s->loadSampleData("tone", std::move(mono), sourceRate, 1)) {
        std::cerr << "setup failed\n";
        std::exit(2);
    }
    s->activate();
    s->setEnvelope(0.001f, 0.001f, 1.0f, 0.010f); // hold at full level, 10 ms release
    return s;
}

// A holds 0-2 s, B (same pitch) holds 1-3 s: B must sound until its own note-off.
// Scheduled notes carry ids (A=1, B=2); `live` sends none, like a MIDI keyboard.
void sameNoteOverlap(SamplerPlugin& s, const char* mode, bool live = false) {
    const uint32_t a = live ? 0 : 1, b = live ? 0 : 2;
    const auto x = render(s, ms(3500), {{0, kNoteOn, kRoot, a}, {ms(1000), kNoteOn, kRoot, b},
                                        {ms(2000), kNoteOff, kRoot, a}, {ms(3000), kNoteOff, kRoot, b}});
    check(peak(x, ms(1100), ms(1900)) > kAudible, std::string(mode) + ": precondition, B sounds once started");
    check(peak(x, ms(2100), ms(2900)) > kAudible,
          std::string(mode) + ": A's note-off leaves B sounding (B is held until 3 s); got peak " +
              std::to_string(peak(x, ms(2100), ms(2900))));
    check(peak(x, ms(3100), ms(3500)) < kSilence, std::string(mode) + ": B stops at its own note-off");
}

} // namespace

int main() {
    const double hz = 220.0;
    const double semitone = std::pow(2.0, 1.0 / 12.0);

    std::cout << "1. pitch: a 44.1 kHz 220 Hz one-shot (2 s) on a 48 kHz engine\n";
    for (int shift : {0, 12, -12, 7}) {
        auto s = makeSampler(sine(2.0, hz, 44100), 44100);
        const auto x = render(*s, ms(5000), {{0, kNoteOn, static_cast<uint8_t>(kRoot + shift)}});
        const double expectHz = hz * std::pow(semitone, shift);
        const double gotHz = zeroCrossingHz(x, ms(50), ms(450));
        const double expectSeconds = 2.0 / std::pow(semitone, shift); // resampling: pitch and length move together
        const double gotSeconds = lastAudibleFrame(x) / static_cast<double>(kEngineRate);
        check(std::abs(gotHz / expectHz - 1.0) < 0.005,
              "shift " + std::to_string(shift) + ": " + std::to_string(gotHz) + " Hz, expected " + std::to_string(expectHz));
        check(std::abs(gotSeconds / expectSeconds - 1.0) < 0.02,
              "shift " + std::to_string(shift) + ": lasts " + std::to_string(gotSeconds) + " s, expected " +
                  std::to_string(expectSeconds) + " (varispeed)");
    }

    std::cout << "2. a chord from one one-shot keeps every note's pitch\n";
    {
        auto s = makeSampler(sine(2.0, hz, 44100), 44100);
        const auto x = render(*s, ms(1000), {{0, kNoteOn, 60}, {0, kNoteOn, 64}, {0, kNoteOn, 67}});
        const double c = toneLevel(x, ms(100), ms(900), hz);
        const double e = toneLevel(x, ms(100), ms(900), hz * std::pow(semitone, 4));
        const double g = toneLevel(x, ms(100), ms(900), hz * std::pow(semitone, 7));
        const double slowed = toneLevel(x, ms(100), ms(900), hz * 44100.0 / 48000.0);
        std::cout << "     levels: C " << c << "  E " << e << "  G " << g << "  (C played 8% slow: " << slowed << ")\n";
        const double loudest = std::max({c, e, g});
        const double quietest = std::min({c, e, g});
        check(quietest > 0.5 * loudest && quietest > 1.0e-3, "all three chord notes present at their own pitch");
        check(slowed < 0.05 * quietest, "no energy at the 44.1-as-48 kHz 'slowed' pitch");
    }

    std::cout << "3. overlapping notes of the same pitch\n";
    {
        auto s = makeSampler(sine(4.0, hz, kEngineRate), kEngineRate);
        sameNoteOverlap(*s, "poly");
    }
    {
        auto s = makeSampler(sine(4.0, hz, kEngineRate), kEngineRate);
        s->setCutSelfMode(true);
        sameNoteOverlap(*s, "cut-self");
    }
    {
        auto s = makeSampler(sine(4.0, hz, kEngineRate), kEngineRate);
        s->setMonoMode(true);
        sameNoteOverlap(*s, "mono");
    }
    {
        auto s = makeSampler(sine(4.0, hz, kEngineRate), kEngineRate);
        s->setMaxVoices(1); // B has to steal A's voice
        sameNoteOverlap(*s, "poly, voice stolen");
    }

    {
        auto s = makeSampler(sine(4.0, hz, kEngineRate), kEngineRate);
        sameNoteOverlap(*s, "poly, live MIDI (no ids)", true);
    }

    std::cout << "3b. note identity edge cases\n";
    {   // Nested under cut-self: A 0-3 s, B 1-2 s. B chokes A; B's own note-off ends B at 2 s.
        auto s = makeSampler(sine(4.0, hz, kEngineRate), kEngineRate);
        s->setCutSelfMode(true);
        const auto x = render(*s, ms(3500), {{0, kNoteOn, kRoot, 1}, {ms(1000), kNoteOn, kRoot, 2},
                                             {ms(2000), kNoteOff, kRoot, 2}, {ms(3000), kNoteOff, kRoot, 1}});
        check(peak(x, ms(1100), ms(1900)) > kAudible, "cut-self nested: B sounds");
        check(peak(x, ms(2100), ms(3500)) < kSilence,
              "cut-self nested: B ends at its own note-off (2 s), not A's (3 s)");
    }
    {   // The sample runs out while its note is still held: A (0-1.5 s) ends at 0.8 s; B starts at
        // 1.0 s. A's late note-off at 1.5 s must not end B (held to 2.5 s; its sample lasts to 1.8 s).
        auto s = makeSampler(sine(0.8, hz, kEngineRate), kEngineRate);
        const auto x = render(*s, ms(2600), {{0, kNoteOn, kRoot, 1}, {ms(1000), kNoteOn, kRoot, 2},
                                             {ms(1500), kNoteOff, kRoot, 1}, {ms(2500), kNoteOff, kRoot, 2}});
        check(peak(x, ms(1550), ms(1750)) > kAudible,
              "expired note's late note-off leaves the newer same-pitch note sounding (peak " +
                  std::to_string(peak(x, ms(1550), ms(1750))) + ")");
    }
    {   // An 808 slide never sends the old note's note-off. A later note on that pitch must still
        // stop at its own note-off: nothing left over from the slide may swallow it.
        auto s = makeSampler(sine(4.0, hz, kEngineRate), kEngineRate);
        s->setMonoMode(true);
        const auto x = render(*s, ms(3500), {{0, kNoteOn, kRoot, 1},                 // no note-off (slide)
                                             {ms(500), kNoteOn, kRoot + 2, 2},       // slides into D
                                             {ms(1500), kNoteOff, kRoot + 2, 2},
                                             {ms(2000), kNoteOn, kRoot, 3},          // C again, later
                                             {ms(2500), kNoteOff, kRoot, 3}});
        check(peak(x, ms(2100), ms(2400)) > kAudible, "after a slide: the later C sounds");
        check(peak(x, ms(2600), ms(3500)) < kSilence, "after a slide: the later C stops at its own note-off");
    }

    std::cout << "4. a square envelope stops the sound where the note ends\n";
    for (const char* mode : {"poly", "cut-self", "mono"}) {
        auto s = makeSampler(sine(2.0, hz, kEngineRate), kEngineRate);
        s->setEnvelope(0.0f, 0.0f, 1.0f, 0.0f);
        if (std::string(mode) == "cut-self") s->setCutSelfMode(true);
        if (std::string(mode) == "mono") s->setMonoMode(true);
        const auto x = render(*s, ms(1500), {{0, kNoteOn, kRoot}, {ms(500), kNoteOff, kRoot}});
        check(peak(x, ms(100), ms(490)) > kAudible, std::string(mode) + ": note sounds while held");
        check(peak(x, ms(505), ms(1500)) < kSilence,
              std::string(mode) + ": no tail after the note ends (peak after 505 ms " +
                  std::to_string(peak(x, ms(505), ms(1500))) + ")");
    }

    std::cout << (failures == 0 ? "[PASS] SamplerVoiceBehaviourTest\n"
                                : "[FAIL] SamplerVoiceBehaviourTest: " + std::to_string(failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
