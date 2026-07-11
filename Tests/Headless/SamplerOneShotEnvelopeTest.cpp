// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SamplerOneShotEnvelopeTest
// Regression for #452: one-shot samples shorter than the configured release
// time rendered pure silence, because the automatic end-of-sample release
// fired at trigger time and captured releaseGain from a still-zero attack.
// Drives the built-in SamplerPlugin directly (no engine, no files, no
// devices) at 48 kHz with the DEFAULT envelope: attack 10 ms, decay 100 ms,
// sustain 0.8, release 300 ms.

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

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockSize = 256;
constexpr uint8_t kNoteOn = 0x90;
constexpr uint8_t kNoteOff = 0x80;
constexpr uint8_t kRootNote = 60;
constexpr uint8_t kVelocity = 110; // clearly audible; sampler squares velocity

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

std::vector<float> makeSineMono(double seconds, double freqHz = 220.0, float amp = 0.5f) {
    const uint32_t frames = static_cast<uint32_t>(seconds * kSampleRate);
    std::vector<float> data(frames, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        data[i] = amp * static_cast<float>(
                            std::sin(2.0 * 3.14159265358979 * freqHz * (static_cast<double>(i) / kSampleRate)));
    }
    return data;
}

struct MidiAt {
    uint32_t frame; // global output frame time
    uint8_t status;
    uint8_t note;
    uint8_t velocity;
};

struct Rendered {
    // max(|L|,|R|) per output frame — windowed analysis never depends on
    // where block boundaries happen to fall.
    std::vector<float> envelope;
    bool hasInvalid = false;
};

Rendered render(Aestra::Audio::Plugins::SamplerPlugin& sampler, uint32_t totalFrames,
                const std::vector<MidiAt>& events) {
    Rendered out;
    out.envelope.resize(totalFrames, 0.0f);
    std::vector<float> left(kBlockSize), right(kBlockSize);
    float* channels[2] = {left.data(), right.data()};

    for (uint32_t start = 0; start < totalFrames; start += kBlockSize) {
        const uint32_t frames = std::min(kBlockSize, totalFrames - start);
        Aestra::Audio::MidiBuffer midi;
        for (const auto& e : events) {
            if (e.frame >= start && e.frame < start + frames) {
                const uint8_t data[3] = {e.status, e.note, e.velocity};
                midi.addEvent(e.frame - start, data, 3);
            }
        }
        sampler.process(nullptr, channels, 0, 2, frames, &midi, nullptr);
        for (uint32_t i = 0; i < frames; ++i) {
            if (!std::isfinite(left[i]) || !std::isfinite(right[i])) {
                out.hasInvalid = true;
            }
            out.envelope[start + i] = std::max(std::abs(left[i]), std::abs(right[i]));
        }
    }
    return out;
}

uint32_t ms(double milliseconds) {
    return static_cast<uint32_t>(milliseconds * kSampleRate / 1000.0);
}

float peakIn(const Rendered& r, uint32_t fromFrame, uint32_t toFrame) {
    float peak = 0.0f;
    toFrame = std::min<uint32_t>(toFrame, static_cast<uint32_t>(r.envelope.size()));
    for (uint32_t i = fromFrame; i < toFrame; ++i) {
        peak = std::max(peak, r.envelope[i]);
    }
    return peak;
}

float rmsIn(const Rendered& r, uint32_t fromFrame, uint32_t toFrame) {
    toFrame = std::min<uint32_t>(toFrame, static_cast<uint32_t>(r.envelope.size()));
    if (toFrame <= fromFrame) {
        return 0.0f;
    }
    double acc = 0.0;
    for (uint32_t i = fromFrame; i < toFrame; ++i) {
        acc += static_cast<double>(r.envelope[i]) * r.envelope[i];
    }
    return static_cast<float>(std::sqrt(acc / (toFrame - fromFrame)));
}

std::unique_ptr<Aestra::Audio::Plugins::SamplerPlugin> makeSampler(double sampleSeconds, const char* name) {
    auto sampler = std::make_unique<Aestra::Audio::Plugins::SamplerPlugin>();
    require(sampler->initialize(kSampleRate, kBlockSize), std::string(name) + ": initialize failed");
    sampler->activate();
    // Default envelope deliberately untouched: A 10 ms / D 100 ms / S 0.8 / R 300 ms.
    require(sampler->loadSampleData(name, makeSineMono(sampleSeconds), kSampleRate, 1),
            std::string(name) + ": loadSampleData failed");
    return sampler;
}

constexpr float kAudible = 1.0e-3f; // meaningful signal, far above numeric noise
constexpr float kSilence = 1.0e-4f; // "returned to silence"

} // namespace

