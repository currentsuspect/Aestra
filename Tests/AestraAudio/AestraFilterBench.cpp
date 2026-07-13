// © 2026 Aestra Studios — All Rights Reserved.
// AestraFilterBench — multi-instance throughput benchmark for AestraFilter.
//
// Measures 1/4/8/16 instances at 44.1/48/96 kHz and reports per-callback cost
// against the audio deadline. Run on the Folio-class target before merging
// DSP changes; CI never runs this (gated behind AESTRA_ENABLE_EXPERIMENTAL_TESTS).

#include "Plugin/AestraFilter.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using Aestra::Audio::Plugins::AestraFilter;

namespace {

constexpr uint32_t kBlockSize = 256;
constexpr uint32_t kBlocks = 4000; // ~21 s of audio at 48 kHz per config

double benchConfig(uint32_t instances, double sampleRate) {
    std::vector<std::unique_ptr<AestraFilter>> filters;
    for (uint32_t n = 0; n < instances; ++n) {
        auto f = std::make_unique<AestraFilter>();
        f->initialize(sampleRate, kBlockSize);
        // Worst-ish case: envelope modulation active, resonant, driven
        f->setParameter(AestraFilter::kCutoff, 0.4f);
        f->setParameter(AestraFilter::kReso, 0.8f);
        f->setParameter(AestraFilter::kDrive, 0.7f);
        f->setParameter(AestraFilter::kEnvAmount, 0.9f);
        f->activate();
        filters.push_back(std::move(f));
    }

    std::vector<float> inL(kBlockSize), inR(kBlockSize);
    std::vector<float> outL(kBlockSize), outR(kBlockSize);
    const double w = 2.0 * 3.14159265358979323846 * 220.0 / sampleRate;

    // Pre-generate a representative input block so the timed region measures
    // only process(), not the oscillator that fills the buffer (~1M std::sin
    // calls per config would otherwise pollute us/callback at low instance
    // counts).
    double phase = 0.0;
    for (uint32_t i = 0; i < kBlockSize; ++i) {
        inL[i] = inR[i] = static_cast<float>(0.5 * std::sin(phase));
        phase += w;
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (uint32_t b = 0; b < kBlocks; ++b) {
        for (auto& f : filters) {
            const float* ins[] = {inL.data(), inR.data()};
            float* outs[] = {outL.data(), outR.data()};
            f->process(ins, outs, 2, 2, kBlockSize);
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

} // namespace

int main() {
    std::printf("AestraFilter multi-instance benchmark (block %u, %u blocks/config)\n", kBlockSize, kBlocks);
    std::printf("%-10s %-10s %-12s %-14s %-10s\n", "rate", "instances", "wall (s)", "us/callback", "% budget");

    for (double sr : {44100.0, 48000.0, 96000.0}) {
        const double budgetUs = 1.0e6 * kBlockSize / sr; // real-time deadline per callback
        for (uint32_t n : {1u, 4u, 8u, 16u}) {
            const double secs = benchConfig(n, sr);
            const double usPerCallback = secs * 1.0e6 / kBlocks;
            std::printf("%-10.0f %-10u %-12.3f %-14.2f %-10.1f\n", sr, n, secs, usPerCallback,
                        100.0 * usPerCallback / budgetUs);
        }
    }
    std::printf("Values are for this machine; gate merges on the Folio-class target.\n");
    return 0;
}
