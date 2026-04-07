// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraComp — Dynamics compressor with lookahead and soft knee.
// Arsenal effect plugin for Aestra DAW.

#pragma once

#include "Plugin/PluginHost.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Plugins {

class AestraComp : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagic = 0x434D5001; // 'CMP' v1

    // Parameters
    enum Param : uint32_t {
        kThreshold = 0,  // -60dB to 0dB
        kRatio,          // 1:1 to 20:1
        kAttack,         // 0.1ms to 100ms
        kRelease,        // 10ms to 1000ms
        kMakeup,         // 0dB to +24dB
        kKnee,           // 0dB to 24dB
        kMix,            // 0% to 100%
        kBypass,
        kParamCount
    };

    AestraComp() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        m_envL = m_envR = 0.0f;
        m_lookaheadDelay = 0;
        updateConstants();
        return true;
    }

    void shutdown() override {}
    void activate() override { m_active.store(true, std::memory_order_relaxed); m_envL = m_envR = 0.0f; }
    void deactivate() override { m_active.store(false, std::memory_order_relaxed); }
    bool isActive() const override { return m_active.load(std::memory_order_relaxed); }

    void process(const float* const* inputs, float** outputs,
                 uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* midiInput = nullptr,
                 MidiBuffer* midiOutput = nullptr) override {
        (void)midiInput;
        (void)midiOutput;

        if (!m_active.load(std::memory_order_relaxed) ||
            m_params[kBypass].load(std::memory_order_relaxed) > 0.5f) {
            for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
                if (outputs[ch] && ch < numInputChannels && inputs[ch])
                    std::memcpy(outputs[ch], inputs[ch], numFrames * sizeof(float));
                else if (outputs[ch])
                    std::memset(outputs[ch], 0, numFrames * sizeof(float));
            }
            return;
        }

        const float threshold = getParameter(kThreshold);
        const float ratio = getParameter(kRatio);
        const float attack = getParameter(kAttack);
        const float release = getParameter(kRelease);
        const float makeup = getParameter(kMakeup);
        const float knee = getParameter(kKnee);
        const float mix = getParameter(kMix);

        const float thresholdDb = -60.0f + threshold * 60.0f;
        const float ratioVal = 1.0f + ratio * 19.0f;
        const float attackTime = 0.0001f + attack * 0.0999f;
        const float releaseTime = 0.01f + release * 0.99f;
        const float makeupGain = makeup * 24.0f;
        const float kneeWidth = knee * 24.0f;
        const float wetMix = mix;

        const float attackCoeff = std::exp(-1.0f / (m_sampleRate * attackTime));
        const float releaseCoeff = std::exp(-1.0f / (m_sampleRate * releaseTime));
        const float makeupLinear = std::pow(10.0f, makeupGain / 20.0f);

        const uint32_t channels = std::min(numInputChannels, numOutputChannels);
        const bool stereo = channels >= 2;

        float envL = m_envL;
        float envR = m_envR;

        for (uint32_t i = 0; i < numFrames; ++i) {
            // Input levels (dB)
            float inL = (stereo && inputs[0] && inputs[1]) ? inputs[0][i] : (inputs[0] ? inputs[0][i] : 0.0f);
            float inR = stereo ? (inputs[1] ? inputs[1][i] : 0.0f) : inL;

            float levelL = std::abs(inL);
            float levelR = std::abs(inR);
            float level = std::max(levelL, levelR);

            // Level to dB (clamp to -120dB floor)
            float levelDb = level > 1e-12f ? 20.0f * std::log10(level) : -120.0f;

            // Envelope follower (peak hold with smoothing)
            float attackC = levelDb > envL ? attackCoeff : releaseCoeff;
            envL = attackC * envL + (1.0f - attackC) * levelDb;
            envR = envL; // linked envelope for stereo coherence

            float envDb = std::max(envL, envR);

            // Compressor transfer function with soft knee
            float reductionDb = 0.0f;
            float diff = envDb - thresholdDb;

            if (kneeWidth > 0.0f && std::abs(diff) < kneeWidth * 0.5f) {
                // Soft knee region — quadratic curve
                float kneeDiff = diff + kneeWidth * 0.5f;
                reductionDb = -(1.0f - 1.0f / ratioVal) * kneeDiff * kneeDiff / (2.0f * kneeWidth);
            } else if (envDb > thresholdDb + kneeWidth * 0.5f) {
                // Above knee — full ratio
                reductionDb = -(envDb - thresholdDb - kneeWidth * 0.5f) * (1.0f - 1.0f / ratioVal)
                              - (1.0f - 1.0f / ratioVal) * kneeWidth * 0.25f;
            }
            // Below knee/threshold: no reduction

            // Apply gain reduction + makeup
            float gainDb = reductionDb + makeupGain;
            float gain = std::pow(10.0f, gainDb / 20.0f);
            gain = std::clamp(gain, 0.0f, 10.0f); // clamp to +20dB max boost

            // Wet/dry mix
            float outL = inL * (1.0f + (gain * makeupLinear - 1.0f) * wetMix);
            float outR = inR * (1.0f + (gain * makeupLinear - 1.0f) * wetMix);

            // Anti-clip
            outL = std::clamp(outL, -1.0f, 1.0f);
            outR = std::clamp(outR, -1.0f, 1.0f);

            if (outputs[0]) outputs[0][i] = outL;
            if (stereo && outputs[1]) outputs[1][i] = outR;
        }

        m_envL = envL;
        m_envR = envR;
    }

    // Parameters
    uint32_t getParameterCount() const override { return kParamCount; }

    float getParameter(uint32_t id) const override {
        if (id >= kParamCount) return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    void setParameter(uint32_t id, float value) override {
        if (id >= kParamCount) return;
        m_params[id].store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    std::vector<PluginParameter> getParameters() const override {
        return {
            { kThreshold, "Threshold", "THR", "dB", 0.5f, 0.0f, 1.0f, true },
            { kRatio, "Ratio", "RAT", ":1", 0.2f, 0.0f, 1.0f, true, false, false, 0 },
            { kAttack, "Attack", "ATK", "ms", 0.1f, 0.0f, 1.0f, true },
            { kRelease, "Release", "REL", "ms", 0.3f, 0.0f, 1.0f, true },
            { kMakeup, "Makeup", "MKP", "dB", 0.0f, 0.0f, 1.0f, true },
            { kKnee, "Knee", "KNE", "dB", 0.25f, 0.0f, 1.0f, true },
            { kMix, "Mix", "MIX", "%", 1.0f, 0.0f, 1.0f, true },
            { kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1 },
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount) return "";
        float v = getParameter(id);
        switch (id) {
        case kThreshold: { float db = -60.0f + v * 60.0f; return std::to_string(static_cast<int>(db)) + "dB"; }
        case kRatio: { float r = 1.0f + v * 19.0f; return std::to_string(static_cast<int>(r)) + ":1"; }
        case kAttack: { float ms = 0.1f + v * 99.9f; return std::to_string(static_cast<int>(ms)) + "ms"; }
        case kRelease: { float ms = 10.0f + v * 990.0f; return std::to_string(static_cast<int>(ms)) + "ms"; }
        case kMakeup: { float db = v * 24.0f; return "+" + std::to_string(static_cast<int>(db)) + "dB"; }
        case kKnee: { float db = v * 24.0f; return std::to_string(static_cast<int>(db)) + "dB"; }
        case kMix: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kBypass: return v > 0.5f ? "ON" : "OFF";
        default: return "";
        }
    }

    // State
    std::vector<uint8_t> saveState() const override {
        struct Blob {
            uint32_t magic = kStateMagic;
            uint32_t version = 1;
            float params[kParamCount];
        } blob;
        for (uint32_t i = 0; i < kParamCount; ++i) blob.params[i] = getParameter(i);
        const auto* data = reinterpret_cast<const uint8_t*>(&blob);
        return { data, data + sizeof(blob) };
    }

    bool loadState(const std::vector<uint8_t>& state) override {
        if (state.size() < sizeof(uint32_t) * 2) return false;
        struct StateBlob { uint32_t magic; uint32_t version; float params[kParamCount]; };
        const auto* blob = reinterpret_cast<const StateBlob*>(state.data());
        if (blob->magic != kStateMagic) return false;
        for (uint32_t i = 0; i < kParamCount; ++i) setParameter(i, blob->params[i]);
        return true;
    }

    bool hasEditor() const override { return false; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {800, 600}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 256; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

private:
    void updateConstants() {}

    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    uint32_t m_maxBlockSize = 512;
    uint32_t m_lookaheadDelay = 0;
    std::atomic<bool> m_active{false};

    std::array<std::atomic<float>, kParamCount> m_params;
    float m_envL = 0, m_envR = 0;
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
