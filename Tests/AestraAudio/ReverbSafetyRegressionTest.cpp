// AestraVerb safety and voicing regression checks.

#include "AestraVerb.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraVerb;

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr float kPi = 3.14159265358979323846f;

struct RenderStats {
    float peak = 0.0f;
    double rms = 0.0;
    double earlyHigh = 0.0;
    double lateHigh = 0.0;
    bool finite = true;
};

void setMode(AestraVerb& verb, int mode) {
    verb.setParameter(AestraVerb::kMode, mode == 0 ? 0.0f : (mode == 1 ? 0.5f : 1.0f));
}

void setUsableDefaults(AestraVerb& verb, int mode) {
    setMode(verb, mode);
    verb.setParameter(AestraVerb::kDecay, mode == 1 ? 0.66f : (mode == 2 ? 0.58f : 0.46f));
    verb.setParameter(AestraVerb::kDamping, mode == 1 ? 0.58f : (mode == 2 ? 0.50f : 0.54f));
    verb.setParameter(AestraVerb::kSize, mode == 1 ? 0.74f : (mode == 2 ? 0.54f : 0.38f));
    verb.setParameter(AestraVerb::kDiffusion, mode == 1 ? 0.66f : (mode == 2 ? 0.70f : 0.58f));
    verb.setParameter(AestraVerb::kModRate, 0.42f);
    verb.setParameter(AestraVerb::kModDepth, mode == 2 ? 0.24f : 0.20f);
    verb.setParameter(AestraVerb::kMix, 1.0f);
    verb.setParameter(AestraVerb::kWidth, mode == 1 ? 0.76f : 0.66f);
}

std::vector<float> makeBrightTransient(size_t frames, float hz) {
    std::vector<float> signal(frames, 0.0f);
    for (size_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float env = std::exp(-t / 0.018f);
        signal[i] = std::sin(2.0f * kPi * hz * t) * env * 0.95f;
    }
    return signal;
}

std::vector<float> makeSibilantVocal(size_t frames) {
    std::vector<float> signal(frames, 0.0f);
    for (size_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        float env = 1.0f;
        if (t < 0.03f) {
            env = t / 0.03f;
        } else if (t > 0.75f) {
            env = std::max(0.0f, (0.9f - t) / 0.15f);
        }
        const float body = std::sin(2.0f * kPi * 220.0f * t) * 0.55f;
        const float formant = std::sin(2.0f * kPi * 880.0f * t) * 0.18f;
        const float sibilance = std::sin(2.0f * kPi * 6800.0f * t) *
            std::exp(-std::abs(t - 0.18f) / 0.035f) * 0.22f;
        signal[i] = (body + formant + sibilance) * env;
    }
    return signal;
}

RenderStats render(AestraVerb& verb, const std::vector<float>& input, uint32_t blockSize) {
    std::vector<float> outL(input.size(), 0.0f);
    std::vector<float> outR(input.size(), 0.0f);
    std::vector<float> inR = input;

    for (size_t offset = 0; offset < input.size(); offset += blockSize) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(blockSize, input.size() - offset));
        const float* ins[2] = {input.data() + offset, inR.data() + offset};
        float* outs[2] = {outL.data() + offset, outR.data() + offset};
        verb.process(ins, outs, 2, 2, frames);
    }

    RenderStats stats;
    double sumSq = 0.0;
    const size_t earlyEnd = input.size() / 3;
    const size_t lateStart = input.size() * 2 / 3;
    for (size_t i = 0; i < input.size(); ++i) {
        const float l = outL[i];
        const float r = outR[i];
        if (!std::isfinite(l) || !std::isfinite(r)) {
            stats.finite = false;
        }
        stats.peak = std::max(stats.peak, std::max(std::abs(l), std::abs(r)));
        sumSq += static_cast<double>(l) * l + static_cast<double>(r) * r;
        if (i > 0) {
            const double high = std::abs(outL[i] - outL[i - 1]) + std::abs(outR[i] - outR[i - 1]);
            if (i < earlyEnd) {
                stats.earlyHigh += high;
            } else if (i >= lateStart) {
                stats.lateHigh += high;
            }
        }
    }
    stats.rms = std::sqrt(sumSq / std::max<size_t>(1, input.size() * 2));
    return stats;
}

bool require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

