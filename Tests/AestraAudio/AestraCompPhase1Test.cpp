// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraCompPhase1Test — Validates Phase 1 core DSP rebuild.
// Run: cmake --build build --target AestraCompPhase1Test && ./build/bin/AestraCompPhase1Test

#include "Plugin/AestraComp.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace Aestra::Audio::Plugins;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockSize = 512;
constexpr double kTau = 6.28318530717958647692;

void generateTone(float* buffer, uint32_t frames, double freq, float ampLinear) {
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        buffer[i] = static_cast<float>(std::sin(kTau * freq * t) * ampLinear);
    }
}

void generateNoise(float* buffer, uint32_t frames, float ampLinear) {
    // Simple deterministic noise (not random — reproducible tests)
    uint32_t seed = 12345;
    for (uint32_t i = 0; i < frames; ++i) {
        seed = seed * 1103515245 + 12345;
        float normalized = static_cast<float>((seed >> 16) & 0x7FFF) / 32767.0f - 0.5f;
        buffer[i] = normalized * ampLinear;
    }
}

void generateImpulse(float* buffer, uint32_t frames, float ampLinear) {
    std::memset(buffer, 0, frames * sizeof(float));
    buffer[0] = ampLinear;
}

float linearToDb(float linear) {
    return linear > 1e-12f ? 20.0f * std::log10(linear) : -120.0f;
}

float calculateRMS(const float* buffer, uint32_t frames) {
    double sum = 0.0;
    for (uint32_t i = 0; i < frames; ++i) sum += buffer[i] * buffer[i];
    return static_cast<float>(std::sqrt(sum / frames));
}

