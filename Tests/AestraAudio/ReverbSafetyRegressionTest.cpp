// AestraVerb safety and voicing regression checks.

#include "AestraVerb.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
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

// mode: 0 = Room, 1 = Hall, 2 = Plate. Select via the canonical modeParam() so
// these actually land on Room/Hall/Plate; the old 0.0/0.5/1.0 constants decoded
// to Room/Chamber/SmoothPlate.
void setMode(AestraVerb& verb, int mode) {
    const AestraVerb::Mode m =
        mode == 0 ? AestraVerb::Mode::Room : (mode == 1 ? AestraVerb::Mode::Hall : AestraVerb::Mode::Plate);
    verb.setParameter(AestraVerb::kMode, AestraVerb::modeParam(m));
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

bool runActiveLoadStateSafetyCheck() {
    AestraVerb verb;
    verb.initialize(kSampleRate, 128);
    setUsableDefaults(verb, 1);
    verb.activate();

    const std::vector<uint8_t> state = verb.saveState();
    std::atomic<bool> stop{false};
    std::atomic<bool> renderOk{true};

    std::thread audioThread([&]() {
        std::vector<float> inL(128, 0.0f);
        std::vector<float> inR(128, 0.0f);
        std::vector<float> outL(128, 0.0f);
        std::vector<float> outR(128, 0.0f);
        inL[0] = 0.5f;
        inR[0] = 0.5f;

        while (!stop.load(std::memory_order_acquire)) {
            const float* inputs[2] = {inL.data(), inR.data()};
            float* outputs[2] = {outL.data(), outR.data()};
            verb.process(inputs, outputs, 2, 2, static_cast<uint32_t>(inL.size()));
            for (size_t i = 0; i < outL.size(); ++i) {
                if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) {
                    renderOk.store(false, std::memory_order_release);
                    stop.store(true, std::memory_order_release);
                    break;
                }
            }
            inL[0] = 0.0f;
            inR[0] = 0.0f;
        }
    });

    bool ok = true;
    for (int i = 0; i < 1000; ++i) {
        ok &= require(verb.loadState(state), "active state load failed");
        if (!ok || !renderOk.load(std::memory_order_acquire)) {
            break;
        }
    }

    stop.store(true, std::memory_order_release);
    audioThread.join();

    ok &= require(renderOk.load(std::memory_order_acquire), "active state load render produced NaN/Inf");
    return ok;
}

// Every one of the nine modes, selected via the canonical modeParam(), must
// render finite, bounded audio. Guards against a mode index that aliases or
// clamps to a neighbor and against any single mode blowing up.
bool runAllNineModesFiniteChecks() {
    bool ok = true;
    std::vector<float> input = makeBrightTransient(48000, 4000.0f);
    for (int mode = 0; mode < AestraVerb::kModeCount; ++mode) {
        AestraVerb verb;
        verb.initialize(kSampleRate, 256);
        verb.setParameter(AestraVerb::kMode, AestraVerb::modeParam(mode));
        verb.setParameter(AestraVerb::kDecay, 0.7f);
        verb.setParameter(AestraVerb::kSize, 0.6f);
        verb.setParameter(AestraVerb::kDiffusion, 0.7f);
        verb.setParameter(AestraVerb::kModRate, 0.42f);
        verb.setParameter(AestraVerb::kModDepth, 0.2f);
        verb.setParameter(AestraVerb::kMix, 1.0f);
        verb.setParameter(AestraVerb::kWidth, 0.7f);
        verb.activate();
        auto stats = render(verb, input, 128);
        ok &= require(stats.finite, "nine-mode sweep produced NaN/Inf");
        ok &= require(stats.peak < 8.0f, "nine-mode sweep exceeded sane bounds");
    }
    return ok;
}

} // namespace

// N3: the synced-predelay display must tell the truth. The buffer caps at
// ~500 ms, so long note values at slow tempos can't be realized; the display
// must show the real resulting time and flag when it is capped rather than a
// nominal division the engine silently clamps.
bool runPredelaySyncDisplayCheck() {
    AestraVerb verb;
    verb.initialize(kSampleRate, 256);
    verb.setBPM(120.0f);
    verb.activate();
    auto display = [&](int idx) {
        verb.setParameter(AestraVerb::kPredelaySync, static_cast<float>(idx) / 6.0f);
        return verb.getParameterDisplay(AestraVerb::kPredelaySync);
    };
    bool ok = true;
    // At 120 BPM: 1/4 = 500 ms (right at the cap), 1/2 = 1000 ms and longer must
    // be flagged as capped; short values must show their true time.
    const std::string q = display(3);  // 1/4
    const std::string h = display(4);  // 1/2
    const std::string bar = display(5); // 1 bar
    if (q.find("500ms") == std::string::npos || q.find("max") != std::string::npos) {
        std::cerr << "FAIL: 1/4 @120BPM should show ~500ms and not be capped: '" << q << "'\n"; ok = false;
    }
    if (h.find("max") == std::string::npos) {
        std::cerr << "FAIL: 1/2 @120BPM (1000ms) should be flagged capped: '" << h << "'\n"; ok = false;
    }
    if (bar.find("max") == std::string::npos) {
        std::cerr << "FAIL: 1 bar @120BPM (2000ms) should be flagged capped: '" << bar << "'\n"; ok = false;
    }
    if (display(0) != "OFF") { std::cerr << "FAIL: sync OFF display\n"; ok = false; }
    if (ok) std::cout << "Predelay sync display is truthful (1/4='" << q << "', 1/2='" << h << "').\n";
    return ok;
}

bool runParameterDisplayFormattingCheck() {
    AestraVerb verb;
    verb.initialize(kSampleRate, 256);
    verb.setParameter(AestraVerb::kMode, AestraVerb::modeParam(AestraVerb::Mode::Room));
    verb.setParameter(AestraVerb::kDecay, 0.56f);
    verb.setParameter(AestraVerb::kSize, 0.52f);
    verb.setParameter(AestraVerb::kModDepth, 0.07f);

    bool ok = true;
    for (uint32_t id : {AestraVerb::kDecay, AestraVerb::kSize, AestraVerb::kModDepth}) {
        const std::string display = verb.getParameterDisplay(id);
        ok &= require(display.find(".000000") == std::string::npos,
                      "parameter display leaked raw std::to_string precision: '" + display + "'");
    }
    ok &= require(verb.getParameterDisplay(AestraVerb::kSize) == "1.08x",
                  "size display should use compact precision");
    ok &= require(verb.getParameterDisplay(AestraVerb::kModDepth) == "1.1 smp",
                  "mod-depth display should use compact precision");
    if (ok) {
        std::cout << "Parameter displays use compact precision (size='"
                  << verb.getParameterDisplay(AestraVerb::kSize) << "', depth='"
                  << verb.getParameterDisplay(AestraVerb::kModDepth) << "').\n";
    }
    return ok;
}

int main() {
    bool ok = true;
    ok &= runTinyBlockChecks();
    ok &= runParameterExtremeChecks();
    ok &= runModeSwitchAndDeterminismChecks();
    ok &= runHighFrequencyBuildupChecks();
    ok &= runActiveLoadStateSafetyCheck();
    ok &= runAllNineModesFiniteChecks();
    ok &= runPredelaySyncDisplayCheck();
    ok &= runParameterDisplayFormattingCheck();

    if (!ok) {
        return 1;
    }

    std::cout << "AestraVerb safety regression checks passed.\n";
    return 0;
}