int main() {
    // ---------------- 1. Primary regression (#452): 100 ms one-shot, default
    // envelope, loop disabled — must be audible, then return to silence.
    {
        auto sampler = makeSampler(0.100, "oneshot-100ms");
        const auto r = render(*sampler, ms(300.0), {{0, kNoteOn, kRootNote, kVelocity}});
        const float peak = peakIn(r, 0, ms(100.0));
        const float rms = rmsIn(r, 0, ms(100.0));
        std::cout << "100ms one-shot: peak=" << peak << " rms=" << rms << "\n";
        require(!r.hasInvalid, "100ms: output contains NaN/Inf");
        require(peak > kAudible, "100ms one-shot with default envelope is silent (#452 regression)");
        require(rms > 1.0e-4f, "100ms one-shot has no integrated energy (#452 regression)");
        require(peakIn(r, ms(150.0), ms(300.0)) < kSilence, "100ms: output did not return to silence after sample end");
    }

    // ---------------- 1b. Same contract through the mono voice path.
    {
        auto sampler = makeSampler(0.100, "oneshot-100ms-mono");
        sampler->setMonoMode(true);
        const auto r = render(*sampler, ms(300.0), {{0, kNoteOn, kRootNote, kVelocity}});
        require(!r.hasInvalid, "mono: output contains NaN/Inf");
        require(peakIn(r, 0, ms(100.0)) > kAudible, "mono-mode 100ms one-shot is silent");
        require(peakIn(r, ms(150.0), ms(300.0)) < kSilence, "mono: no return to silence");
    }

    // ---------------- 2. Very short transient (5 ms — shorter than the 10 ms
    // default attack): must not be identically silent, must stay bounded, and
    // the voice must terminate.
    {
        auto sampler = makeSampler(0.005, "transient-5ms");
        const auto r = render(*sampler, ms(100.0), {{0, kNoteOn, kRootNote, kVelocity}});
        const float peak = peakIn(r, 0, ms(10.0));
        std::cout << "5ms transient: peak=" << peak << "\n";
        require(!r.hasInvalid, "5ms: output contains NaN/Inf");
        require(peak > 1.0e-4f, "5ms transient is identically silent");
        require(peak < 1.0f, "5ms transient exceeds unity bound");
        require(peakIn(r, ms(50.0), ms(100.0)) < kSilence, "5ms: voice did not terminate to silence");
    }

    // ---------------- 3. Longer one-shot (750 ms > 300 ms release): body stays
    // audible, only the final release window fades, ends in silence.
    {
        auto sampler = makeSampler(0.750, "oneshot-750ms");
        const auto r = render(*sampler, ms(1000.0), {{0, kNoteOn, kRootNote, kVelocity}});
        const float body = peakIn(r, ms(300.0), ms(400.0));
        const float tail = peakIn(r, ms(700.0), ms(750.0));
        require(!r.hasInvalid, "750ms: output contains NaN/Inf");
        require(body > kAudible, "750ms one-shot body is not audible");
        // Not faded wholesale: mid-body must hold a healthy fraction of the peak.
        require(peakIn(r, ms(400.0), ms(450.0)) > 0.3f * peakIn(r, 0, ms(750.0)),
                "750ms: body was faded by the automatic tail");
        // Final region decreases toward silence.
        require(tail < 0.5f * body, "750ms: no fade in the final release region");
        require(peakIn(r, ms(800.0), ms(1000.0)) < kSilence, "750ms: no silence after sample end");
    }

    // ---------------- 4. Playback-rate correctness: +12 semitones (rate 2.0)
    // on a 200 ms sample plays out in ~100 ms of output time. The tail math
    // must convert source frames to output frames — not compare them raw.
    {
        auto sampler = makeSampler(0.200, "oneshot-200ms-up12");
        const auto r = render(*sampler, ms(300.0), {{0, kNoteOn, kRootNote + 12, kVelocity}});
        require(!r.hasInvalid, "+12st: output contains NaN/Inf");
        require(peakIn(r, 0, ms(50.0)) > kAudible, "+12st transposed one-shot is silent");
        require(peakIn(r, ms(120.0), ms(300.0)) < kSilence,
                "+12st: voice still sounding past its shortened playback window (stuck voice or unit mix-up)");
    }

    // ---------------- 5. Explicit note-off still drives the normal ADSR
    // release: 750 ms sample, note-off at 200 ms, silent well before the
    // natural endpoint, no restart from the automatic tail.
    {
        auto sampler = makeSampler(0.750, "oneshot-noteoff");
        const auto r =
            render(*sampler, ms(900.0), {{0, kNoteOn, kRootNote, kVelocity}, {ms(200.0), kNoteOff, kRootNote, 0}});
        require(!r.hasInvalid, "note-off: output contains NaN/Inf");
        require(peakIn(r, 0, ms(200.0)) > kAudible, "note-off: no audio before note-off");
        // Release is 300 ms → decaying through it, gone afterwards.
        require(peakIn(r, ms(450.0), ms(500.0)) < peakIn(r, ms(250.0), ms(300.0)), "note-off: release is not decaying");
        require(peakIn(r, ms(560.0), ms(900.0)) < kSilence, "note-off: release did not reach silence");
    }

    // ---------------- 6. Loop exclusion: with looping enabled the automatic
    // end-of-sample fade must not exist — a 100 ms loop keeps sounding far
    // past one sample length until note-off.
    {
        auto sampler = makeSampler(0.100, "loop-100ms");
        sampler->setLoopEnabled(true);
        const auto r =
            render(*sampler, ms(900.0), {{0, kNoteOn, kRootNote, kVelocity}, {ms(500.0), kNoteOff, kRootNote, 0}});
        require(!r.hasInvalid, "loop: output contains NaN/Inf");
        require(peakIn(r, ms(300.0), ms(400.0)) > kAudible,
                "loop: sound stopped after one sample length (automatic fade leaked into loop mode)");
        require(peakIn(r, ms(850.0), ms(900.0)) < kSilence, "loop: no silence after note-off release");
    }

    std::cout << "[PASS] SamplerOneShotEnvelopeTest\n";
    return 0;
}
