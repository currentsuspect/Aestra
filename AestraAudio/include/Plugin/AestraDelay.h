// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraDelay — Stereo delay with feedback, filtering, and LFO modulation.
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

class AestraDelay : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagic = 0x444C5901; // 'DLY' v1
    static constexpr uint32_t kMaxDelaySec = 2;
    static constexpr uint32_t kMaxSamplesAt48k = kMaxDelaySec * 48000;
    // Runtime-resized buffers in initialize()

    enum Param : uint32_t {
        kTime = 0,     // 10ms to 2000ms
        kFeedback,     // 0 to 0.95
        kDamping,      // 0 to 1 (high-freq absorption on feedback)
        kStereoShift,  // -1 to 1 (delay time offset between L/R)
        kModDepth,     // 0 to 1 (LFO modulation depth)
        kModRate,      // 0.1Hz to 10Hz
        kMix,          // wet/dry 0 to 1
        kBypass,
        kParamCount
    };

    AestraDelay() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        (void)maxBlockSize;
        m_sampleRate = sampleRate;
        const auto defaults = getParameters();
        for (const auto& param : defaults) {
            if (param.id < kParamCount) {
                m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
            }
        }
        // Size buffer for max delay time at actual sample rate
        uint32_t maxSamples = static_cast<uint32_t>(kMaxDelaySec * sampleRate);
        m_bufL.assign(maxSamples, 0.0f);
        m_bufR.assign(maxSamples, 0.0f);
        m_posL = m_posR = 0;
        m_lfoPhase = 0.0f;
        m_filtL = m_filtR = 0.0f;
        return true;
    }

    void shutdown() override {}
    void activate() override {
        m_active.store(true, std::memory_order_relaxed);
        std::fill(m_bufL.begin(), m_bufL.end(), 0.0f);
        std::fill(m_bufR.begin(), m_bufR.end(), 0.0f);
        m_posL = m_posR = 0;
        m_filtL = m_filtR = 0.0f;
    }
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

        const float time = 0.01f + m_params[kTime].load(std::memory_order_relaxed) * 1.99f; // seconds
        const float fb = m_params[kFeedback].load(std::memory_order_relaxed) * 0.95f;
        const float damp = m_params[kDamping].load(std::memory_order_relaxed);
        const float stereoShift = m_params[kStereoShift].load(std::memory_order_relaxed) * 2.0f - 1.0f; // -1 to 1
        const float modDepth = m_params[kModDepth].load(std::memory_order_relaxed);
        const float modRate = 0.1f + m_params[kModRate].load(std::memory_order_relaxed) * 9.9f;
        const float mix = m_params[kMix].load(std::memory_order_relaxed);

        const int baseDelaySamples = static_cast<int>(time * m_sampleRate);
        const float maxShift = std::min(baseDelaySamples / 2, 500); // ±500 samples max shift
        const int shiftL = static_cast<int>(stereoShift * maxShift);
        const int shiftR = -shiftL;

        constexpr float pi = 3.14159265358979323846f;
        float lfoPhase = m_lfoPhase;
        float filtL = m_filtL;
        float filtR = m_filtR;
        int posL = m_posL;
        int posR = m_posR;
        const int bufSize = static_cast<int>(m_bufL.size());

        for (uint32_t i = 0; i < numFrames; ++i) {
            float inL = (numInputChannels > 0 && inputs[0]) ? inputs[0][i] : 0.0f;
            float inR = (numInputChannels > 1 && inputs[1]) ? inputs[1][i] : inL;

            // LFO modulation
            lfoPhase += modRate / static_cast<float>(m_sampleRate);
            if (lfoPhase > 1.0f) lfoPhase -= 1.0f;
            float lfo = std::sin(2.0f * pi * lfoPhase) * modDepth * maxShift;

            // Read from delay line with fractional delay (linear interpolation)
            int readPosL = (posL - baseDelaySamples - shiftL - static_cast<int>(lfo)) % bufSize;
            int readPosR = (posR - baseDelaySamples - shiftR + static_cast<int>(lfo)) % bufSize;
            if (readPosL < 0) readPosL += bufSize;
            if (readPosR < 0) readPosR += bufSize;

            float delayOutL = m_bufL[readPosL];
            float delayOutR = m_bufR[readPosR];

            // Feedback with damping (low-pass filter on feedback path)
            float fbL = delayOutL * (1.0f - damp) + filtL * damp;
            float fbR = delayOutR * (1.0f - damp) + filtR * damp;
            filtL = fbL;
            filtR = fbR;

            // Write input + feedback
            m_bufL[posL] = inL + fbL * fb;
            m_bufR[posR] = inR + fbR * fb;

            posL = (posL + 1) % bufSize;
            posR = (posR + 1) % bufSize;

            // Wet/dry mix
            float outL = inL * (1.0f - mix) + delayOutL * mix;
            float outR = inR * (1.0f - mix) + delayOutR * mix;

            if (numOutputChannels > 0 && outputs[0]) outputs[0][i] = std::clamp(outL, -1.0f, 1.0f);
            if (numOutputChannels > 1 && outputs[1]) outputs[1][i] = std::clamp(outR, -1.0f, 1.0f);
        }

        m_lfoPhase = lfoPhase;
        m_filtL = filtL;
        m_filtR = filtR;
        m_posL = posL;
        m_posR = posR;
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
            { kTime, "Time", "TIME", "s", 0.25f, 0.0f, 1.0f, true },
            { kFeedback, "Feedback", "FB", "", 0.3f, 0.0f, 1.0f, true },
            { kDamping, "Damping", "DMP", "", 0.2f, 0.0f, 1.0f, true },
            { kStereoShift, "Stereo", "STR", "", 0.5f, 0.0f, 1.0f, true },
            { kModDepth, "Mod Depth", "MOD", "", 0.0f, 0.0f, 1.0f, true },
            { kModRate, "Mod Rate", "RATE", "Hz", 0.1f, 0.0f, 1.0f, true },
            { kMix, "Mix", "MIX", "%", 0.3f, 0.0f, 1.0f, true },
            { kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1 },
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount) return "";
        float v = getParameter(id);
        switch (id) {
        case kTime: { float s = 0.01f + v * 1.99f; return s < 1.0f ? std::to_string(static_cast<int>(s * 1000)) + "ms" : std::to_string(s).substr(0, 4) + "s"; }
        case kFeedback: return std::to_string(static_cast<int>(v * 95)) + "%";
        case kDamping: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kStereoShift: { float s = v * 2.0f - 1.0f; return (s >= 0 ? "+" : "") + std::to_string(static_cast<int>(s * 100)) + "%"; }
        case kModDepth: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kModRate: { float r = 0.1f + v * 9.9f; return std::to_string(r).substr(0, 4) + "Hz"; }
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
    uint32_t getTailSamples() const override { return static_cast<uint32_t>(m_bufL.size()) / 4; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

private:
    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    std::atomic<bool> m_active{false};
    std::array<std::atomic<float>, kParamCount> m_params;

    std::vector<float> m_bufL;
    std::vector<float> m_bufR;
    int m_posL = 0, m_posR = 0;
    float m_lfoPhase = 0.0f;
    float m_filtL = 0, m_filtR = 0;
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
