// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraFilter V1 — creative resonant filter with envelope-follower modulation.
//
// Topology-preserving-transform (ZDF) state-variable filter (Cytomic/Simper
// form) so cutoff can be modulated per sample without zipper noise or
// instability. An input peak follower drives the cutoff bipolarly (up to
// ±4 octaves), which is what turns a static filter into an envelope shaper:
// auto-wah upward sweeps, reverse "duck" filtering, dynamic brightness.
//
// Signal path (per channel, per sample):
//   in -> drive (transparent-at-zero tanh blend) -> ZDF SVF (LP/BP/HP)
//      -> mix with dry (zero latency — no compensation needed)
//
// The envelope is derived from the pre-drive stereo peak so drive changes
// never re-tune the sweep.

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

class AestraFilter : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagic = 0x464C5431; // 'FLT1'
    static constexpr float kEnvModOctaves = 4.0f;

    enum Param : uint32_t {
        kType = 0,   // 0=LP, 1=BP, 2=HP (stepped)
        kCutoff,     // 20 Hz to 20 kHz (log)
        kReso,       // Q 0.5 to 10 (log)
        kDrive,      // 0 to +18 dB pre-filter, transparent at 0
        kEnvAmount,  // -100% to +100% (bipolar cutoff modulation)
        kEnvAttack,  // 0.1 to 100 ms (log)
        kEnvRelease, // 5 to 1000 ms (log)
        kMix,        // 0% to 100%
        kBypass,
        kParamCount,
    };

    enum Type : uint32_t { kTypeLowPass = 0, kTypeBandPass, kTypeHighPass, kTypeCount };

    AestraFilter() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        (void)maxBlockSize;
        m_sampleRate = std::max(1.0, sampleRate);
        for (const auto& param : getParameters()) {
            m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
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

        if (!m_active.load(std::memory_order_relaxed) || m_params[kBypass].load(std::memory_order_relaxed) > 0.5f) {
            copyOrClear(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            return;
        }

        const uint32_t channels = std::min<uint32_t>(2, numOutputChannels);
        const bool stereo = channels >= 2;
        const auto type = typeFromNorm(m_params[kType].load(std::memory_order_relaxed));
        const float sr = static_cast<float>(m_sampleRate);
        const float maxCutoff = 0.45f * sr;

        for (uint32_t i = 0; i < numFrames; ++i) {
            // Smooth continuous params (2 ms one-pole, shared by both channels)
            m_cutoffSmoothed += (getParameter(kCutoff) - m_cutoffSmoothed) * m_smoothCoeff;
            m_resoSmoothed += (getParameter(kReso) - m_resoSmoothed) * m_smoothCoeff;
            m_driveSmoothed += (getParameter(kDrive) - m_driveSmoothed) * m_smoothCoeff;
            m_envAmtSmoothed += (getParameter(kEnvAmount) - m_envAmtSmoothed) * m_smoothCoeff;
            m_mixSmoothed += (getParameter(kMix) - m_mixSmoothed) * m_smoothCoeff;

            const float inL = sanitizeSample(readInput(inputs, numInputChannels, 0, i));
            const float inR = stereo ? sanitizeSample(readInput(inputs, numInputChannels, 1, i)) : inL;

            // ── Envelope follower (pre-drive stereo peak) ──
            const float peak = std::max(std::abs(inL), std::abs(inR));
            const float attackMs = attackMsFromNorm(getParameter(kEnvAttack));
            const float releaseMs = releaseMsFromNorm(getParameter(kEnvRelease));
            const float coeffMs = (peak > m_envelope) ? attackMs : releaseMs;
            const float envCoeff = 1.0f - std::exp(-1.0f / (coeffMs * 0.001f * sr));
            m_envelope += (peak - m_envelope) * envCoeff;

            // ── Cutoff with bipolar envelope modulation (octave domain) ──
            const float envAmount = 2.0f * m_envAmtSmoothed - 1.0f;
            const float baseCutoff = cutoffHzFromNorm(m_cutoffSmoothed);
            const float modOctaves = envAmount * kEnvModOctaves * std::min(1.0f, m_envelope);
            const float fc = std::clamp(baseCutoff * std::exp2(modOctaves), 20.0f, maxCutoff);

            // ── ZDF SVF coefficients (shared by both channels) ──
            const float g = std::tan(3.14159265358979323846f * fc / sr);
            const float q = qFromNorm(m_resoSmoothed);
            const float k = 1.0f / q;
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;

            // ── Drive: transparent at 0, tanh soft clip as it rises.
            // 1/sqrt(G) makeup keeps perceived loudness roughly level: small
            // signals gain sqrt(G) (+9 dB max), the clip ceiling sits at
            // 1/sqrt(G) (-9 dB max) instead of collapsing to 1/G. ──
            const float driveNorm = m_driveSmoothed;
            const float driveGain = dbToLinear(driveDbFromNorm(driveNorm));
            const float driveMakeup = 1.0f / std::sqrt(driveGain);

            const float wet = m_mixSmoothed;
            const float dry = 1.0f - wet;

            for (uint32_t ch = 0; ch < channels; ++ch) {
                const float x = (ch == 0) ? inL : inR;
                const float driven = x + driveNorm * (std::tanh(driveGain * x) * driveMakeup - x);

                float& ic1 = m_ic1[ch];
                float& ic2 = m_ic2[ch];
                const float v3 = driven - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;

                float filtered;
                switch (type) {
                case kTypeLowPass:
                    filtered = v2;
                    break;
                case kTypeBandPass:
                    filtered = v1;
                    break;
                default:
                    filtered = driven - k * v1 - v2;
                    break;
                }

                if (outputs[ch])
                    outputs[ch][i] = x * dry + filtered * wet;
            }

            if (!stereo && numOutputChannels > 1 && outputs[1] && outputs[0]) {
                outputs[1][i] = outputs[0][i];
            }
            for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                if (outputs[ch])
                    outputs[ch][i] = 0.0f;
            }
        }

        m_envLevel.store(std::min(1.0f, m_envelope), std::memory_order_relaxed);
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
        m_params[id].store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    std::vector<PluginParameter> getParameters() const override {
        return {
            {kType, "Type", "TYP", "", 0.0f, 0.0f, 1.0f, true, false, false, kTypeCount - 1},
            {kCutoff, "Cutoff", "CUT", "Hz", 1.0f, 0.0f, 1.0f, true},
            {kReso, "Resonance", "RES", "", 0.116f, 0.0f, 1.0f, true}, // ~Q 0.707
            {kDrive, "Drive", "DRV", "dB", 0.0f, 0.0f, 1.0f, true},
            {kEnvAmount, "Envelope", "ENV", "%", 0.5f, 0.0f, 1.0f, true},    // center = off
            {kEnvAttack, "Attack", "ATK", "ms", 0.567f, 0.0f, 1.0f, true},   // ~5 ms
            {kEnvRelease, "Release", "REL", "ms", 0.642f, 0.0f, 1.0f, true}, // ~150 ms
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
        case kType:
            switch (typeFromNorm(v)) {
            case kTypeLowPass:
                return "Low Pass";
            case kTypeBandPass:
                return "Band Pass";
            default:
                return "High Pass";
            }
        case kCutoff: {
            const float hz = cutoffHzFromNorm(v);
            if (hz >= 1000.0f) {
                std::snprintf(buf, sizeof(buf), "%.2f kHz", hz / 1000.0f);
            } else {
                std::snprintf(buf, sizeof(buf), "%.0f Hz", hz);
            }
            return buf;
        }
        case kReso:
            std::snprintf(buf, sizeof(buf), "Q %.2f", qFromNorm(v));
            return buf;
        case kDrive:
            std::snprintf(buf, sizeof(buf), "%.1f dB", driveDbFromNorm(v));
            return buf;
        case kEnvAmount:
            std::snprintf(buf, sizeof(buf), "%+d%%", static_cast<int>(std::round((2.0f * v - 1.0f) * 100.0f)));
            return buf;
        case kEnvAttack:
            std::snprintf(buf, sizeof(buf), "%.1f ms", attackMsFromNorm(v));
            return buf;
        case kEnvRelease:
            std::snprintf(buf, sizeof(buf), "%.0f ms", releaseMsFromNorm(v));
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
    std::pair<int, int> getEditorSize() const override { return {520, 320}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 4096; } // resonant ring-out
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

    // Metering (for the editor's envelope indicator)
    float getEnvelopeLevel() const { return m_envLevel.load(std::memory_order_relaxed); }

    // ── Parameter mapping (public: shared with editor/tests) ──
    static float cutoffHzFromNorm(float value) {
        // 20 Hz .. 20 kHz, log
        return 20.0f * std::pow(1000.0f, std::clamp(value, 0.0f, 1.0f));
    }

    static float qFromNorm(float value) {
        // 0.5 .. 10, log
        return 0.5f * std::pow(20.0f, std::clamp(value, 0.0f, 1.0f));
    }

    static float driveDbFromNorm(float value) { return std::clamp(value, 0.0f, 1.0f) * 18.0f; }

    static float attackMsFromNorm(float value) {
        // 0.1 .. 100 ms, log
        return 0.1f * std::pow(1000.0f, std::clamp(value, 0.0f, 1.0f));
    }

    static float releaseMsFromNorm(float value) {
        // 5 .. 1000 ms, log
        return 5.0f * std::pow(200.0f, std::clamp(value, 0.0f, 1.0f));
    }

    static Type typeFromNorm(float value) {
        return static_cast<Type>(std::min<uint32_t>(
            kTypeCount - 1,
            static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * static_cast<float>(kTypeCount - 1) + 0.5f)));
    }

    static float normFromType(Type type) { return static_cast<float>(type) / static_cast<float>(kTypeCount - 1); }

private:
    void resetRuntimeState() {
        m_ic1[0] = m_ic1[1] = 0.0f;
        m_ic2[0] = m_ic2[1] = 0.0f;
        m_envelope = 0.0f;
        const float sr = static_cast<float>(m_sampleRate);
        m_smoothCoeff = 1.0f - std::exp(-1.0f / (0.002f * sr));
        m_envLevel.store(0.0f, std::memory_order_relaxed);
    }

    void snapSmoothedParams() {
        m_cutoffSmoothed = getParameter(kCutoff);
        m_resoSmoothed = getParameter(kReso);
        m_driveSmoothed = getParameter(kDrive);
        m_envAmtSmoothed = getParameter(kEnvAmount);
        m_mixSmoothed = getParameter(kMix);
    }

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

    // ── State ──
    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    std::atomic<bool> m_active{false};
    std::array<std::atomic<float>, kParamCount> m_params{};

    // ZDF SVF integrator state, per channel
    float m_ic1[2] = {0.0f, 0.0f};
    float m_ic2[2] = {0.0f, 0.0f};

    float m_envelope = 0.0f;

    float m_smoothCoeff = 0.0f;
    float m_cutoffSmoothed = 1.0f;
    float m_resoSmoothed = 0.116f;
    float m_driveSmoothed = 0.0f;
    float m_envAmtSmoothed = 0.5f;
    float m_mixSmoothed = 1.0f;

    std::atomic<float> m_envLevel{0.0f};
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
