// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraTransient V1 — feed-forward transient shaper (envelope-follower attack/decay
// gain scaling). Zero latency: output[i] depends only on inputs up to i, no
// lookahead, no reported latency, no PDC.
//
// Signal path (mono-linked detector, per-channel gain application):
//   in -> |x| -> fast envelope (peaks) + slow envelope (body)
//      -> normalized difference -> tanh-bounded gain computer
//      -> smoothed gain -> output trim -> mix with dry
//
// The fast envelope tracks onsets within ~1 ms while the slow envelope holds the
// steady body, so their normalized difference is positive on an attack portion
// and negative on a decay portion. The Attack amount scales the gain on the
// attack portion, the Sustain amount scales it on the decay portion; both are
// bipolar around neutral.

#pragma once

#include "Plugin/PluginHost.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Plugins {

class AestraTransient : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagic = 0x54524E31; // 'TRN1'

    enum Param : uint32_t {
        kAttack = 0, // onset amount, 0.5 = neutral (bipolar)
        kSustain,    // decay amount, 0.5 = neutral (bipolar)
        kOutput,     // -12 to +12 dB trim
        kMix,        // 0% to 100%
        kBypass,
        kParamCount,
    };

    // Detector time constants (seconds). Fixed by design: the workhorse brief
    // exposes only the two amounts, not the detector tuning.
    static constexpr float kFastAttackSec = 0.001f;
    static constexpr float kFastReleaseSec = 0.008f;
    static constexpr float kSlowAttackSec = 0.025f;
    static constexpr float kSlowReleaseSec = 0.120f;
    static constexpr float kMaxSwingDb = 12.0f;
    static constexpr float kGainSmoothSec = 0.002f;
    static constexpr float kEnvFloor = 1.0e-6f;

    AestraTransient() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        (void)maxBlockSize;
        m_sampleRate = std::max(1.0, sampleRate);
        // Seed parameter defaults only on the first initialization of a fresh
        // instance. EffectChain::prepare() re-calls initialize() on the live
        // instance during sample-rate/device changes and must preserve the
        // user's current parameter values (and any loaded project state).
        if (!m_paramsInitialized.exchange(true)) {
            for (const auto& param : getParameters()) {
                m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
            }
        }
        resetRuntimeState();
        snapSmoothedParams();
        return true;
    }

    void shutdown() override {}

    void activate() override {
        m_active.store(true, std::memory_order_relaxed);
        resetRuntimeState();
        snapSmoothedParams();
    }

    void deactivate() override { m_active.store(false, std::memory_order_relaxed); }
    bool isActive() const override { return m_active.load(std::memory_order_relaxed); }

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* midiInput = nullptr, MidiBuffer* midiOutput = nullptr) override {
        (void)midiInput;
        (void)midiOutput;

        // Zero reported latency means internal bypass is a plain copy.
        if (!m_active.load(std::memory_order_relaxed) || m_params[kBypass].load(std::memory_order_relaxed) > 0.5f) {
            copyOrClear(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            m_wasBypassed = true;
            return;
        }
        if (m_wasBypassed) {
            // Flush stale envelope/gain state from before the bypass so
            // reactivation does not emit it.
            resetRuntimeState();
            snapSmoothedParams();
            m_wasBypassed = false;
        }

        const uint32_t channels = std::min<uint32_t>(2, numOutputChannels);
        const bool stereo = channels >= 2;

        float blockInputPeak = 0.0f;
        float blockOutputPeak = 0.0f;

        float fast = m_fast;
        float slow = m_slow;
        float gainSm = m_gainSmoothed;

        for (uint32_t blockStart = 0; blockStart < numFrames; blockStart += kCtrlBlock) {
            const uint32_t blockEnd = std::min(blockStart + kCtrlBlock, numFrames);

            m_attackSmoothed += (getParameter(kAttack) - m_attackSmoothed) * m_smoothCoeff;
            m_sustainSmoothed += (getParameter(kSustain) - m_sustainSmoothed) * m_smoothCoeff;
            m_outputSmoothed += (getParameter(kOutput) - m_outputSmoothed) * m_smoothCoeff;
            m_mixSmoothed += (getParameter(kMix) - m_mixSmoothed) * m_smoothCoeff;

            const float amtA = bipolarFromNorm(m_attackSmoothed);
            const float amtS = bipolarFromNorm(m_sustainSmoothed);
            const float outGain = dbToLinear(outputDbFromNorm(m_outputSmoothed));
            const float wet = std::clamp(m_mixSmoothed, 0.0f, 1.0f);
            const float dry = 1.0f - wet;

            for (uint32_t i = blockStart; i < blockEnd; ++i) {
                const float inL = sanitizeSample(readInput(inputs, numInputChannels, 0, i));
                const float inR = stereo ? sanitizeSample(readInput(inputs, numInputChannels, 1, i)) : inL;
                blockInputPeak = std::max(blockInputPeak, std::max(std::abs(inL), std::abs(inR)));

                const float abs = std::max(std::abs(inL), std::abs(inR));
                fast += (abs - fast) * (abs > fast ? m_fastAttackCoeff : m_fastReleaseCoeff);
                slow += (abs - slow) * (abs > slow ? m_slowAttackCoeff : m_slowReleaseCoeff);

                // Normalized envelope difference: positive on an attack
                // portion, negative on a decay portion, bounded via tanh.
                const float diff = (fast - slow) / std::max(slow, kEnvFloor);
                const float shaped = std::tanh(diff);
                const float gainDb = (diff >= 0.0f ? amtA * shaped : amtS * (-shaped)) * kMaxSwingDb;
                const float gain = dbToLinear(gainDb);

                // One-pole smoothing of the applied gain prevents zipper noise
                // when the gain computer jumps between attack and decay.
                gainSm += (gain - gainSm) * m_gainSmoothCoeff;
                if (!std::isfinite(gainSm) || gainSm < 1.0e-12f)
                    gainSm = 1.0f; // NaN guard + denormal flush; unity is the safe state

                const float outL = flushDenormal((inL * gainSm * outGain) * wet + inL * dry);
                const float outR = flushDenormal((inR * gainSm * outGain) * wet + inR * dry);
                blockOutputPeak = std::max(blockOutputPeak, std::max(std::abs(outL), std::abs(outR)));

                if (numOutputChannels > 0 && outputs[0])
                    outputs[0][i] = outL;
                if (numOutputChannels > 1 && outputs[1])
                    outputs[1][i] = stereo ? outR : outL;
                for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                    if (outputs[ch])
                        outputs[ch][i] = 0.0f;
                }
            }
        }

        m_fast = flushEnvelope(fast);
        m_slow = flushEnvelope(slow);
        m_gainSmoothed = gainSm;
        m_inputLevel.store(blockInputPeak, std::memory_order_relaxed);
        m_outputLevel.store(blockOutputPeak, std::memory_order_relaxed);
    }

    uint32_t getParameterCount() const override { return kParamCount; }

    float getParameter(uint32_t id) const override {
        if (id >= kParamCount)
            return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    void setParameter(uint32_t id, float value) override {
        if (id >= kParamCount)
            return;
        if (!std::isfinite(value))
            return; // NaN survives clamp (comparisons are false) and would poison smoothing
        m_params[id].store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    std::vector<PluginParameter> getParameters() const override {
        return {
            {kAttack, "Attack", "ATK", "", 0.5f, 0.0f, 1.0f, true},
            {kSustain, "Sustain", "SUS", "", 0.5f, 0.0f, 1.0f, true},
            {kOutput, "Output", "OUT", "dB", 0.5f, 0.0f, 1.0f, true},
            {kMix, "Mix", "MIX", "%", 1.0f, 0.0f, 1.0f, true},
            {kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1},
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount)
            return "";
        const float v = getParameter(id);
        char buf[32];
        switch (id) {
        case kAttack: {
            const float amt = bipolarFromNorm(v);
            std::snprintf(buf, sizeof(buf), "%+.0f%%", amt * 100.0f);
            return buf;
        }
        case kSustain: {
            const float amt = bipolarFromNorm(v);
            std::snprintf(buf, sizeof(buf), "%+.0f%%", amt * 100.0f);
            return buf;
        }
        case kOutput:
            std::snprintf(buf, sizeof(buf), "%+.1f dB", outputDbFromNorm(v));
            return buf;
        case kMix:
            return std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        case kBypass:
            return v > 0.5f ? "ON" : "OFF";
        default:
            return "";
        }
    }

    std::vector<uint8_t> saveState() const override {
        struct Blob {
            uint32_t magic = kStateMagic;
            uint32_t version = 1;
            float params[kParamCount] = {};
        } blob;
        for (uint32_t i = 0; i < kParamCount; ++i) {
            blob.params[i] = getParameter(i);
        }
        const auto* data = reinterpret_cast<const uint8_t*>(&blob);
        return {data, data + sizeof(blob)};
    }

    bool loadState(const std::vector<uint8_t>& state) override {
        if (state.size() < sizeof(uint32_t) * 2)
            return false;
        uint32_t magic = 0;
        std::memcpy(&magic, state.data(), sizeof(magic));
        if (magic != kStateMagic)
            return false;
        struct Blob {
            uint32_t magic;
            uint32_t version;
            float params[kParamCount];
        };
        if (state.size() < sizeof(Blob))
            return false;
        Blob blob{};
        std::memcpy(&blob, state.data(), sizeof(blob));
        if (blob.version < 1 || blob.version > 1)
            return false;
        for (uint32_t i = 0; i < kParamCount; ++i) {
            setParameter(i, blob.params[i]);
        }
        return true;
    }

    bool hasEditor() const override { return true; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {480, 300}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 0; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

    // Metering
    float getInputLevel() const { return m_inputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return m_outputLevel.load(std::memory_order_relaxed); }

    // ── Parameter mapping (public: shared with editor/tests) ──
    static float bipolarFromNorm(float value) { return std::clamp(value, 0.0f, 1.0f) * 2.0f - 1.0f; }

    static float outputDbFromNorm(float value) { return -12.0f + std::clamp(value, 0.0f, 1.0f) * 24.0f; }

private:
    static constexpr uint32_t kCtrlBlock = 16; // control-rate interval for parameter smoothing

    void resetRuntimeState() {
        m_fast = 0.0f;
        m_slow = 0.0f;
        m_gainSmoothed = 1.0f;
        m_wasBypassed = false;
        const float sr = static_cast<float>(m_sampleRate);
        // Per-sample one-pole coefficients from the fixed detector constants.
        m_fastAttackCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, sr * kFastAttackSec));
        m_fastReleaseCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, sr * kFastReleaseSec));
        m_slowAttackCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, sr * kSlowAttackSec));
        m_slowReleaseCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, sr * kSlowReleaseSec));
        m_gainSmoothCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, sr * kGainSmoothSec));
        // Smoothing runs once per control block, so the coefficient is scaled
        // to the block interval to keep the 2 ms time constant.
        m_smoothCoeff = 1.0f - std::exp(-static_cast<float>(kCtrlBlock) / (0.002f * sr));
        m_inputLevel.store(0.0f, std::memory_order_relaxed);
        m_outputLevel.store(0.0f, std::memory_order_relaxed);
    }

    void snapSmoothedParams() {
        m_attackSmoothed = getParameter(kAttack);
        m_sustainSmoothed = getParameter(kSustain);
        m_outputSmoothed = getParameter(kOutput);
        m_mixSmoothed = getParameter(kMix);
    }

    static float flushEnvelope(float value) { return (!std::isfinite(value) || value < 1.0e-12f) ? 0.0f : value; }

    // ── Utility ──
    static void copyOrClear(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                            uint32_t numOutputChannels, uint32_t numFrames) {
        for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
            if (outputs[ch] && ch < numInputChannels && inputs[ch]) {
                std::memcpy(outputs[ch], inputs[ch], numFrames * sizeof(float));
            } else if (outputs[ch]) {
                std::memset(outputs[ch], 0, numFrames * sizeof(float));
            }
        }
    }

    static float readInput(const float* const* inputs, uint32_t channels, uint32_t channel, uint32_t frame) {
        if (channel >= channels || !inputs[channel]) {
            return (channel == 1 && channels > 0 && inputs[0]) ? inputs[0][frame] : 0.0f;
        }
        return inputs[channel][frame];
    }

    static float sanitizeSample(float sample) {
        if (!std::isfinite(sample))
            return 0.0f;
        return std::clamp(sample, -16.0f, 16.0f);
    }

    static float dbToLinear(float db) { return std::exp(db * 0.11512925464970229f); }

    static float flushDenormal(float value) { return std::abs(value) < 1.0e-20f ? 0.0f : value; }

    // ── State ──
    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_paramsInitialized{false};
    std::array<std::atomic<float>, kParamCount> m_params{};

    float m_fast = 0.0f;
    float m_slow = 0.0f;
    float m_gainSmoothed = 1.0f;
    bool m_wasBypassed = false;

    float m_fastAttackCoeff = 0.0f;
    float m_fastReleaseCoeff = 0.0f;
    float m_slowAttackCoeff = 0.0f;
    float m_slowReleaseCoeff = 0.0f;
    float m_gainSmoothCoeff = 0.0f;
    float m_smoothCoeff = 0.0f;

    float m_attackSmoothed = 0.5f;
    float m_sustainSmoothed = 0.5f;
    float m_outputSmoothed = 0.5f;
    float m_mixSmoothed = 1.0f;

    std::atomic<float> m_inputLevel{0.0f};
    std::atomic<float> m_outputLevel{0.0f};
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
