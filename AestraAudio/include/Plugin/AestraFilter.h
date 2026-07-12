// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraFilter V1 — creative resonant filter with envelope-follower modulation.
//
// Topology-preserving-transform (ZDF) state-variable filter (Cytomic/Simper
// form) so cutoff can be modulated per sample without zipper noise or
// instability. An input peak follower drives the cutoff bipolarly (up to
// ±4 octaves), which is what turns a static filter into an envelope shaper:
// auto-wah upward sweeps, reverse "duck" filtering, dynamic brightness.
//
// Control-rate architecture: parameters are read and smoothed once per
// 8-sample control block, transcendental coefficient recomputes (pow/exp/tan)
// are gated on smoothed-target movement, and the TPT coefficient set is
// linearly interpolated across the block. The per-sample path is the envelope
// follower, the drive tanh and the SVF arithmetic.
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
            m_wasBypassed = true;
            return;
        }
        if (m_wasBypassed) {
            // While bypassed the integrators and envelope stopped evolving;
            // re-enabling must not restore stale resonance/envelope state.
            m_ic1[0] = m_ic1[1] = 0.0f;
            m_ic2[0] = m_ic2[1] = 0.0f;
            m_envelope = 0.0f;
            snapSmoothedParams();
            m_coeffsDirty = true;
            snapTypeGains();
            m_wasBypassed = false;
        }

        const uint32_t channels = std::min<uint32_t>(2, numOutputChannels);
        const bool stereo = channels >= 2;
        const float sr = static_cast<float>(m_sampleRate);
        const float maxCutoff = 0.45f * sr;

        for (uint32_t blockStart = 0; blockStart < numFrames; blockStart += kCtrlInterval) {
            const uint32_t blockEnd = std::min(blockStart + kCtrlInterval, numFrames);
            const uint32_t blockLen = blockEnd - blockStart;

            // ── Control block: smooth params, gated coefficient recomputes ──
            m_cutoffSmoothed += (getParameter(kCutoff) - m_cutoffSmoothed) * m_smoothCoeff;
            m_resoSmoothed += (getParameter(kReso) - m_resoSmoothed) * m_smoothCoeff;
            m_driveSmoothed += (getParameter(kDrive) - m_driveSmoothed) * m_smoothCoeff;
            m_envAmtSmoothed += (getParameter(kEnvAmount) - m_envAmtSmoothed) * m_smoothCoeff;
            m_attackSmoothed += (getParameter(kEnvAttack) - m_attackSmoothed) * m_smoothCoeff;
            m_releaseSmoothed += (getParameter(kEnvRelease) - m_releaseSmoothed) * m_smoothCoeff;
            m_mixSmoothed += (getParameter(kMix) - m_mixSmoothed) * m_smoothCoeff;

            if (m_coeffsDirty || std::abs(m_resoSmoothed - m_resoCached) > 1.0e-5f) {
                m_resoCached = m_resoSmoothed;
                m_k = 1.0f / qFromNorm(m_resoCached);
            }
            if (m_coeffsDirty || std::abs(m_driveSmoothed - m_driveCached) > 1.0e-5f) {
                m_driveCached = m_driveSmoothed;
                m_driveGain = dbToLinear(driveDbFromNorm(m_driveCached));
                m_driveMakeup = 1.0f / std::sqrt(m_driveGain);
            }
            if (m_coeffsDirty || std::abs(m_attackSmoothed - m_attackCached) > 1.0e-4f) {
                m_attackCached = m_attackSmoothed;
                m_attackCoeff = 1.0f - std::exp(-1.0f / (attackMsFromNorm(m_attackCached) * 0.001f * sr));
            }
            if (m_coeffsDirty || std::abs(m_releaseSmoothed - m_releaseCached) > 1.0e-4f) {
                m_releaseCached = m_releaseSmoothed;
                m_releaseCoeff = 1.0f - std::exp(-1.0f / (releaseMsFromNorm(m_releaseCached) * 0.001f * sr));
            }
            if (m_coeffsDirty || std::abs(m_cutoffSmoothed - m_cutoffCached) > 1.0e-5f) {
                m_cutoffCached = m_cutoffSmoothed;
                m_baseCutoffHz = cutoffHzFromNorm(m_cutoffCached);
            }

            // Envelope-modulated TPT coefficient target for this block; the
            // set is linearly interpolated across the block so per-sample cost
            // is three adds instead of exp2 + tan.
            const float envAmount = 2.0f * m_envAmtSmoothed - 1.0f;
            const float modOctaves = envAmount * kEnvModOctaves * std::min(1.0f, m_envelope);
            const float fc = std::clamp(m_baseCutoffHz * std::exp2(modOctaves), 20.0f, maxCutoff);
            const float g = std::tan(3.14159265358979323846f * fc / sr);
            const float a1t = 1.0f / (1.0f + g * (g + m_k));
            const float a2t = g * a1t;
            const float a3t = g * a2t;
            float da1, da2, da3;
            if (m_coeffsDirty) {
                m_a1 = a1t;
                m_a2 = a2t;
                m_a3 = a3t;
                da1 = da2 = da3 = 0.0f;
                m_coeffsDirty = false;
            } else {
                const float inv = 1.0f / static_cast<float>(blockLen);
                da1 = (a1t - m_a1) * inv;
                da2 = (a2t - m_a2) * inv;
                da3 = (a3t - m_a3) * inv;
            }

            // Type is stepped; each of LP/BP/HP carries a gain that continuously
            // slews toward a one-hot target. A plain two-state crossfade drops
            // the in-progress blend when a switch interrupts a fade (rapid
            // LP->BP->HP), which can click; per-type gains morph through any
            // interruption because the current audible blend is the state.
            const uint32_t selType =
                static_cast<uint32_t>(typeFromNorm(m_params[kType].load(std::memory_order_relaxed)));

            const float wet = m_mixSmoothed;
            const float dry = 1.0f - wet;

            for (uint32_t i = blockStart; i < blockEnd; ++i) {
                m_a1 += da1;
                m_a2 += da2;
                m_a3 += da3;

                const float inL = sanitizeSample(readInput(inputs, numInputChannels, 0, i));
                const float inR = stereo ? sanitizeSample(readInput(inputs, numInputChannels, 1, i)) : inL;

                // ── Envelope follower (pre-drive stereo peak) ──
                const float peak = std::max(std::abs(inL), std::abs(inR));
                const float envCoeff = (peak > m_envelope) ? m_attackCoeff : m_releaseCoeff;
                m_envelope += (peak - m_envelope) * envCoeff;

                // Continuous per-type gain slew toward the one-hot selection.
                for (uint32_t t = 0; t < kTypeCount; ++t) {
                    const float target = (t == selType) ? 1.0f : 0.0f;
                    m_typeGain[t] += (target - m_typeGain[t]) * m_typeCoeff;
                }

                for (uint32_t ch = 0; ch < channels; ++ch) {
                    const float x = (ch == 0) ? inL : inR;
                    // Drive: transparent at 0, tanh soft clip as it rises.
                    // 1/sqrt(G) makeup keeps perceived loudness roughly level.
                    const float driven = x + m_driveCached * (std::tanh(m_driveGain * x) * m_driveMakeup - x);

                    float& ic1 = m_ic1[ch];
                    float& ic2 = m_ic2[ch];
                    const float v3 = driven - ic2;
                    const float v1 = m_a1 * ic1 + m_a2 * v3;
                    const float v2 = ic2 + m_a2 * ic1 + m_a3 * v3;
                    ic1 = 2.0f * v1 - ic1;
                    ic2 = 2.0f * v2 - ic2;

                    const float lp = v2;
                    const float bp = v1;
                    const float hp = driven - m_k * v1 - v2;
                    const float filtered =
                        m_typeGain[kTypeLowPass] * lp + m_typeGain[kTypeBandPass] * bp + m_typeGain[kTypeHighPass] * hp;

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
        if (!std::isfinite(value))
            return; // NaN survives clamp (comparisons are false) and would poison the SVF coefficients
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
        // Validate the entire decoded set before mutating anything. setParameter
        // silently drops non-finite values, so applying in place would leave a
        // half-updated state while still reporting success — fail atomically.
        for (uint32_t i = 0; i < kParamCount; ++i) {
            if (!std::isfinite(blob.params[i]) || blob.params[i] < 0.0f || blob.params[i] > 1.0f)
                return false;
        }
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
    uint32_t getTailSamples() const override {
        // Resonant ring-out is sample-rate-relative: a 2-pole resonator's -60 dB
        // decay is ~ln(1000)*Q/(pi*f0), so the worst case here (20 Hz cutoff,
        // Q 10) rings ~1.1 s. A fixed 4096 samples truncated that ring in
        // offline bounce and drifted with sample rate — report ~1.2 s scaled to
        // the current rate.
        return static_cast<uint32_t>(m_sampleRate * 1.2);
    }
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
    static constexpr uint32_t kCtrlInterval = 8; // control-rate interval for param/coefficient updates

    // Snap the per-type gains to the current one-hot selection so activation
    // and bypass->active transitions do not morph the filter type.
    void snapTypeGains() {
        const uint32_t sel = static_cast<uint32_t>(typeFromNorm(getParameter(kType)));
        for (uint32_t t = 0; t < kTypeCount; ++t)
            m_typeGain[t] = (t == sel) ? 1.0f : 0.0f;
    }

    void resetRuntimeState() {
        m_ic1[0] = m_ic1[1] = 0.0f;
        m_ic2[0] = m_ic2[1] = 0.0f;
        m_envelope = 0.0f;
        m_wasBypassed = false;
        m_coeffsDirty = true;
        const float sr = static_cast<float>(m_sampleRate);
        // Smoothing runs once per control block, so the coefficient is scaled
        // to the block interval to keep the 2 ms time constant.
        m_smoothCoeff = 1.0f - std::exp(-static_cast<float>(kCtrlInterval) / (0.002f * sr));
        m_typeCoeff = 1.0f - std::exp(-1.0f / (0.003f * sr)); // ~3 ms type morph (per sample)
        snapTypeGains();
        m_envLevel.store(0.0f, std::memory_order_relaxed);
    }

    void snapSmoothedParams() {
        m_cutoffSmoothed = getParameter(kCutoff);
        m_resoSmoothed = getParameter(kReso);
        m_driveSmoothed = getParameter(kDrive);
        m_envAmtSmoothed = getParameter(kEnvAmount);
        m_attackSmoothed = getParameter(kEnvAttack);
        m_releaseSmoothed = getParameter(kEnvRelease);
        m_mixSmoothed = getParameter(kMix);
        m_coeffsDirty = true;
    }

    // ── Utility ──
    static void copyOrClear(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                            uint32_t numOutputChannels, uint32_t numFrames) {
        for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
            if (outputs[ch] && ch < numInputChannels && inputs[ch]) {
                // Skip the copy when the host renders in place (out == in):
                // memcpy on aliased buffers is undefined and the data is already
                // where it needs to be.
                if (outputs[ch] != inputs[ch])
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
    bool m_wasBypassed = false;

    // Control-rate smoothed params
    float m_smoothCoeff = 0.0f;
    float m_cutoffSmoothed = 1.0f;
    float m_resoSmoothed = 0.116f;
    float m_driveSmoothed = 0.0f;
    float m_envAmtSmoothed = 0.5f;
    float m_attackSmoothed = 0.567f;
    float m_releaseSmoothed = 0.642f;
    float m_mixSmoothed = 1.0f;

    // Cached coefficient inputs (recompute gated on movement)
    bool m_coeffsDirty = true;
    float m_resoCached = -1.0f;
    float m_driveCached = -1.0f;
    float m_attackCached = -1.0f;
    float m_releaseCached = -1.0f;
    float m_cutoffCached = -1.0f;
    float m_k = 1.41421356f;
    float m_driveGain = 1.0f;
    float m_driveMakeup = 1.0f;
    float m_attackCoeff = 0.0f;
    float m_releaseCoeff = 0.0f;
    float m_baseCutoffHz = 20000.0f;

    // Interpolated TPT coefficients
    float m_a1 = 0.0f;
    float m_a2 = 0.0f;
    float m_a3 = 0.0f;

    // Continuous per-type morph: one gain per LP/BP/HP, slewed toward the
    // one-hot selection so switches (even mid-morph) never drop the blend.
    float m_typeGain[kTypeCount] = {1.0f, 0.0f, 0.0f};
    float m_typeCoeff = 0.0f;

    std::atomic<float> m_envLevel{0.0f};
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
