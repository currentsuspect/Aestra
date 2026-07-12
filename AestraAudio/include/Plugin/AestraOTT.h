// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraOTT V1 — 3-band upward + downward compressor ("over-the-top" style).
//
// The input is split into low/mid/high with 4th-order Linkwitz-Riley
// crossovers (two cascaded Butterworth TPT-SVF stages per slope). The low
// band passes through an LR4 allpass at the high crossover so all three
// bands stay phase-aligned and sum flat when no gain is applied.
//
// Each band has a stereo-linked peak follower and a symmetric gain computer
// that pulls the band level toward a fixed -18 dBFS target: loud bands are
// compressed down, quiet bands are boosted up. Upward gain fades to zero
// below -50 dB so silence and noise floors are never dragged up. Depth
// scales the computed gain (in dB) toward unity — the wet control lives in
// the gain domain, not a dry/wet mix, so there is no phase-cancelling
// recombination with the allpass-rotated wet path.
//
// Signal path (per sample):
//   in -> input gain -> LR4 split (low/mid/high)
//      -> per-band gain computer (up+down toward -18 dB, scaled by Depth)
//      -> per-band trim -> sum -> output gain

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

class AestraOTT : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagic = 0x4F545431; // 'OTT1'
    static constexpr float kTargetDb = -18.0f;          // band level the computer pulls toward
    static constexpr float kRatioAmount = 0.6f;         // fraction of the distance to close
    static constexpr float kUpGateLowDb = -50.0f;       // upward gain is zero at/below this
    static constexpr float kUpGateHighDb = -40.0f;      // ...and fully active at/above this
    static constexpr float kMaxUpDb = 24.0f;            // hard cap on upward gain

    enum Param : uint32_t {
        kDepth = 0, // 0% to 100% wet (gain-domain)
        kTime,      // 0.1x to 10x time-constant scale (log)
        kInGain,    // -24 dB to +24 dB
        kOutGain,   // -24 dB to +24 dB
        kLowGain,   // -12 dB to +12 dB band trim
        kMidGain,   // -12 dB to +12 dB band trim
        kHighGain,  // -12 dB to +12 dB band trim
        kXoverLow,  // 60 Hz to 500 Hz (log)
        kXoverHigh, // 1 kHz to 8 kHz (log)
        kBypass,
        kParamCount,
    };

    enum Band : uint32_t { kBandLow = 0, kBandMid, kBandHigh, kBandCount };

    AestraOTT() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        (void)maxBlockSize;
        m_sampleRate = std::max(1.0, sampleRate);
        // EffectChain::prepare() re-calls initialize() on the *live* instance
        // during stream restarts / sample-rate changes, so seed defaults only on
        // the first init — otherwise a device change would silently wipe the
        // user's current parameter values.
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

        if (!m_active.load(std::memory_order_relaxed) || m_params[kBypass].load(std::memory_order_relaxed) > 0.5f) {
            copyOrClear(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            return;
        }

        const uint32_t channels = std::min<uint32_t>(2, numOutputChannels);
        const bool stereo = channels >= 2;
        const float sr = static_cast<float>(m_sampleRate);

        // Parameters are read/smoothed and all dB<->linear transforms run once
        // per control block; per-band gains are linearly interpolated across
        // the block. The per-sample path is the crossover SVFs, the peak
        // followers and multiply-adds — no atomics or transcendentals.
        for (uint32_t blockStart = 0; blockStart < numFrames; blockStart += kCtrlBlock) {
            const uint32_t blockEnd = std::min(blockStart + kCtrlBlock, numFrames);
            const uint32_t blockLen = blockEnd - blockStart;

            m_depthSmoothed += (getParameter(kDepth) - m_depthSmoothed) * m_smoothCoeff;
            m_timeSmoothed += (getParameter(kTime) - m_timeSmoothed) * m_smoothCoeff;
            m_inGainSmoothed += (getParameter(kInGain) - m_inGainSmoothed) * m_smoothCoeff;
            m_outGainSmoothed += (getParameter(kOutGain) - m_outGainSmoothed) * m_smoothCoeff;
            m_xoverLowSmoothed += (getParameter(kXoverLow) - m_xoverLowSmoothed) * m_smoothCoeff;
            m_xoverHighSmoothed += (getParameter(kXoverHigh) - m_xoverHighSmoothed) * m_smoothCoeff;
            for (uint32_t b = 0; b < kBandCount; ++b) {
                m_bandTrimSmoothed[b] += (getParameter(kLowGain + b) - m_bandTrimSmoothed[b]) * m_smoothCoeff;
            }

            // Crossover coefficients: recompute only when the smoothed target
            // actually moved — tan() is not free and xovers are near-static.
            if (std::abs(m_xoverLowSmoothed - m_xoverLowCached) > 1.0e-5f || m_coeffsDirty) {
                m_xoverLowCached = m_xoverLowSmoothed;
                m_coeffLow = makeCoeff(xoverLowHzFromNorm(m_xoverLowCached), sr);
            }
            if (std::abs(m_xoverHighSmoothed - m_xoverHighCached) > 1.0e-5f || m_coeffsDirty) {
                m_xoverHighCached = m_xoverHighSmoothed;
                m_coeffHigh = makeCoeff(xoverHighHzFromNorm(m_xoverHighCached), sr);
            }
            // Attack/release coefficients: same gating, driven by the Time macro.
            if (std::abs(m_timeSmoothed - m_timeCached) > 1.0e-4f || m_coeffsDirty) {
                m_timeCached = m_timeSmoothed;
                updateTimeCoeffs(sr);
            }

            const float inGain = dbToLinear(bipolarDbFromNorm(m_inGainSmoothed, 24.0f));
            const float outGain = dbToLinear(bipolarDbFromNorm(m_outGainSmoothed, 24.0f));
            const float depth = m_depthSmoothed;

            // ── Per-band gain targets from the current envelopes; the applied
            // gain ramps linearly to the target across the block. Envelopes
            // move over milliseconds, so a 16-sample ramp tracks them closely. ──
            float dGain[kBandCount];
            for (uint32_t b = 0; b < kBandCount; ++b) {
                const float envDb = linearToDb(m_env[b]);
                float compDb = (kTargetDb - envDb) * kRatioAmount;
                if (compDb > 0.0f) {
                    // Upward gain: gate away near the noise floor, cap the boost.
                    const float upScale =
                        std::clamp((envDb - kUpGateLowDb) / (kUpGateHighDb - kUpGateLowDb), 0.0f, 1.0f);
                    compDb = std::min(compDb * upScale, kMaxUpDb);
                }
                const float trimDb = bipolarDbFromNorm(m_bandTrimSmoothed[b], 12.0f);
                const float target = dbToLinear(depth * compDb + trimDb);
                m_bandGainDb[b].store(depth * compDb, std::memory_order_relaxed);
                if (m_coeffsDirty) {
                    m_bandGain[b] = target;
                    dGain[b] = 0.0f;
                } else {
                    dGain[b] = (target - m_bandGain[b]) / static_cast<float>(blockLen);
                }
            }
            m_coeffsDirty = false;

            for (uint32_t i = blockStart; i < blockEnd; ++i) {
                const float inL = sanitizeSample(readInput(inputs, numInputChannels, 0, i)) * inGain;
                const float inR = (stereo ? sanitizeSample(readInput(inputs, numInputChannels, 1, i)) : inL) * inGain;

                // ── LR4 band split (low band allpass-corrected at the high xover) ──
                float band[kBandCount][2];
                for (uint32_t ch = 0; ch < 2; ++ch) {
                    const float x = (ch == 0) ? inL : inR;
                    float lowPath, highPath;
                    m_xover1[ch].split(x, m_coeffLow, lowPath, highPath);
                    float apLo, apHi;
                    m_apCorrect[ch].split(lowPath, m_coeffHigh, apLo, apHi);
                    band[kBandLow][ch] = apLo + apHi; // LR4 allpass at fHigh
                    m_xover2[ch].split(highPath, m_coeffHigh, band[kBandMid][ch], band[kBandHigh][ch]);
                }

                float outL = 0.0f;
                float outR = 0.0f;
                for (uint32_t b = 0; b < kBandCount; ++b) {
                    const float peak = std::max(std::abs(band[b][0]), std::abs(band[b][1]));
                    float& env = m_env[b];
                    const float coeff = (peak > env) ? m_attackCoeff[b] : m_releaseCoeff[b];
                    env += (peak - env) * coeff;
                    if (env != env || env < 1.0e-12f)
                        env = 0.0f; // NaN check + denormal flush

                    m_bandGain[b] += dGain[b];
                    outL += band[b][0] * m_bandGain[b];
                    outR += band[b][1] * m_bandGain[b];
                }

                if (outputs[0])
                    outputs[0][i] = flushDenormal(outL * outGain);
                if (numOutputChannels > 1 && outputs[1])
                    outputs[1][i] = flushDenormal((stereo ? outR : outL) * outGain);
                for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                    if (outputs[ch])
                        outputs[ch][i] = 0.0f;
                }
            }
        }
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
            return;
        m_params[id].store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    std::vector<PluginParameter> getParameters() const override {
        return {
            {kDepth, "Depth", "DPT", "%", 1.0f, 0.0f, 1.0f, true},
            {kTime, "Time", "TIME", "x", 0.5f, 0.0f, 1.0f, true}, // center = 1x
            {kInGain, "Input Gain", "IN", "dB", 0.5f, 0.0f, 1.0f, true},
            {kOutGain, "Output Gain", "OUT", "dB", 0.5f, 0.0f, 1.0f, true},
            {kLowGain, "Low Gain", "LOW", "dB", 0.5f, 0.0f, 1.0f, true},
            {kMidGain, "Mid Gain", "MID", "dB", 0.5f, 0.0f, 1.0f, true},
            {kHighGain, "High Gain", "HIGH", "dB", 0.5f, 0.0f, 1.0f, true},
            {kXoverLow, "Low Crossover", "XLO", "Hz", 0.25f, 0.0f, 1.0f, true},   // ~102 Hz
            {kXoverHigh, "High Crossover", "XHI", "Hz", 0.44f, 0.0f, 1.0f, true}, // ~2.5 kHz
            {kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1},
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount)
            return "";
        const float v = getParameter(id);
        char buf[32];
        switch (id) {
        case kDepth:
            return std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        case kTime:
            std::snprintf(buf, sizeof(buf), "%.2fx", timeMultFromNorm(v));
            return buf;
        case kInGain:
        case kOutGain:
            std::snprintf(buf, sizeof(buf), "%+.1f dB", bipolarDbFromNorm(v, 24.0f));
            return buf;
        case kLowGain:
        case kMidGain:
        case kHighGain:
            std::snprintf(buf, sizeof(buf), "%+.1f dB", bipolarDbFromNorm(v, 12.0f));
            return buf;
        case kXoverLow:
            std::snprintf(buf, sizeof(buf), "%.0f Hz", xoverLowHzFromNorm(v));
            return buf;
        case kXoverHigh: {
            const float hz = xoverHighHzFromNorm(v);
            std::snprintf(buf, sizeof(buf), "%.2f kHz", hz / 1000.0f);
            return buf;
        }
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
    std::pair<int, int> getEditorSize() const override { return {560, 340}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override {
        // Sample-rate-relative so the reported tail is constant in time, not in
        // samples. The LR4 crossovers are Butterworth (non-resonant) so the
        // ring-out at the lowest 60 Hz split is short (~ln(1000)/(pi·60) ≈ 40 ms);
        // report ~150 ms to also cover the gain settle after input stops.
        return static_cast<uint32_t>(m_sampleRate * 0.15);
    }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

    // Metering (for the editor's band gain indicators): applied comp gain in dB.
    float getBandGainDb(uint32_t band) const {
        if (band >= kBandCount)
            return 0.0f;
        return m_bandGainDb[band].load(std::memory_order_relaxed);
    }

    // ── Parameter mapping (public: shared with editor/tests) ──
    static float timeMultFromNorm(float value) {
        // 0.1x .. 10x, log, center = 1x
        return 0.1f * std::pow(100.0f, std::clamp(value, 0.0f, 1.0f));
    }

    static float bipolarDbFromNorm(float value, float rangeDb) {
        return (std::clamp(value, 0.0f, 1.0f) * 2.0f - 1.0f) * rangeDb;
    }

    static float xoverLowHzFromNorm(float value) {
        // 60 Hz .. 500 Hz, log
        return 60.0f * std::pow(500.0f / 60.0f, std::clamp(value, 0.0f, 1.0f));
    }

    static float xoverHighHzFromNorm(float value) {
        // 1 kHz .. 8 kHz, log
        return 1000.0f * std::pow(8.0f, std::clamp(value, 0.0f, 1.0f));
    }

private:
    static constexpr uint32_t kCtrlBlock = 16; // control-rate interval for param/gain updates

    // ── TPT (ZDF) SVF stage, Butterworth damping — building block for LR4 ──
    struct SvfCoeff {
        float a1 = 0.0f;
        float a2 = 0.0f;
        float a3 = 0.0f;
        float k = 1.41421356f; // 1/Q for Butterworth
    };

    struct SvfStage {
        float ic1 = 0.0f;
        float ic2 = 0.0f;
        void reset() { ic1 = ic2 = 0.0f; }
        void step(float x, const SvfCoeff& c, float& lp, float& hp) {
            const float v3 = x - ic2;
            const float v1 = c.a1 * ic1 + c.a2 * v3;
            const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2;
            hp = x - c.k * v1 - v2;
        }
    };

    // LR4 crossover: LP4 = two cascaded Butterworth LP2, HP4 = two cascaded
    // Butterworth HP2. LP4 + HP4 sums to a 4th-order allpass (flat magnitude).
    struct CrossoverLR4 {
        SvfStage first;
        SvfStage lowSecond;
        SvfStage highSecond;
        void reset() {
            first.reset();
            lowSecond.reset();
            highSecond.reset();
        }
        void split(float x, const SvfCoeff& c, float& low, float& high) {
            float lp1, hp1;
            first.step(x, c, lp1, hp1);
            float unusedHp, unusedLp;
            lowSecond.step(lp1, c, low, unusedHp);
            highSecond.step(hp1, c, unusedLp, high);
        }
    };

    static SvfCoeff makeCoeff(float fc, float sr) {
        SvfCoeff c;
        // At extremely low sample rates 0.45*sr can fall below the 20 Hz floor,
        // which would make std::clamp's bounds invalid (lo > hi). Keep lo <= hi.
        const float hi = 0.45f * sr;
        const float clamped = std::clamp(fc, std::min(20.0f, hi), hi);
        const float g = std::tan(3.14159265358979323846f * clamped / sr);
        c.a1 = 1.0f / (1.0f + g * (g + c.k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }

    void updateTimeCoeffs(float sr) {
        // Base times per band (ms): lows breathe slower, highs snap faster.
        static constexpr float kAttackMs[kBandCount] = {24.0f, 10.0f, 3.0f};
        static constexpr float kReleaseMs[kBandCount] = {240.0f, 120.0f, 60.0f};
        const float mult = timeMultFromNorm(m_timeCached);
        for (uint32_t b = 0; b < kBandCount; ++b) {
            m_attackCoeff[b] = 1.0f - std::exp(-1.0f / (kAttackMs[b] * mult * 0.001f * sr));
            m_releaseCoeff[b] = 1.0f - std::exp(-1.0f / (kReleaseMs[b] * mult * 0.001f * sr));
        }
    }

    void resetRuntimeState() {
        for (uint32_t ch = 0; ch < 2; ++ch) {
            m_xover1[ch].reset();
            m_xover2[ch].reset();
            m_apCorrect[ch].reset();
        }
        for (uint32_t b = 0; b < kBandCount; ++b) {
            m_env[b] = 0.0f;
            m_bandGainDb[b].store(0.0f, std::memory_order_relaxed);
        }
        const float sr = static_cast<float>(m_sampleRate);
        // Smoothing runs once per control block, so the coefficient is scaled
        // to the block interval to keep the 2 ms time constant.
        m_smoothCoeff = 1.0f - std::exp(-static_cast<float>(kCtrlBlock) / (0.002f * sr));
        m_coeffsDirty = true;
    }

    void snapSmoothedParams() {
        m_depthSmoothed = getParameter(kDepth);
        m_timeSmoothed = getParameter(kTime);
        m_inGainSmoothed = getParameter(kInGain);
        m_outGainSmoothed = getParameter(kOutGain);
        m_xoverLowSmoothed = getParameter(kXoverLow);
        m_xoverHighSmoothed = getParameter(kXoverHigh);
        for (uint32_t b = 0; b < kBandCount; ++b) {
            m_bandTrimSmoothed[b] = getParameter(kLowGain + b);
        }
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

    static float flushDenormal(float value) {
        if (!std::isfinite(value))
            return 0.0f; // guard the output against NaN/Inf, not just denormals
        return std::abs(value) < 1.0e-20f ? 0.0f : value;
    }

    static float linearToDb(float linear) {
        if (!std::isfinite(linear) || linear <= 1.0e-6f)
            return -120.0f;
        return 20.0f * std::log10(linear);
    }

    static float dbToLinear(float db) { return std::exp(db * 0.11512925464970229f); }

    // ── State ──
    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_paramsInitialized{false};
    std::array<std::atomic<float>, kParamCount> m_params{};

    // Crossover network, per channel
    CrossoverLR4 m_xover1[2];    // at fLow: low path vs (mid+high) path
    CrossoverLR4 m_xover2[2];    // at fHigh: mid vs high
    CrossoverLR4 m_apCorrect[2]; // at fHigh: allpass correction on the low path

    SvfCoeff m_coeffLow;
    SvfCoeff m_coeffHigh;
    bool m_coeffsDirty = true;
    float m_xoverLowCached = -1.0f;
    float m_xoverHighCached = -1.0f;
    float m_timeCached = -1.0f;

    float m_env[kBandCount] = {};
    float m_attackCoeff[kBandCount] = {};
    float m_releaseCoeff[kBandCount] = {};
    float m_bandGain[kBandCount] = {1.0f, 1.0f, 1.0f}; // block-interpolated applied gains

    float m_smoothCoeff = 0.0f;
    float m_depthSmoothed = 1.0f;
    float m_timeSmoothed = 0.5f;
    float m_inGainSmoothed = 0.5f;
    float m_outGainSmoothed = 0.5f;
    float m_xoverLowSmoothed = 0.25f;
    float m_xoverHighSmoothed = 0.44f;
    float m_bandTrimSmoothed[kBandCount] = {0.5f, 0.5f, 0.5f};

    std::array<std::atomic<float>, kBandCount> m_bandGainDb{};
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
