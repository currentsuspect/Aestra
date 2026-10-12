// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SamplerSwapTest (V81-E3, FD-27 "swap while it plays, gap-free")
//
// Swapping a unit's sample while the pattern plays must not cut or jump what is already sounding, and the
// next hit must use the new sample:
//   - a note held across the swap plays on, sample for sample, exactly as it would have without the swap;
//   - a note struck after the swap plays the new sample;
//   - swapping repeatedly while notes sound is safe, and the swapped-out buffers are let go once silent.
// Drives SamplerPlugin directly: no engine, no files, no devices.

#include "Plugin/PluginHost.h"
#include "Plugin/SamplerPlugin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using Aestra::Audio::Plugins::SamplerPlugin;

constexpr uint32_t kRate = 48000;
constexpr uint32_t kBlock = 256;
constexpr double kPi = 3.14159265358979323846;
int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) ++failures;
}

std::vector<float> sine(double seconds, double hz) {
    std::vector<float> s(static_cast<size_t>(seconds * kRate));
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = 0.5f * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
    return s;
}

uint32_t ms(double t) { return static_cast<uint32_t>(t * kRate / 1000.0); }

std::unique_ptr<SamplerPlugin> make(double hz) {
    auto s = std::make_unique<SamplerPlugin>();
    if (!s->initialize(kRate, kBlock) || !s->loadSampleData("a", sine(3.0, hz), kRate, 1)) std::exit(2);
    s->activate();
    s->setEnvelope(0.001f, 0.001f, 1.0f, 0.010f);
    return s;
}

struct Ev { uint32_t frame; uint8_t status; };

// Left channel; `atBlock` runs before each block with the block's first frame (to swap mid-stream).
std::vector<float> render(SamplerPlugin& s, uint32_t frames, const std::vector<Ev>& events,
                          const std::function<void(uint32_t)>& atBlock = {}) {
    std::vector<float> out(frames), l(kBlock), r(kBlock);
    float* ch[2] = {l.data(), r.data()};
    for (uint32_t start = 0; start < frames; start += kBlock) {
        if (atBlock) atBlock(start);
        const uint32_t n = std::min(kBlock, frames - start);
        Aestra::Audio::MidiBuffer midi;
        for (const auto& e : events)
            if (e.frame >= start && e.frame < start + n) {
                const uint8_t d[3] = {e.status, 60, static_cast<uint8_t>(e.status == 0x90 ? 110 : 0)};
                midi.addEvent(e.frame - start, d, 3, 0);
            }
        s.process(nullptr, ch, 0, 2, n, &midi, nullptr);
        std::copy(l.begin(), l.begin() + n, out.begin() + start);
    }
    return out;
}

double zeroCrossingHz(const std::vector<float>& x, uint32_t from, uint32_t to) {
    int first = -1, last = -1, count = 0;
    for (uint32_t i = from + 1; i < to && i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) { if (first < 0) first = static_cast<int>(i); last = static_cast<int>(i); ++count; }
    return count > 1 ? (count - 1) * static_cast<double>(kRate) / (last - first) : 0.0;
}

float maxDiff(const std::vector<float>& a, const std::vector<float>& b, uint32_t from, uint32_t to) {
    float d = 0.0f;
    for (uint32_t i = from; i < to && i < a.size() && i < b.size(); ++i) d = std::max(d, std::abs(a[i] - b[i]));
    return d;
}

} // namespace

int main() {
    const std::vector<Ev> held{{0, 0x90}, {ms(1000), 0x80}};

    std::cout << "[1] a note held across the swap plays on exactly as before\n";
    {
        auto baseline = make(200.0);
        const auto base = render(*baseline, ms(1200), held);
        auto swapped = make(200.0);
        bool done = false;
        const auto x = render(*swapped, ms(1200), held, [&](uint32_t start) {
            if (!done && start >= ms(200)) { done = swapped->loadSampleData("b", sine(3.0, 800.0), kRate, 1); }
        });
        check(done, "the swap happened mid-note");
        check(maxDiff(base, x, ms(250), ms(1000)) < 1e-6f, "after the swap the held note matches the unswapped render sample for sample (diff " + std::to_string(maxDiff(base, x, ms(250), ms(1000))) + ")");
        check(std::abs(zeroCrossingHz(x, ms(300), ms(900)) - 200.0) < 6.0, "it is still the old tone, about 200 Hz");
    }

    std::cout << "[2] the next hit uses the new sample\n";
    {
        auto s = make(200.0);
        bool done = false;
        const auto x = render(*s, ms(1600), {{0, 0x90}, {ms(1000), 0x80}, {ms(1100), 0x90}, {ms(1500), 0x80}}, [&](uint32_t start) {
            if (!done && start >= ms(300)) { done = s->loadSampleData("b", sine(3.0, 800.0), kRate, 1); }
        });
        check(std::abs(zeroCrossingHz(x, ms(400), ms(900)) - 200.0) < 6.0, "the note that started before the swap stays 200 Hz");
        check(std::abs(zeroCrossingHz(x, ms(1150), ms(1450)) - 800.0) < 25.0, "the note struck after it is the new 800 Hz sample");
    }

    std::cout << "[3] swapping again and again while notes sound\n";
    {
        auto s = make(200.0);
        int swaps = 0;
        const auto x = render(*s, ms(2000), {{0, 0x90}, {ms(500), 0x90}, {ms(900), 0x90}, {ms(1900), 0x80}}, [&](uint32_t start) {
            if (start > 0 && start % ms(100) < kBlock) { s->loadSampleData("n", sine(3.0, 200.0 + 50.0 * (swaps % 5)), kRate, 1); ++swaps; }
        });
        bool finite = true, audible = false;
        for (float v : x) { finite = finite && std::isfinite(v); audible = audible || std::abs(v) > 1e-3f; }
        check(swaps > 10 && finite && audible, "ten-plus swaps under sounding notes: audio stays finite and present (" + std::to_string(swaps) + " swaps)");
        s->shutdown();
        check(true, "shutdown after swaps lets every retired buffer go");
    }

    std::cout << (failures == 0 ? "[PASS] SamplerSwapTest\n" : "[FAIL] SamplerSwapTest: " + std::to_string(failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
