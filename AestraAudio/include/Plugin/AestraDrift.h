// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraDrift V1 — real-time pitch shifter with dual-tap SOLA.

#pragma once

#include "Plugin/PluginHost.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Plugins {

class AestraDrift : public IPluginInstance {
public:
    enum Param : uint32_t {
        kPitch = 0,       // -12 to +12 semitones
        kGrain,           // 0% to 100% — grain/overlap size
        kMix,             // 0% to 100%
        kBypass,
        kParamCount
    };

    AestraDrift() {
        m_buffer.fill(0.0f);
        m_bufferR.fill(0.0f);
    }

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        (void)maxBlockSize;
        m_sampleRate = std::max(1.0, sampleRate);
        for (const auto& param : getParameters()) {
            m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
        }
        resetRuntimeState();
        return true;
    }

    void shutdown() override {}

    void activate() override {
        m_active.store(true, std::memory_order_relaxed);
        m_buffer.fill(0.0f);
        m_bufferR.fill(0.0f);
        resetRuntimeState();
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

        const uint32_t channels = std::min<uint32_t>(2, numOutputChannels);
        const bool stereo = channels >= 2 && numInputChannels >= 2;
        int writePos = m_writePos;
        float tapPhase[2] = { m_tapPhase[0], m_tapPhase[1] };
        const float baseDelay = m_baseDelay;

        const float twoPi = 2.0f * kPi;

        // Per-sample one-pole smoothing for the automatable params (Pitch, Mix),
        // so automation or fast knob moves glide instead of zippering. Coeff is
        // computed once per block; ~8 ms time constant.
        const float smoothCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(m_sampleRate) * 0.008f));
        // Once the smoother is within this of its target, snap exactly onto it.
        // A one-pole only asymptotes, so without this the residual decays into
        // denormal floats — a CPU penalty on the audio thread. 1e-6 is far below
        // the audible resolution of these normalized (0..1) params.
        constexpr float kSmoothSnapEps = 1.0e-6f;
        const float pitchTarget = m_params[kPitch].load(std::memory_order_relaxed);
        const float mixTarget = m_params[kMix].load(std::memory_order_relaxed);
        float pitchSmoothed = m_pitchSmoothed;
        float mixSmoothed = m_mixSmoothed;

        for (uint32_t i = 0; i < numFrames; ++i) {
            const float inL = (numInputChannels > 0 && inputs[0]) ? inputs[0][i] : 0.0f;
            const float inR = stereo ? ((numInputChannels > 1 && inputs[1]) ? inputs[1][i] : inL) : inL;

            m_buffer[writePos] = inL;
            m_bufferR[writePos] = inR;

            // Dual overlapping taps — always active, Hann-windowed.
            // When one tap wraps (phase crosses 0/grainSize), its window is ~0
            // and the other tap dominates. No audible discontinuity.
            float outL = 0.0f, outR = 0.0f;
            float winSum = 0.0f;
            for (int t = 0; t < 2; ++t) {
                const float phaseNorm = tapPhase[t] / kGrainSizeF;
                // Hann window: 0 at phase=0 and phase=grainSize, 1 at phase=grainSize/2
                const float win = 0.5f * (1.0f - std::cos(twoPi * phaseNorm));
                winSum += win;

                const float readPos = static_cast<float>(writePos) - baseDelay + tapPhase[t];
                const float sL = readBuffer(m_buffer, readPos);
                outL += sL * win;
                if (stereo) {
                    const float sR = readBuffer(m_bufferR, readPos);
                    outR += sR * win;
                }
            }
            // Normalize by window sum (should be ~1.0 for 50% overlap, but guard)
            const float norm = 1.0f / std::max(winSum, 0.001f);
            outL *= norm;
            outR *= norm;

            pitchSmoothed += smoothCoeff * (pitchTarget - pitchSmoothed);
            if (std::fabs(pitchTarget - pitchSmoothed) < kSmoothSnapEps) pitchSmoothed = pitchTarget;
            const float pitchNorm = pitchSmoothed;
            const float pitchSemitones = -12.0f + pitchNorm * 24.0f;
            const float pitchRatio = std::pow(2.0f, pitchSemitones / 12.0f);
            const float delta = 1.0f - pitchRatio;

            for (int t = 0; t < 2; ++t) {
                tapPhase[t] += delta;
                // Wrap: when one tap wraps, its Hann window is near 0
                if (tapPhase[t] >= kGrainSizeF) tapPhase[t] -= kGrainSizeF;
                if (tapPhase[t] < 0.0f) tapPhase[t] += kGrainSizeF;
            }

            writePos = (writePos + 1) & kBufferMask;

            mixSmoothed += smoothCoeff * (mixTarget - mixSmoothed);
            if (std::fabs(mixTarget - mixSmoothed) < kSmoothSnapEps) mixSmoothed = mixTarget;
            const float mix = mixSmoothed;
            const float wet = mix;
            const float dry = 1.0f - wet;

            if (numOutputChannels > 0 && outputs[0])
                outputs[0][i] = inL * dry + outL * wet;
            if (numOutputChannels > 1 && outputs[1])
                outputs[1][i] = (stereo ? inR : inL) * dry + outR * wet;
            for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                if (outputs[ch]) outputs[ch][i] = 0.0f;
            }
        }

        m_writePos = writePos;
        m_tapPhase[0] = tapPhase[0];
        m_tapPhase[1] = tapPhase[1];
        m_pitchSmoothed = pitchSmoothed;
        m_mixSmoothed = mixSmoothed;
    }

    uint32_t getParameterCount() const override { return kParamCount; }

    float getParameter(uint32_t id) const override {
        if (id >= kParamCount) return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    void setParameter(uint32_t id, float value) override {
        if (id >= kParamCount) return;
        if (!std::isfinite(value)) return; // NaN survives clamp and would poison the Pitch/Mix smoothers
        m_params[id].store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    std::vector<PluginParameter> getParameters() const override {
        return {
            { kPitch, "Pitch", "PIT", "st", 0.5f, 0.0f, 1.0f, true },
            { kGrain, "Grain", "GRN", "", 0.5f, 0.0f, 1.0f, true },
            { kMix, "Mix", "MIX", "%", 1.0f, 0.0f, 1.0f, true },
            { kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1 },
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount) return "";
        const float v = getParameter(id);
        switch (id) {
        case kPitch: {
            const float st = -12.0f + v * 24.0f;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%+.1f st", st);
            return buf;
        }
        case kGrain: {
            const float ms = grainMsFromNorm(v);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f ms", ms);
            return buf;
        }
        case kMix:
            return std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        case kBypass:
            return v > 0.5f ? "ON" : "OFF";
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
        struct Header { uint32_t magic; uint32_t version; };
        const auto* header = reinterpret_cast<const Header*>(state.data());
        if (header->magic != kStateMagic) return false;
        struct StateBlob { uint32_t magic; uint32_t version; float params[kParamCount]; };
        if (state.size() < sizeof(StateBlob)) return false;
        const auto* blob = reinterpret_cast<const StateBlob*>(state.data());
        for (uint32_t i = 0; i < kParamCount; ++i) setParameter(i, blob->params[i]);
        return true;
    }

    bool hasEditor() const override { return true; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {480, 290}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 2048; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

private:
    static constexpr uint32_t kStateMagic = 0x44524654;
    static constexpr int kBufferSize = 8192;
    static constexpr int kBufferMask = kBufferSize - 1;
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kGrainSizeF = 2048.0f;
    static constexpr float kBaseDelay = static_cast<float>(kBufferSize) * 0.20f;

    static float grainMsFromNorm(float norm) {
        return 5.0f + norm * 35.0f;
    }

    // Cubic Hermite interpolation
    static float readBuffer(const std::array<float, kBufferSize>& buf, float readPos) {
        const int i0 = static_cast<int>(std::floor(readPos)) & kBufferMask;
        const int i1 = (i0 + 1) & kBufferMask;
        const int i2 = (i1 + 1) & kBufferMask;
        const int i3 = (i2 + 1) & kBufferMask;
        const float frac = readPos - std::floor(readPos);
        const float a = buf[static_cast<size_t>(i3)] - buf[static_cast<size_t>(i2)]
                      + buf[static_cast<size_t>(i1)] - buf[static_cast<size_t>(i0)];
        const float b = buf[static_cast<size_t>(i0)] - buf[static_cast<size_t>(i1)] - a;
        const float c = buf[static_cast<size_t>(i2)] - buf[static_cast<size_t>(i0)];
        const float d = buf[static_cast<size_t>(i1)];
        return ((a * frac + b) * frac + c) * frac + d;
    }

    void resetRuntimeState() {
        m_writePos = 0;
        m_baseDelay = kBaseDelay;
        m_tapPhase[0] = 0.0f;
        m_tapPhase[1] = kGrainSizeF * 0.5f;
        // Snap smoothers to the current targets so load/activate doesn't glide.
        m_pitchSmoothed = m_params[kPitch].load(std::memory_order_relaxed);
        m_mixSmoothed = m_params[kMix].load(std::memory_order_relaxed);
    }

    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    std::atomic<bool> m_active{false};
    std::array<std::atomic<float>, kParamCount> m_params{};

    std::array<float, kBufferSize> m_buffer{};
    std::array<float, kBufferSize> m_bufferR{};
    int m_writePos = 0;
    float m_baseDelay = kBaseDelay;
    float m_tapPhase[2] = {0.0f, kGrainSizeF * 0.5f};

    // Smoothed automatable params. Drift reads Pitch/Mix per-sample; without
    // smoothing, automating Mix (a dry/wet gain crossfade) or Pitch zippers.
    // setParameter only stores the atomic target; process() ramps toward it,
    // matching the smoothing contract the other internal plugins follow.
    float m_pitchSmoothed = 0.5f;
    float m_mixSmoothed = 1.0f;
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