bool runTinyBlockChecks() {
    bool ok = true;
    for (uint32_t block : {1U, 2U, 3U, 7U, 16U}) {
        AestraVerb verb;
        verb.initialize(kSampleRate, block);
        setUsableDefaults(verb, 0);
        verb.activate();
        std::vector<float> input(257, 0.0f);
        input[0] = 1.0f;
        auto stats = render(verb, input, block);
        ok &= require(stats.finite, "tiny block render produced NaN/Inf");
        ok &= require(stats.peak < 8.0f, "tiny block render exceeded sane bounds");
    }
    return ok;
}

bool runParameterExtremeChecks() {
    bool ok = true;
    std::vector<float> input = makeBrightTransient(48000, 3200.0f);
    for (int mode = 0; mode < 3; ++mode) {
        for (float value : {0.0f, 1.0f}) {
            AestraVerb verb;
            verb.initialize(kSampleRate, 256);
            setMode(verb, mode);
            verb.setParameter(AestraVerb::kDecay, value);
            verb.setParameter(AestraVerb::kDamping, value);
            verb.setParameter(AestraVerb::kSize, value);
            verb.setParameter(AestraVerb::kDiffusion, value);
            verb.setParameter(AestraVerb::kModRate, value);
            verb.setParameter(AestraVerb::kModDepth, value);
            verb.setParameter(AestraVerb::kPredelayMs, value);
            verb.setParameter(AestraVerb::kMix, 1.0f);
            verb.activate();
            auto stats = render(verb, input, 64);
            ok &= require(stats.finite, "parameter extremes produced NaN/Inf");
            ok &= require(stats.peak < 8.0f, "parameter extremes exceeded sane bounds");
        }
    }
    return ok;
}

bool runModeSwitchAndDeterminismChecks() {
    bool ok = true;
    std::vector<float> input = makeSibilantVocal(48000);

    AestraVerb switching;
    switching.initialize(kSampleRate, 64);
    setUsableDefaults(switching, 0);
    switching.activate();
    std::vector<float> outL(input.size(), 0.0f), outR(input.size(), 0.0f), inR = input;
    for (size_t offset = 0; offset < input.size(); offset += 32) {
        setMode(switching, static_cast<int>((offset / 32) % 3));
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(32, input.size() - offset));
        const float* ins[2] = {input.data() + offset, inR.data() + offset};
        float* outs[2] = {outL.data() + offset, outR.data() + offset};
        switching.process(ins, outs, 2, 2, frames);
    }
    for (size_t i = 0; i < outL.size(); ++i) {
        ok &= require(std::isfinite(outL[i]) && std::isfinite(outR[i]), "mode switch produced NaN/Inf");
        if (!ok) break;
    }

    AestraVerb a;
    AestraVerb b;
    a.initialize(kSampleRate, 128);
    b.initialize(kSampleRate, 128);
    setUsableDefaults(a, 2);
    setUsableDefaults(b, 2);
    a.activate();
    b.activate();
    auto statsA = render(a, input, 128);
    auto statsB = render(b, input, 128);
    ok &= require(std::abs(statsA.peak - statsB.peak) < 1.0e-6f, "repeated render peak is not deterministic");
    ok &= require(std::abs(statsA.rms - statsB.rms) < 1.0e-8, "repeated render RMS is not deterministic");
    return ok;
}

bool runHighFrequencyBuildupChecks() {
    bool ok = true;
    for (int mode = 0; mode < 3; ++mode) {
        AestraVerb verb;
        verb.initialize(kSampleRate, 256);
        setUsableDefaults(verb, mode);
        verb.setParameter(AestraVerb::kDecay, mode == 1 ? 0.92f : 0.78f);
        verb.activate();
        auto stats = render(verb, makeBrightTransient(96000, 7200.0f), 128);
        ok &= require(stats.finite, "long bright transient produced NaN/Inf");
        ok &= require(stats.peak < 8.0f, "long bright transient exceeded sane bounds");
        ok &= require(stats.lateHigh < stats.earlyHigh * 0.85 + 1.0e-9, "late high-frequency energy is building up");
    }
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= runTinyBlockChecks();
    ok &= runParameterExtremeChecks();
    ok &= runModeSwitchAndDeterminismChecks();
    ok &= runHighFrequencyBuildupChecks();

    if (!ok) {
        return 1;
    }

    std::cout << "AestraVerb safety regression checks passed.\n";
    return 0;
}
