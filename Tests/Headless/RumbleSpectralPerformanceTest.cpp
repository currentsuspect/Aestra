// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Spectral foldback characterization and real-time cost sanity gate for Aestra Rumble.

#include "AudioMeasure.h"
#include "Plugin/PluginHost.h"
#include "RumbleInstance.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using Aestra::Audio::MidiBuffer;
using Aestra::Plugins::RumbleInstance;
using AudioResearch::fitTone;
using AudioResearch::Signal;
using AudioResearch::toDb;

namespace {
constexpr uint8_t kProbeNote = 96;
constexpr double kProbeFrequencyHz = 2093.004522404789;

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
    }
    return condition;
}

Signal renderSaturationProbe(uint32_t sampleRate, bool hardMode) {
    constexpr uint32_t blockSize = 127;
    const uint32_t totalFrames = sampleRate;

    RumbleInstance rumble(RumbleInstance::TestLicense::GrantRumble);
    rumble.initialize(sampleRate, blockSize);
    rumble.setParameter(0, 1.0f);                    // longest amplitude decay
    rumble.setParameter(1, 1.0f);                    // maximum drive
    rumble.setParameter(2, 1.0f);                    // open tone filter
    rumble.setParameter(3, 0.35f);                   // keep the safety knee mostly inactive
    rumble.setParameter(4, 0.0f);                    // no pitch envelope
    rumble.setParameter(7, 0.0f);                    // minimum attack
    rumble.setParameter(8, 0.0f);                    // minimum filter resonance
    rumble.setParameter(9, 0.0f);                    // no transient harmonic
    rumble.setParameter(10, 0.0f);                   // no noise click
    rumble.setParameter(17, 0.5f);                   // neutral filter envelope
    rumble.setParameter(18, 0.0f);                   // no keytrack offset
    rumble.setParameter(19, hardMode ? 1.0f : 0.0f); // saturation mode
    rumble.setParameter(20, 0.0f);                   // fixed amplitude
    rumble.setParameter(23, 0.0f);                   // pure fundamental before drive
    rumble.setParameter(24, 0.0f);                   // expose the nonlinear path
    rumble.activate();

    Signal signal;
    signal.sampleRate = sampleRate;
    signal.channels = 1;
    signal.samples.reserve(totalFrames);
    std::vector<float> output(blockSize, 0.0f);
    float* outputs[1] = {output.data()};

    for (uint32_t frame = 0; frame < totalFrames; frame += blockSize) {
        const uint32_t framesThisBlock = std::min(blockSize, totalFrames - frame);
        MidiBuffer midi;
        if (frame == 0) {
            midi.addNoteOn(1, kProbeNote, 127, 0);
        }
        std::fill(output.begin(), output.end(), 0.0f);
        rumble.process(nullptr, outputs, 0, 1, framesThisBlock, &midi, nullptr);
        signal.samples.insert(signal.samples.end(), output.begin(), output.begin() + framesThisBlock);
    }
    return signal;
}

double foldedFrequency(double frequency, double sampleRate) {
    double folded = std::fmod(frequency, sampleRate);
    if (folded > sampleRate * 0.5) {
        folded = sampleRate - folded;
    }
    return std::abs(folded);
}

double maximumAliasDbc(const Signal& signal) {
    const uint32_t startFrame = static_cast<uint32_t>(0.12 * signal.sampleRate);
    const uint32_t endFrame = static_cast<uint32_t>(0.72 * signal.sampleRate);
    const double fundamental = fitTone(signal, 0, kProbeFrequencyHz, startFrame, endFrame).amplitude;
    if (fundamental <= 1.0e-12) {
        return std::numeric_limits<double>::infinity();
    }

    const double nyquist = signal.sampleRate * 0.5;
    double maximumAlias = 0.0;
    uint32_t measured = 0;
    // A symmetric driven sine produces odd harmonics. Probe the first twelve odd
    // harmonics above Nyquist at their exact foldback frequencies.
    for (uint32_t harmonic = 3; measured < 12; harmonic += 2) {
        const double harmonicFrequency = kProbeFrequencyHz * harmonic;
        if (harmonicFrequency <= nyquist) {
            continue;
        }
        const double aliasFrequency = foldedFrequency(harmonicFrequency, signal.sampleRate);
        if (aliasFrequency < 100.0 || aliasFrequency > nyquist - 100.0) {
            continue;
        }

        bool overlapsInBandHarmonic = false;
        for (uint32_t inBand = 1; kProbeFrequencyHz * inBand < nyquist; inBand += 2) {
            if (std::abs(aliasFrequency - kProbeFrequencyHz * inBand) < 30.0) {
                overlapsInBandHarmonic = true;
                break;
            }
        }
        if (overlapsInBandHarmonic) {
            continue;
        }

        maximumAlias = std::max(maximumAlias, fitTone(signal, 0, aliasFrequency, startFrame, endFrame).amplitude);
        ++measured;
    }
    return toDb(maximumAlias / fundamental);
}

