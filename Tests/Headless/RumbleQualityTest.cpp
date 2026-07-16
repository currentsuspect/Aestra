// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Deterministic, hardware-free signal-quality contract for Aestra Rumble.

#include "Plugin/PluginHost.h"
#include "RumbleInstance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using Aestra::Audio::MidiBuffer;
using Aestra::Plugins::RumbleInstance;

namespace {
constexpr double kExpectedC2Hz = 65.40639132514966;

struct RenderResult {
    std::vector<float> mono;
    bool stereoMatched = true;
    bool finite = true;
    float peak = 0.0f;
};

RenderResult render(double sampleRate, uint32_t blockSize, bool phrase, float drive = 0.0f, float harmonics = 0.0f,
                    float subClean = 0.70f, float outputGain = 0.42f, float punch = 0.0f, float attack = 0.0f) {
    RumbleInstance rumble(RumbleInstance::TestLicense::GrantRumble);
    if (!rumble.initialize(sampleRate, blockSize)) {
        return {};
    }

    // Isolate pitch and engine consistency from the deliberately noisy click layer.
    rumble.setParameter(0, 0.72f); // decay
    rumble.setParameter(1, drive);
    rumble.setParameter(2, 1.0f); // tone
    rumble.setParameter(3, outputGain);
    rumble.setParameter(4, 0.0f); // pitch envelope amount
    rumble.setParameter(7, attack);
    rumble.setParameter(9, punch); // independent punch/body excitation
    rumble.setParameter(10, 0.0f); // click
    rumble.setParameter(17, 0.5f); // neutral filter envelope
    rumble.setParameter(23, harmonics);
    rumble.setParameter(24, subClean);
    rumble.activate();

    const uint64_t totalFrames = static_cast<uint64_t>(std::llround(sampleRate * 1.25));
    RenderResult result;
    result.mono.reserve(static_cast<size_t>(totalFrames));

    std::vector<float> left(blockSize, 0.0f);
    std::vector<float> right(blockSize, 0.0f);
    float* outputs[2] = {left.data(), right.data()};

    const uint64_t secondOn = static_cast<uint64_t>(std::llround(sampleRate * 0.50));
    const uint64_t secondOff = static_cast<uint64_t>(std::llround(sampleRate * 0.82));
    const uint64_t firstOff = static_cast<uint64_t>(std::llround(sampleRate * 1.00));

    for (uint64_t frame = 0; frame < totalFrames; frame += blockSize) {
        const uint32_t framesThisBlock = static_cast<uint32_t>(std::min<uint64_t>(blockSize, totalFrames - frame));
        MidiBuffer midi;
        if (frame == 0) {
            midi.addNoteOn(1, 36, 112, 0);
        }
        auto addPhraseEvent = [&](uint64_t eventFrame, bool noteOn, uint8_t note, uint8_t velocity) {
            if (!phrase || eventFrame < frame || eventFrame >= frame + framesThisBlock) {
                return;
            }
            const uint32_t offset = static_cast<uint32_t>(eventFrame - frame);
            if (noteOn) {
                midi.addNoteOn(1, note, velocity, offset);
            } else {
                midi.addNoteOff(1, note, 0, offset);
            }
        };
        addPhraseEvent(secondOn, true, 43, 96);
        addPhraseEvent(secondOff, false, 43, 0);
        addPhraseEvent(firstOff, false, 36, 0);

        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        rumble.process(nullptr, outputs, 0, 2, framesThisBlock, &midi, nullptr);

        for (uint32_t i = 0; i < framesThisBlock; ++i) {
            result.finite = result.finite && std::isfinite(left[i]) && std::isfinite(right[i]);
            result.stereoMatched = result.stereoMatched && left[i] == right[i];
            result.peak = std::max(result.peak, std::abs(left[i]));
            result.mono.push_back(left[i]);
        }
    }
    return result;
}

double estimateFrequency(const std::vector<float>& signal, double sampleRate, double startSeconds, double endSeconds) {
    const size_t start = static_cast<size_t>(std::clamp(startSeconds * sampleRate, 1.0, signal.size() - 1.0));
    const size_t end = static_cast<size_t>(std::clamp(endSeconds * sampleRate, 1.0, signal.size() - 1.0));
    std::vector<double> crossings;
    for (size_t i = start; i < end; ++i) {
        if (signal[i - 1] <= 0.0f && signal[i] > 0.0f) {
            const double denominator = static_cast<double>(signal[i] - signal[i - 1]);
            const double fraction = denominator == 0.0 ? 0.0 : -static_cast<double>(signal[i - 1]) / denominator;
            crossings.push_back(static_cast<double>(i - 1) + fraction);
        }
    }
    if (crossings.size() < 4) {
        return 0.0;
    }
    const double period = (crossings.back() - crossings.front()) / static_cast<double>(crossings.size() - 1);
    return period > 0.0 ? sampleRate / period : 0.0;
}

double centsBetween(double actual, double expected) {
    return 1200.0 * std::log2(actual / expected);
}

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "failed: " << message << "\n";
    }
    return condition;
}

double differenceRms(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    long double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const long double delta = static_cast<long double>(a[i]) - static_cast<long double>(b[i]);
        sum += delta * delta;
    }
    return std::sqrt(sum / static_cast<long double>(a.size()));
}

double signalRms(const std::vector<float>& signal) {
    long double sum = 0.0;
    for (float sample : signal) {
        sum += static_cast<long double>(sample) * static_cast<long double>(sample);
    }
    return signal.empty() ? 0.0 : std::sqrt(sum / static_cast<long double>(signal.size()));
}

