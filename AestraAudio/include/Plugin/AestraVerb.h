// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraVerb — Algorithmic stereo reverb with predelay, decay, and damping.
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

// Schroeder reverb: 4 parallel comb filters → 2 all-pass filters in series
class AestraVerb : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagic = 0x52564201; // 'RVB' v1

    // Delay line sizes (prime numbers for good diffusion)
    static constexpr uint32_t kComb1 = 1116;
    static constexpr uint32_t kComb2 = 1188;
    static constexpr uint32_t kComb3 = 1277;
    static constexpr uint32_t kComb4 = 1356;
    static constexpr uint32_t kAllpass1 = 225;
    static constexpr uint32_t kAllpass2 = 556;
    static constexpr uint32_t kPredelay = 4800; // 100ms at 48kHz
    static constexpr uint32_t kBufferSize = kPredelay + 64;

    enum Param : uint32_t {
        kDecay = 0,    // 0.1 to 0.99
        kDamping,      // 0 to 1 (high-freq absorption)
        kPredelayMs,   // 0 to 200ms
        kWidth,        // stereo width 0 to 1
        kMix,          // wet/dry 0 to 1
        kBypass,
        kParamCount
    };

    AestraVerb() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        (void)maxBlockSize;
        m_sampleRate = sampleRate;
        const auto defaults = getParameters();
        for (const auto& param : defaults) {
            if (param.id < kParamCount) {
                m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
            }
        }
        // Initialize delay buffers
        for (auto& b : m_combBuf) b.assign(kBufferSize, 0.0f);
        for (auto& b : m_allpassBuf) b.assign(kAllpass2 + 64, 0.0f);
        m_combPos.assign(4, 0);
        m_allpassPos.assign(2, 0);
        m_predelayPos = 0;
        m_filtL = m_filtR = 0.0f;
        return true;
    }

    void shutdown() override {}
    void activate() override { m_active.store(true, std::memory_order_relaxed); clearBuffers(); }
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

        const float decay = 0.1f + m_params[kDecay].load(std::memory_order_relaxed) * 0.89f;
        const float damp = m_params[kDamping].load(std::memory_order_relaxed);
        const float width = m_params[kWidth].load(std::memory_order_relaxed);
        const float mix = m_params[kMix].load(std::memory_order_relaxed);

        const float combLens[4] = { kComb1, kComb2, kComb3, kComb4 };
        const float allpassLens[2] = { kAllpass1, kAllpass2 };
        const float feedback = decay;
        const float dampCoeff = damp;

        float filtL = m_filtL;
        float filtR = m_filtR;
        int combPos[4] = { m_combPos[0], m_combPos[1], m_combPos[2], m_combPos[3] };
        int allpassPos[2] = { m_allpassPos[0], m_allpassPos[1] };
        int predelayPos = m_predelayPos;

        for (uint32_t i = 0; i < numFrames; ++i) {
            float inL = (numInputChannels > 0 && inputs[0]) ? inputs[0][i] : 0.0f;
            float inR = (numInputChannels > 1 && inputs[1]) ? inputs[1][i] : inL;

            // Predelay buffer — ring buffer with separate read position
            m_predelay[predelayPos] = inL;
            m_predelay[predelayPos + kPredelay] = inR;

            int readPos = (predelayPos + 1) % kPredelay;
            float delayedL = m_predelay[readPos];
            float delayedR = m_predelay[readPos + kPredelay];
            predelayPos = (predelayPos + 1) % kPredelay;
            float dryL = inL;
            float dryR = inR;

            // Parallel comb filters (staggered for stereo)
            float combOutL = 0, combOutR = 0;
            for (int c = 0; c < 4; ++c) {
                int len = static_cast<int>(combLens[c]);
                float outL = m_combBuf[c][combPos[c]];
                float outR = m_combBuf[c][(combPos[c] + len / 3) % len];

                // Damping
                filtL = outL * (1.0f - dampCoeff) + filtL * dampCoeff;
                filtR = outR * (1.0f - dampCoeff) + filtR * dampCoeff;

                m_combBuf[c][combPos[c]] = delayedL + filtL * feedback;
                m_combBuf[c][(combPos[c] + len / 3) % len] = delayedR + filtR * feedback;
                combPos[c] = (combPos[c] + 1) % len;

                combOutL += outL;
                combOutR += outR;
            }

            // Scale comb output
            combOutL /= 4.0f;
            combOutR /= 4.0f;

            // Two all-pass filters in series
            for (int a = 0; a < 2; ++a) {
                int len = static_cast<int>(allpassLens[a]);
                float bufL = m_allpassBuf[a][allpassPos[a]];
                float bufR = m_allpassBuf[a][(allpassPos[a] + len / 3) % len];

                float outL = -combOutL + bufL;
                float outR = -combOutR + bufR;

                m_allpassBuf[a][allpassPos[a]] = combOutL + bufL * 0.5f;
                m_allpassBuf[a][(allpassPos[a] + len / 3) % len] = combOutR + bufR * 0.5f;
                allpassPos[a] = (allpassPos[a] + 1) % len;

                combOutL = outL;
                combOutR = outR;
            }

            // Stereo width
            float wetL = combOutL * (1.0f + width) + combOutR * (1.0f - width);
            float wetR = combOutR * (1.0f + width) + combOutL * (1.0f - width);
            wetL *= 0.25f;
            wetR *= 0.25f;

            // Wet/dry mix
            float outL = dryL * (1.0f - mix) + wetL * mix;
            float outR = dryR * (1.0f - mix) + wetR * mix;

            if (numOutputChannels > 0 && outputs[0]) outputs[0][i] = std::clamp(outL, -1.0f, 1.0f);
            if (numOutputChannels > 1 && outputs[1]) outputs[1][i] = std::clamp(outR, -1.0f, 1.0f);
        }

        m_filtL = filtL;
        m_filtR = filtR;
        m_combPos = { combPos[0], combPos[1], combPos[2], combPos[3] };
        m_allpassPos = { allpassPos[0], allpassPos[1] };
        m_predelayPos = predelayPos;
    }

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
            { kDecay, "Decay", "DEC", "", 0.5f, 0.0f, 1.0f, true },
            { kDamping, "Damping", "DMP", "", 0.3f, 0.0f, 1.0f, true },
            { kPredelayMs, "Predelay", "PRE", "ms", 0.1f, 0.0f, 1.0f, true },
            { kWidth, "Width", "WID", "", 0.7f, 0.0f, 1.0f, true },
            { kMix, "Mix", "MIX", "%", 0.3f, 0.0f, 1.0f, true },
            { kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1 },
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount) return "";
        float v = getParameter(id);
        switch (id) {
        case kDecay: return std::to_string(static_cast<int>((0.1f + v * 0.89f) * 100)) + "%";
        case kDamping: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kPredelayMs: return std::to_string(static_cast<int>(v * 200)) + "ms";
        case kWidth: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kMix: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kBypass: return v > 0.5f ? "ON" : "OFF";
        default: return "";
        }
    }

    std::vector<uint8_t> saveState() const override {
        struct Blob { uint32_t magic = kStateMagic; uint32_t version = 1; float params[kParamCount]; } blob;
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
    uint32_t getTailSamples() const override { return 4096; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

private:
    void clearBuffers() {
        for (auto& b : m_combBuf) std::fill(b.begin(), b.end(), 0.0f);
        for (auto& b : m_allpassBuf) std::fill(b.begin(), b.end(), 0.0f);
        std::fill(m_predelay.begin(), m_predelay.end(), 0.0f);
        m_filtL = m_filtR = 0.0f;
        m_combPos.assign(4, 0);
        m_allpassPos.assign(2, 0);
        m_predelayPos = 0;
    }

    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    std::atomic<bool> m_active{false};
    std::array<std::atomic<float>, kParamCount> m_params;

    std::array<std::vector<float>, 4> m_combBuf;
    std::array<std::vector<float>, 2> m_allpassBuf;
    std::vector<float> m_predelay = std::vector<float>(kPredelay * 2, 0.0f);
    std::vector<int> m_combPos = {0, 0, 0, 0};
    std::vector<int> m_allpassPos = {0, 0};
    int m_predelayPos = 0;
    float m_filtL = 0, m_filtR = 0;
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