float calculatePeak(const float* buffer, uint32_t frames) {
    float peak = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        float a = std::abs(buffer[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

std::vector<float> processBlocks(AestraComp& comp, const float* input,
                                  uint32_t numBlocks, uint32_t blockSize) {
    uint32_t totalFrames = numBlocks * blockSize;
    std::vector<float> output(totalFrames, 0.0f);
    const float* inPtr = input;
    float* outPtr = output.data();
    for (uint32_t b = 0; b < numBlocks; ++b) {
        comp.process(&inPtr, &outPtr, 1, 1, blockSize);
        inPtr += blockSize;
        outPtr += blockSize;
    }
    return output;
}

std::pair<std::vector<float>, std::vector<float>>
processStereoBlocks(AestraComp& comp, const float* inL, const float* inR,
                     uint32_t numBlocks, uint32_t blockSize) {
    uint32_t totalFrames = numBlocks * blockSize;
    std::vector<float> outL(totalFrames, 0.0f), outR(totalFrames, 0.0f);
    for (uint32_t b = 0; b < numBlocks; ++b) {
        uint32_t off = b * blockSize;
        const float* inputs[2] = {inL + off, inR + off};
        float* outputs[2] = {outL.data() + off, outR.data() + off};
        comp.process(inputs, outputs, 2, 2, blockSize);
    }
    return {outL, outR};
}

void setDefaults(AestraComp& comp) {
    comp.setParameter(AestraComp::kThreshold, 0.5f);
    comp.setParameter(AestraComp::kRatio, 0.2f);
    comp.setParameter(AestraComp::kAttack, 0.1f);
    comp.setParameter(AestraComp::kRelease, 0.3f);
    comp.setParameter(AestraComp::kMakeup, 0.0f);
    comp.setParameter(AestraComp::kKnee, 0.25f);
    comp.setParameter(AestraComp::kMix, 1.0f);
    comp.setParameter(AestraComp::kBypass, 0.0f);
    comp.setParameter(AestraComp::kDetectorMode, 0.0f); // peak
    comp.setParameter(AestraComp::kTopology, 0.0f);     // feed-forward
    comp.setParameter(AestraComp::kHold, 0.0f);
    comp.setParameter(AestraComp::kAutoRelease, 0.0f);
    comp.setParameter(AestraComp::kRange, 0.0f);         // no range limit
    comp.setParameter(AestraComp::kLookahead, 0.0f);
    comp.setParameter(AestraComp::kStereoLink, 1.0f);    // fully linked
    comp.setParameter(AestraComp::kStereoLinkLaw, 0.0f); // max
    comp.setParameter(AestraComp::kSCHPF, 0.0f);
    comp.setParameter(AestraComp::kSCLPF, 0.0f);
    comp.setParameter(AestraComp::kSCListen, 0.0f);
    comp.setParameter(AestraComp::kOutputTrim, 0.5f);    // 0dB
    comp.setParameter(AestraComp::kStyle, 0.0f);         // clean
    comp.setParameter(AestraComp::kQuality, 1.0f);       // normal
}

// =====================================================================
// T1: New parameters exist and are valid
// =====================================================================
bool testNewParamsExist() {
    std::cout << "  [T1] New parameters exist... ";

    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);

    // Should have more params than old 8
    if (comp.getParameterCount() <= 8) {
        std::cerr << "FAILED: param count=" << comp.getParameterCount() << " (expected > 8)\n";
        return false;
    }

    // All new params should be gettable/settable
    auto params = comp.getParameters();
    if (params.size() != comp.getParameterCount()) {
        std::cerr << "FAILED: getParameters() size=" << params.size()
                  << " != getParameterCount()=" << comp.getParameterCount() << "\n";
        return false;
    }

    // Check all params have valid metadata
    for (uint32_t i = 0; i < params.size(); ++i) {
        if (params[i].id != i) {
            std::cerr << "FAILED: param[" << i << "].id=" << params[i].id << "\n";
            return false;
        }
        if (params[i].name.empty()) {
            std::cerr << "FAILED: param[" << i << "] has empty name\n";
            return false;
        }
        std::string display = comp.getParameterDisplay(i);
        if (display.empty()) {
            std::cerr << "FAILED: param[" << i << "] display is empty\n";
            return false;
        }
    }

    std::cout << "PASSED (" << comp.getParameterCount() << " params)\n";
    return true;
}

// =====================================================================
// T2: State save/load round-trips all new params
// =====================================================================
bool testStateRoundTrip() {
    std::cout << "  [T2] State round-trip... ";

    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    setDefaults(comp);

    // Set all params to non-default values
    uint32_t count = comp.getParameterCount();
    for (uint32_t i = 0; i < count; ++i) {
        float v = 0.1f + (static_cast<float>(i) / count) * 0.8f;
        comp.setParameter(i, v);
    }

    auto state = comp.saveState();

    AestraComp comp2;
    comp2.initialize(kSampleRate, kBlockSize);
    if (!comp2.loadState(state)) {
        std::cerr << "FAILED: loadState returned false\n";
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        float v1 = comp.getParameter(i);
        float v2 = comp2.getParameter(i);
        if (std::abs(v1 - v2) > 1e-5f) {
            std::cerr << "FAILED: param[" << i << "]: " << v1 << " != " << v2 << "\n";
            return false;
        }
    }

    std::cout << "PASSED\n";
    return true;
}

// =====================================================================
// T3: Peak vs RMS detection produces different results
// =====================================================================
bool testDetectorModes() {
    std::cout << "  [T3] Peak vs RMS detection... ";

    const uint32_t numBlocks = 30;
    const uint32_t totalFrames = numBlocks * kBlockSize;
    std::vector<float> input(totalFrames);
    // Use noise — peaks are higher than RMS
    generateNoise(input.data(), totalFrames, 0.5f);

    // Peak mode
    AestraComp compPeak;
    compPeak.initialize(kSampleRate, kBlockSize);
    setDefaults(compPeak);
    compPeak.setParameter(AestraComp::kDetectorMode, 0.0f); // peak
    compPeak.setParameter(AestraComp::kThreshold, 0.4f);    // -36dB
    compPeak.setParameter(AestraComp::kRatio, 0.4f);        // ~8.6:1
    compPeak.activate();
    auto outPeak = processBlocks(compPeak, input.data(), numBlocks, kBlockSize);

    // RMS mode
    AestraComp compRMS;
    compRMS.initialize(kSampleRate, kBlockSize);
    setDefaults(compRMS);
    compRMS.setParameter(AestraComp::kDetectorMode, 1.0f);  // RMS
    compRMS.setParameter(AestraComp::kThreshold, 0.4f);
    compRMS.setParameter(AestraComp::kRatio, 0.4f);
    compRMS.activate();
    auto outRMS = processBlocks(compRMS, input.data(), numBlocks, kBlockSize);

    // GR should differ — peak detects higher levels, more compression
    float grPeak = compPeak.getCurrentGainReductionDb();
    float grRMS = compRMS.getCurrentGainReductionDb();

    if (std::abs(grPeak - grRMS) < 0.5f) {
        std::cerr << "FAILED: peak GR=" << grPeak << " dB, RMS GR=" << grRMS
                  << " dB (too similar)\n";
        return false;
    }

    // Peak should compress more (detects higher levels)
    if (grPeak <= grRMS) {
        std::cerr << "FAILED: peak GR=" << grPeak << " should be > RMS GR=" << grRMS << "\n";
        return false;
    }

    std::cout << "PASSED (peakGR=" << std::fixed << std::setprecision(1)
              << grPeak << "dB, rmsGR=" << grRMS << "dB)\n";
    return true;
}

// =====================================================================
// T4: Feed-forward vs feedback produce different results
// =====================================================================
bool testTopologies() {
    std::cout << "  [T4] Feed-forward vs feedback... ";

    const uint32_t numBlocks = 30;
    const uint32_t totalFrames = numBlocks * kBlockSize;
    std::vector<float> input(totalFrames);
    generateTone(input.data(), totalFrames, 1000.0, 0.5f);

    // Feed-forward
    AestraComp compFF;
    compFF.initialize(kSampleRate, kBlockSize);
    setDefaults(compFF);
    compFF.setParameter(AestraComp::kTopology, 0.0f); // feed-forward
    compFF.setParameter(AestraComp::kThreshold, 0.5f);
    compFF.setParameter(AestraComp::kRatio, 0.5f);
    compFF.setParameter(AestraComp::kAttack, 0.01f); // fast
    compFF.activate();
    auto outFF = processBlocks(compFF, input.data(), numBlocks, kBlockSize);

    // Feedback
    AestraComp compFB;
    compFB.initialize(kSampleRate, kBlockSize);
    setDefaults(compFB);
    compFB.setParameter(AestraComp::kTopology, 1.0f); // feedback
    compFB.setParameter(AestraComp::kThreshold, 0.5f);
    compFB.setParameter(AestraComp::kRatio, 0.5f);
    compFB.setParameter(AestraComp::kAttack, 0.01f);
    compFB.activate();
    auto outFB = processBlocks(compFB, input.data(), numBlocks, kBlockSize);

    // Compare output levels (skip settling blocks)
    float rmsFF = calculateRMS(outFF.data() + 5 * kBlockSize, (numBlocks - 5) * kBlockSize);
    float rmsFB = calculateRMS(outFB.data() + 5 * kBlockSize, (numBlocks - 5) * kBlockSize);

    if (std::abs(linearToDb(rmsFF) - linearToDb(rmsFB)) < 0.1f) {
        std::cerr << "FAILED: FF=" << linearToDb(rmsFF) << "dB, FB=" << linearToDb(rmsFB)
                  << "dB (too similar)\n";
        return false;
    }

    std::cout << "PASSED (FF=" << std::fixed << std::setprecision(1)
              << linearToDb(rmsFF) << "dB, FB=" << linearToDb(rmsFB) << "dB)\n";
    return true;
}

// =====================================================================
// T5: Range limits max gain reduction
// =====================================================================
bool testRangeLimit() {
    std::cout << "  [T5] Range limits GR... ";

    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    setDefaults(comp);

    // Heavy compression with range limit
    comp.setParameter(AestraComp::kThreshold, 0.3f);   // -42dB
    comp.setParameter(AestraComp::kRatio, 0.95f);      // ~19:1
    comp.setParameter(AestraComp::kAttack, 0.0f);
    comp.setParameter(AestraComp::kRelease, 0.3f);
    comp.setParameter(AestraComp::kKnee, 0.0f);
    comp.setParameter(AestraComp::kRange, 0.333f);     // -20dB max GR
    comp.activate();

    const uint32_t numBlocks = 20;
    const uint32_t totalFrames = numBlocks * kBlockSize;
    std::vector<float> input(totalFrames);
    generateTone(input.data(), totalFrames, 1000.0, 0.5f); // -6dB

    auto output = processBlocks(comp, input.data(), numBlocks, kBlockSize);

    float gr = comp.getCurrentGainReductionDb();

    // Without range limit: ~36dB of GR (input -6dB above threshold -42dB, ratio 19:1)
    // With range limit at -20dB: max 20dB
    if (gr > 21.0f) {
        std::cerr << "FAILED: GR=" << gr << "dB (exceeds 20dB range limit)\n";
        return false;
    }

    std::cout << "PASSED (GR=" << std::fixed << std::setprecision(1) << gr << "dB, capped at 20dB)\n";
    return true;
}

// =====================================================================
// T6: Sidechain HPF affects detection
// =====================================================================
bool testSCHPF() {
    std::cout << "  [T6] Sidechain HPF... ";

    const uint32_t numBlocks = 30;
    const uint32_t totalFrames = numBlocks * kBlockSize;

    // Low-frequency input (100Hz) — should be attenuated by HPF
    std::vector<float> input(totalFrames);
    generateTone(input.data(), totalFrames, 100.0, 0.5f);

    // Without SC HPF
    AestraComp compNoFilter;
    compNoFilter.initialize(kSampleRate, kBlockSize);
    setDefaults(compNoFilter);
    compNoFilter.setParameter(AestraComp::kThreshold, 0.6f);
    compNoFilter.setParameter(AestraComp::kRatio, 0.5f);
    compNoFilter.activate();
    processBlocks(compNoFilter, input.data(), numBlocks, kBlockSize);
    float grNoFilter = compNoFilter.getCurrentGainReductionDb();

    // With SC HPF at 500Hz
    AestraComp compWithFilter;
    compWithFilter.initialize(kSampleRate, kBlockSize);
    setDefaults(compWithFilter);
    compWithFilter.setParameter(AestraComp::kThreshold, 0.6f);
    compWithFilter.setParameter(AestraComp::kRatio, 0.5f);
    compWithFilter.setParameter(AestraComp::kSCHPF, 1.0f); // 500Hz HPF
    compWithFilter.activate();
    processBlocks(compWithFilter, input.data(), numBlocks, kBlockSize);
    float grWithFilter = compWithFilter.getCurrentGainReductionDb();

    // HPF should reduce GR (100Hz content attenuated before detection)
    if (grWithFilter >= grNoFilter) {
        std::cerr << "FAILED: GR without HPF=" << grNoFilter << "dB, with HPF=" << grWithFilter
                  << "dB (HPF should reduce GR)\n";
        return false;
    }

    std::cout << "PASSED (noHPF=" << std::fixed << std::setprecision(1)
              << grNoFilter << "dB, withHPF=" << grWithFilter << "dB)\n";
    return true;
}

// =====================================================================
// T7: Stereo link control works
// =====================================================================
bool testStereoLink() {
    std::cout << "  [T7] Stereo link... ";

    const uint32_t numBlocks = 30;
    const uint32_t totalFrames = numBlocks * kBlockSize;

    // Different signals L/R
    std::vector<float> inL(totalFrames), inR(totalFrames);
    generateTone(inL.data(), totalFrames, 1000.0, 0.5f); // -6dB
    generateTone(inR.data(), totalFrames, 1000.0, 0.1f); // -20dB

    // Fully linked
    AestraComp compLinked;
    compLinked.initialize(kSampleRate, kBlockSize);
    setDefaults(compLinked);
    compLinked.setParameter(AestraComp::kStereoLink, 1.0f); // 100%
    compLinked.setParameter(AestraComp::kThreshold, 0.5f);
    compLinked.setParameter(AestraComp::kRatio, 0.4f);
    compLinked.activate();
    auto [outLL, outLR] = processStereoBlocks(compLinked, inL.data(), inR.data(), numBlocks, kBlockSize);

    // Unlinked (dual mono)
    AestraComp compUnlinked;
    compUnlinked.initialize(kSampleRate, kBlockSize);
    setDefaults(compUnlinked);
    compUnlinked.setParameter(AestraComp::kStereoLink, 0.0f); // 0%
    compUnlinked.setParameter(AestraComp::kThreshold, 0.5f);
    compUnlinked.setParameter(AestraComp::kRatio, 0.4f);
    compUnlinked.activate();
    auto [outUL, outUR] = processStereoBlocks(compUnlinked, inL.data(), inR.data(), numBlocks, kBlockSize);

    // Skip settling blocks
    uint32_t skip = 5 * kBlockSize;
    uint32_t process = (numBlocks - 5) * kBlockSize;

    float rmsLL = calculateRMS(outLL.data() + skip, process);
    float rmsLR = calculateRMS(outLR.data() + skip, process);
    float rmsUL = calculateRMS(outUL.data() + skip, process);
    float rmsUR = calculateRMS(outUR.data() + skip, process);

    // Linked: L and R should have similar levels (same GR applied)
    // Unlinked: L and R should differ (different GR per channel)
    float linkedDiff = std::abs(linearToDb(rmsLL) - linearToDb(rmsLR));
    float unlinkedDiff = std::abs(linearToDb(rmsUL) - linearToDb(rmsUR));

    if (linkedDiff > 3.0f) {
        std::cerr << "FAILED: linked L/R diff=" << linkedDiff << "dB (should be small)\n";
        return false;
    }

    if (unlinkedDiff < 1.0f) {
        std::cerr << "FAILED: unlinked L/R diff=" << unlinkedDiff << "dB (should be larger)\n";
        return false;
    }

    std::cout << "PASSED (linkedDiff=" << std::fixed << std::setprecision(1)
              << linkedDiff << "dB, unlinkedDiff=" << unlinkedDiff << "dB)\n";
    return true;
}

// =====================================================================
// T8: Output trim applies correctly
// =====================================================================
bool testOutputTrim() {
    std::cout << "  [T8] Output trim... ";

    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    setDefaults(comp);
    comp.setParameter(AestraComp::kThreshold, 0.0f); // no compression
    comp.setParameter(AestraComp::kRatio, 0.0f);     // 1:1
    comp.setParameter(AestraComp::kMakeup, 0.0f);
    comp.setParameter(AestraComp::kOutputTrim, 0.75f); // +12dB
    comp.activate();

    const uint32_t numBlocks = 5;
    const uint32_t totalFrames = numBlocks * kBlockSize;
    std::vector<float> input(totalFrames);
    generateTone(input.data(), totalFrames, 1000.0, 0.25f); // -12dB

    auto output = processBlocks(comp, input.data(), numBlocks, kBlockSize);

    float inRms = calculateRMS(input.data() + kBlockSize, (numBlocks - 1) * kBlockSize);
    float outRms = calculateRMS(output.data() + kBlockSize, (numBlocks - 1) * kBlockSize);
    float gainDb = linearToDb(outRms) - linearToDb(inRms);

    if (std::abs(gainDb - 12.0f) > 2.0f) {
        std::cerr << "FAILED: trim gain=" << std::fixed << std::setprecision(1)
                  << gainDb << "dB (expected +12dB)\n";
        return false;
    }

    std::cout << "PASSED (gain=" << std::fixed << std::setprecision(1) << gainDb << "dB)\n";
    return true;
}

// =====================================================================
// T9: No zipper noise on all smoothed params
// =====================================================================
bool testNoZipperNoiseAllParams() {
    std::cout << "  [T9] No zipper on all params... ";

    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    setDefaults(comp);
    comp.activate();

    const uint32_t totalFrames = 48000; // 1 second
    std::vector<float> input(totalFrames);
    generateTone(input.data(), totalFrames, 1000.0, 0.1f);

    std::vector<float> output(totalFrames, 0.0f);
    const float* inPtr = input.data();
    float* outPtr = output.data();

    // Ramp threshold, ratio, knee, makeup, mix during playback
    for (uint32_t b = 0; b < totalFrames / kBlockSize; ++b) {
        float t = static_cast<float>(b) / (totalFrames / kBlockSize);
        comp.setParameter(AestraComp::kThreshold, 0.2f + t * 0.6f);
        comp.setParameter(AestraComp::kRatio, t);
        comp.setParameter(AestraComp::kKnee, t);
        comp.setParameter(AestraComp::kMakeup, t * 0.5f);
        comp.process(&inPtr, &outPtr, 1, 1, kBlockSize);
        inPtr += kBlockSize;
        outPtr += kBlockSize;
    }

    float maxJump = 0.0f;
    for (uint32_t i = 1; i < totalFrames; ++i) {
        float jump = std::abs(output[i] - output[i - 1]);
        if (jump > maxJump) maxJump = jump;
    }

    if (maxJump > 0.1f) {
        std::cerr << "FAILED: max jump=" << std::fixed << std::setprecision(4)
                  << maxJump << " (> 0.1)\n";
        return false;
    }

    std::cout << "PASSED (maxJump=" << std::fixed << std::setprecision(4) << maxJump << ")\n";
    return true;
}

// =====================================================================
// T10: Bypass still works with new params
// =====================================================================
bool testBypassStillWorks() {
    std::cout << "  [T10] Bypass passthrough... ";

    const uint32_t numBlocks = 4;
    const uint32_t totalFrames = numBlocks * kBlockSize;
    std::vector<float> input(totalFrames);
    generateTone(input.data(), totalFrames, 1000.0, 0.3f);

    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    setDefaults(comp);
    comp.setParameter(AestraComp::kBypass, 1.0f);
    comp.activate();

    auto output = processBlocks(comp, input.data(), numBlocks, kBlockSize);

    float maxDiff = 0.0f;
    for (uint32_t i = 0; i < totalFrames; ++i) {
        float diff = std::abs(output[i] - input[i]);
        if (diff > maxDiff) maxDiff = diff;
    }

    if (maxDiff > 1e-6f) {
        std::cerr << "FAILED: bypass diff=" << maxDiff << "\n";
        return false;
    }

    std::cout << "PASSED\n";
    return true;
}

} // namespace

int main() {
    std::cout << "AestraComp Phase 1 Tests\n";
    std::cout << "========================\n";

    int passed = 0;
    int total = 10;

    if (testNewParamsExist())       passed++;
    if (testStateRoundTrip())       passed++;
    if (testDetectorModes())        passed++;
    if (testTopologies())           passed++;
    if (testRangeLimit())           passed++;
    if (testSCHPF())                passed++;
    if (testStereoLink())           passed++;
    if (testOutputTrim())           passed++;
    if (testNoZipperNoiseAllParams()) passed++;
    if (testBypassStillWorks())     passed++;

    std::cout << "\n" << passed << "/" << total << " tests passed\n";
    return passed == total ? 0 : 1;
}
