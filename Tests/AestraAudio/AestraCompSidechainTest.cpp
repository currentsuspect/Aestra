// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraCompSidechainTest — the compressor's external sidechain (Sidechain param).
//
// The effect chain hands the built-in compressor its key after the main
// channels (EffectChain::process, isBuiltInComp). With Sidechain on, the
// detector follows that key while gain reduction applies to the main signal:
// an 808 under the threshold is ducked only while the kick plays. With
// Sidechain off, or with no key routed, the key is ignored.

#include "Plugin/AestraComp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraComp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockSize = 256;
constexpr uint32_t kFrames = 48000; // 1 s: key silent for the first half, loud for the second

int g_failures = 0;
void check(bool cond, const std::string& label) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << label << "\n";
    if (!cond) ++g_failures;
}

double rmsDb(const std::vector<float>& x, size_t begin, size_t end) {
    double acc = 0.0;
    for (size_t i = begin; i < end; ++i) acc += static_cast<double>(x[i]) * x[i];
    return 10.0 * std::log10(std::max(1.0e-24, acc / static_cast<double>(end - begin)));
}

void configure(AestraComp& comp, bool sidechainOn, float oversampling) {
    comp.initialize(kSampleRate, kBlockSize);
    comp.activate();
    comp.setParameter(AestraComp::kThreshold, 40.0f / 60.0f); // -20 dB: the main signal sits below it
    comp.setParameter(AestraComp::kRatio, 1.0f);              // 20:1
    comp.setParameter(AestraComp::kAttack, 0.0f);
    comp.setParameter(AestraComp::kRelease, 0.0f);
    comp.setParameter(AestraComp::kKnee, 0.0f);
    comp.setParameter(AestraComp::kMix, 1.0f);
    comp.setParameter(AestraComp::kOversampling, oversampling);
    comp.setParameter(AestraComp::kSidechain, sidechainOn ? 1.0f : 0.0f);
}

// Runs main (a quiet 60 Hz "808") through the compressor with an optional key
// (silence, then a loud 100 Hz "kick" burst). channels = 2 (no key) or 4.
std::vector<float> run(bool sidechainOn, uint32_t channels, float oversampling = 0.0f) {
    AestraComp comp;
    configure(comp, sidechainOn, oversampling);
    std::vector<float> mainL(kFrames), mainR(kFrames), keyL(kFrames), keyR(kFrames);
    for (uint32_t i = 0; i < kFrames; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        mainL[i] = mainR[i] = static_cast<float>(0.05 * std::sin(2.0 * 3.14159265358979 * 60.0 * t)); // -29 dB RMS
        keyL[i] = keyR[i] = i < kFrames / 2 ? 0.0f : static_cast<float>(0.9 * std::sin(2.0 * 3.14159265358979 * 100.0 * t));
    }
    std::vector<float> outL(kFrames), outR(kFrames);
    for (uint32_t start = 0; start < kFrames; start += kBlockSize) {
        const uint32_t n = std::min<uint32_t>(kBlockSize, kFrames - start);
        const float* ins[4] = {mainL.data() + start, mainR.data() + start, keyL.data() + start, keyR.data() + start};
        float* outs[2] = {outL.data() + start, outR.data() + start};
        comp.process(ins, outs, channels, 2, n);
    }
    return outL;
}

} // namespace

int main() {
    // Measure past the attack/release settling: the second quarter (key silent)
    // and the last quarter (key loud).
    const size_t quiet0 = kFrames / 4, quiet1 = kFrames / 2;
    const size_t loud0 = kFrames * 3 / 4, loud1 = kFrames;
    const double inputDb = 20.0 * std::log10(0.05 / std::sqrt(2.0));

    const auto on = run(true, 4);
    check(std::abs(rmsDb(on, quiet0, quiet1) - inputDb) < 0.5,
          "sidechain on, key silent: the quiet main signal passes untouched (" +
              std::to_string(rmsDb(on, quiet0, quiet1)) + " dB)");
    const double ducked = inputDb - rmsDb(on, loud0, loud1);
    check(ducked > 10.0, "sidechain on, key loud: the main signal is ducked by " + std::to_string(ducked) + " dB");

    const auto off = run(false, 4);
    check(std::abs(rmsDb(off, loud0, loud1) - inputDb) < 0.5,
          "sidechain off: the key is ignored (" + std::to_string(rmsDb(off, loud0, loud1)) + " dB)");

    const auto noKey = run(true, 2);
    check(std::abs(rmsDb(noKey, loud0, loud1) - inputDb) < 0.5,
          "sidechain on with no key routed: falls back to its own (quiet) input, no ducking");

    const auto os = run(true, 4, 1.0f); // 4x oversampling
    const double duckedOs = inputDb - rmsDb(os, loud0, loud1);
    check(duckedOs > 10.0, "the key also drives the oversampled path (ducked " + std::to_string(duckedOs) + " dB)");

    AestraComp comp;
    configure(comp, true, 0.0f);
    check(comp.getParameterDisplay(AestraComp::kSidechain) == "ON", "the parameter reads ON");
    const auto blob = comp.saveState();
    AestraComp restored;
    restored.initialize(kSampleRate, kBlockSize);
    check(restored.loadState(blob) && restored.getParameter(AestraComp::kSidechain) > 0.5f,
          "Sidechain survives a state round trip");

    if (g_failures == 0) {
        std::cout << "AestraCompSidechainTest: all checks passed\n";
        return 0;
    }
    std::cout << "AestraCompSidechainTest: " << g_failures << " failure(s)\n";
    return 1;
}