double windowRms(const std::vector<float>& signal, size_t start, size_t end) {
    start = std::min(start, signal.size());
    end = std::min(std::max(start, end), signal.size());
    long double sum = 0.0;
    for (size_t i = start; i < end; ++i) {
        sum += static_cast<long double>(signal[i]) * static_cast<long double>(signal[i]);
    }
    return end > start ? std::sqrt(sum / static_cast<long double>(end - start)) : 0.0;
}

double signalMean(const std::vector<float>& signal) {
    long double sum = 0.0;
    for (float sample : signal) {
        sum += sample;
    }
    return signal.empty() ? 0.0 : static_cast<double>(sum / static_cast<long double>(signal.size()));
}
} // namespace

int main() {
    bool ok = true;

    const RenderResult first = render(48000.0, 257, true);
    const RenderResult repeat = render(48000.0, 257, true);
    const RenderResult otherBlock = render(48000.0, 64, true);
    ok &= check(first.finite && repeat.finite && otherBlock.finite, "render contains NaN or Inf");
    ok &= check(first.stereoMatched && repeat.stereoMatched && otherBlock.stereoMatched,
                "sub output is not exactly mono-compatible");
    ok &= check(first.peak > 1.0e-4f && first.peak < 0.98f, "render peak is silent or unsafe");
    ok &= check(first.mono == repeat.mono, "identical input does not produce an identical render");
    ok &= check(std::abs(signalMean(first.mono)) < 1.0e-4, "render has excessive DC offset");
    ok &= check(first.mono.size() == otherBlock.mono.size(), "block-size renders have different lengths");
    if (first.mono.size() == otherBlock.mono.size()) {
        float maxDelta = 0.0f;
        for (size_t i = 0; i < first.mono.size(); ++i) {
            maxDelta = std::max(maxDelta, std::abs(first.mono[i] - otherBlock.mono[i]));
        }
        ok &= check(maxDelta <= 1.0e-7f, "render changes with host block size");
    }

    const RenderResult pure = render(48000.0, 127, false, 0.0f, 0.0f, 1.0f);
    const RenderResult rich = render(48000.0, 127, false, 0.0f, 1.0f, 1.0f);
    const RenderResult driven = render(48000.0, 127, false, 0.80f, 0.0f, 0.70f);
    const RenderResult extreme = render(48000.0, 127, false, 1.0f, 1.0f, 0.0f, 1.0f);
    ok &= check(differenceRms(pure.mono, rich.mono) > 0.005, "Harmonics control does not materially change tone");
    ok &= check(differenceRms(pure.mono, driven.mono) > 0.005, "Drive control does not materially change tone");
    const double pureRms = signalRms(pure.mono);
    const double drivenRms = signalRms(driven.mono);
    if (pureRms > 0.0) {
        std::cout << "Drive level ratio: " << (drivenRms / pureRms) << "\n";
    }
    ok &= check(pureRms > 0.0 && drivenRms / pureRms > 0.65 && drivenRms / pureRms < 1.50,
                "parallel Drive does not preserve a usable output level");
    ok &= check(extreme.finite && extreme.peak < 0.98f, "extreme settings exceed the final safety ceiling");

    const RenderResult punchOff = render(48000.0, 127, false, 0.0f, 0.0f, 1.0f, 0.38f, 0.0f, 0.12f);
    const RenderResult punchOn = render(48000.0, 127, false, 0.0f, 0.0f, 1.0f, 0.38f, 1.0f, 0.12f);
    const double dryAttackRms = windowRms(punchOff.mono, 0, 2400);
    const double punchAttackRms = windowRms(punchOn.mono, 0, 2400);
    const double dryTailRms = windowRms(punchOff.mono, 7200, 12000);
    const double punchTailRms = windowRms(punchOn.mono, 7200, 12000);
    std::cout << "Punch early-energy ratio: " << (punchAttackRms / std::max(1.0e-12, dryAttackRms))
              << ", tail ratio: " << (punchTailRms / std::max(1.0e-12, dryTailRms)) << "\n";
    ok &= check(punchAttackRms > dryAttackRms * 1.25, "Punch does not add at least 25% RMS energy to the first 50 ms");
    // Both sides of the contract: Punch must neither leak into nor carve away
    // the settled tail — it is an attack-stage effect.
    ok &= check(punchTailRms < dryTailRms * 1.15, "Punch leaks more than 15% extra RMS energy into the settled tail");
    ok &= check(punchTailRms > dryTailRms * 0.85, "Punch removes more than 15% of the settled tail's RMS energy");
    ok &= check(punchOn.finite && punchOn.peak < 0.98f, "maximum Punch is non-finite or exceeds safety ceiling");

    for (double sampleRate : {44100.0, 48000.0, 96000.0}) {
        const RenderResult pitchRender = render(sampleRate, 127, false);
        const double frequency = estimateFrequency(pitchRender.mono, sampleRate, 0.35, 0.80);
        const double cents =
            frequency > 0.0 ? centsBetween(frequency, kExpectedC2Hz) : std::numeric_limits<double>::infinity();
        if (std::abs(cents) > 2.0) {
            std::cerr << "failed: C2 pitch error at " << sampleRate << " Hz is " << cents << " cents (" << frequency
                      << " Hz)\n";
            ok = false;
        }
    }

    if (!ok) {
        return 1;
    }
    std::cout << "Rumble quality contract passed: deterministic, block-invariant, mono-safe, finite, and pitch-true.\n";
    return 0;
}