struct PerformanceResult {
    double medianNanosecondsPerSample = 0.0;
    double realtimeFactor = 0.0;
    float checksum = 0.0f;
};

PerformanceResult measurePerformance() {
    constexpr uint32_t sampleRate = 48000;
    constexpr uint32_t blockSize = 256;
    constexpr uint32_t framesPerIteration = sampleRate * 2;
    constexpr uint32_t iterations = 7;

    std::vector<double> times;
    times.reserve(iterations);
    float checksum = 0.0f;
    for (uint32_t iteration = 0; iteration < iterations + 1; ++iteration) {
        RumbleInstance rumble(RumbleInstance::TestLicense::GrantRumble);
        rumble.initialize(sampleRate, blockSize);
        rumble.setParameter(1, 1.0f);
        rumble.setParameter(2, 1.0f);
        rumble.setParameter(19, 1.0f);
        rumble.setParameter(23, 1.0f);
        rumble.setParameter(24, 0.0f);
        rumble.activate();

        std::vector<float> left(blockSize, 0.0f);
        std::vector<float> right(blockSize, 0.0f);
        float* outputs[2] = {left.data(), right.data()};
        const auto start = std::chrono::steady_clock::now();
        for (uint32_t frame = 0; frame < framesPerIteration; frame += blockSize) {
            const uint32_t framesThisBlock = std::min(blockSize, framesPerIteration - frame);
            MidiBuffer midi;
            if (frame == 0) {
                midi.addNoteOn(1, 36, 127, 0);
            }
            rumble.process(nullptr, outputs, 0, 2, framesThisBlock, &midi, nullptr);
            checksum += left[framesThisBlock - 1] + right[framesThisBlock - 1];
        }
        const auto end = std::chrono::steady_clock::now();
        if (iteration > 0) {
            times.push_back(std::chrono::duration<double, std::nano>(end - start).count() / framesPerIteration);
        }
    }

    std::sort(times.begin(), times.end());
    const double median = times[times.size() / 2];
    const double realtimeFactor = 1.0e9 / (median * sampleRate);
    return {median, realtimeFactor, checksum};
}
} // namespace

int main() {
    bool ok = true;
    for (uint32_t sampleRate : {44100u, 48000u, 96000u}) {
        const double softAliasDbc = maximumAliasDbc(renderSaturationProbe(sampleRate, false));
        const double hardAliasDbc = maximumAliasDbc(renderSaturationProbe(sampleRate, true));
        std::cout << sampleRate << " Hz: soft max foldback=" << softAliasDbc
                  << " dBc, hard max foldback=" << hardAliasDbc << " dBc\n";
        ok &= check(std::isfinite(softAliasDbc) && std::isfinite(hardAliasDbc),
                    std::to_string(sampleRate) + " Hz alias measurement was not finite");
        ok &= check(softAliasDbc < -35.0, std::to_string(sampleRate) + " Hz soft saturation foldback exceeds -35 dBc");
        ok &= check(hardAliasDbc < -30.0, std::to_string(sampleRate) + " Hz hard saturation foldback exceeds -30 dBc");
    }

    const PerformanceResult performance = measurePerformance();
    std::cout << "Worst-path median: " << performance.medianNanosecondsPerSample << " ns/sample, "
              << performance.realtimeFactor << "x real-time\n";
    ok &= check(std::isfinite(performance.checksum), "performance render checksum was not finite");
    // Deliberately loose machine-independent sanity gate. The printed median is the
    // useful comparison metric; this threshold only catches accidental runaway work.
    ok &= check(performance.realtimeFactor > 10.0, "single Rumble instance is below 10x real-time");

    if (!ok) {
        return 1;
    }
    std::cout << "Rumble spectral/performance contract passed.\n";
    return 0;
}
