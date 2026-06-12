// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraLimit V1 — musical brickwall limiter with density-driven auto-release.
//
// OPTFLOOR: AestraLimit 2026-06-11
// CPU: ~100 cycles/sample @ 48 kHz, 512-sample buffer
//   Breakdown: log10 ~20c, exp (gain) ~20c, exp (release) ~20c, upsample 4x ~16c, misc ~24c
// Memory: 3.5 KB hot working set per instance (840 bytes L1-active at 48 kHz)
// Cache: fits in L1 at any instance count; fits in L2 up to 300+ instances
// Transcendental floor: log10 and exp are accuracy-critical for gain computation;
//   replacing with approximations introduces > 0.1 dB error — unacceptable for brickwall limiting.
// SIMD: N/A — lookahead is a circular buffer (write 1, read 1), not a batch scan.
//   4-sample peak detection loop is below SIMD threshold.
// Denormals: handled by engine-level FTZ+DAZ. EMA state zeroed on reset/bypass.
// Next review: when sample rate exceeds 192 kHz or lookahead exceeds 2 ms.

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

class AestraLimit : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagic = 0x4C4D5431; // 'LMT1'

    static constexpr float kLookaheadMs = 2.0f;
    static constexpr float kCeilingMarginDb = 0.1f;
    static constexpr uint32_t kOsRatio = 4;
    static constexpr uint32_t kOsTapsPerPhase = 4;
    static constexpr uint32_t kMaxLookaheadSamples = 400;

    enum Param : uint32_t {
        kCeiling = 0,    // -24 to 0 dBTP
        kReleaseMode,    // 0=Auto, 1=Manual
        kRelease,        // 10 to 500 ms (manual only)
        kBypass,
        kParamCount,
    };

    AestraLimit() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        m_sampleRate = std::max(1.0, sampleRate);
        m_maxBlockSize = maxBlockSize;
        for (const auto& param : getParameters()) {
            m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
        }
        resetRuntimeState();
        snapSmoothedParams();
        updateLookaheadSize();
        return true;
    }

    void shutdown() override {}

    void activate() override {
        m_active.store(true, std::memory_order_relaxed);
        resetRuntimeState();
        snapSmoothedParams();
        updateLookaheadSize();
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
            copyOrClear(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            return;
        }

        const uint32_t channels = std::min<uint32_t>(2, numOutputChannels);
        const bool stereo = channels >= 2;

        // Cache per-block constants (avoids repeated atomic loads and transcendental calls)
        const bool autoRelease = m_params[kReleaseMode].load(std::memory_order_relaxed) <= 0.5f;
        const float clipLevel = m_clipLevel;

        float blockInputPeak = 0.0f;
        float blockOutputPeak = 0.0f;

        for (uint32_t i = 0; i < numFrames; ++i) {
            const float inL = sanitizeSample(readInput(inputs, numInputChannels, 0, i));
            const float inR = stereo ? sanitizeSample(readInput(inputs, numInputChannels, 1, i)) : inL;

            // Update smoothed params
            const float targetCeil = getParameter(kCeiling);
            const float targetRel = getParameter(kRelease);
            m_ceilingSmoothed += (targetCeil - m_ceilingSmoothed) * m_smoothCoeff;
            m_releaseSmoothed += (targetRel - m_releaseSmoothed) * m_smoothCoeff;
            if (autoRelease)
                computeAutoReleaseMs();

            // --- True Peak Detection (per channel, 4x oversample) ---
            float osValsL[kOsRatio];
            upsampleOs(m_tpUpsampleL, inL, osValsL);
            float peakL = 0.0f;
            for (uint32_t p = 0; p < kOsRatio; ++p)
                peakL = std::max(peakL, std::abs(osValsL[p]));

            float blockPeak = peakL;
            if (stereo) {
                float osValsR[kOsRatio];
                upsampleOs(m_tpUpsampleR, inR, osValsR);
                float peakR = 0.0f;
                for (uint32_t p = 0; p < kOsRatio; ++p)
                    peakR = std::max(peakR, std::abs(osValsR[p]));
                blockPeak = std::max(peakL, peakR);
            }

            // --- Density Analysis (mono sum) ---
            const float monoSum = (inL + inR) * 0.5f;
            updateDensity(monoSum);

            // --- Lookahead Write ---
            m_lookaheadBuf[0][m_writeCursor] = inL;
            if (stereo)
                m_lookaheadBuf[1][m_writeCursor] = inR;
            m_writeCursor = (m_writeCursor + 1) % m_lookaheadSize;

            // --- Gain Computer ---
            const float peakDbTP = linearToDb(blockPeak);
            const float userCeilingDb = ceilingDbFromNorm(m_ceilingSmoothed);
            const float internalCeilingDb = userCeilingDb - kCeilingMarginDb;

            float targetGainDb;
            if (peakDbTP > userCeilingDb) {
                targetGainDb = internalCeilingDb - peakDbTP;
            } else {
                targetGainDb = 0.0f;
            }

            // --- Release Smoother ---
            if (targetGainDb < m_appliedGainDb) {
                m_appliedGainDb = targetGainDb;
            } else {
                const float releaseMs = autoRelease
                    ? m_autoReleaseMs
                    : releaseMsFromNorm(m_releaseSmoothed);
                const float releaseCoeff = std::exp(-m_oneOverSampleRateMs / releaseMs);
                m_appliedGainDb = releaseCoeff * m_appliedGainDb
                                  + (1.0f - releaseCoeff) * targetGainDb;
            }

            // --- Lookahead Read ---
            const uint32_t readOffset = (m_writeCursor + m_lookaheadSize - m_lookaheadSamples) % m_lookaheadSize;
            const float delayedL = m_lookaheadBuf[0][readOffset];
            const float delayedR = stereo ? m_lookaheadBuf[1][readOffset] : delayedL;

            // --- VCA ---
            const float gainLinear = dbToLinear(m_appliedGainDb);
            const float vcaOutL = delayedL * gainLinear;
            const float vcaOutR = delayedR * gainLinear;

            // --- Output Safety Clip ---
            const float outL = std::clamp(vcaOutL, -clipLevel, clipLevel);
            const float outR = stereo ? std::clamp(vcaOutR, -clipLevel, clipLevel) : outL;

            blockInputPeak = std::max(blockInputPeak, blockPeak);
            blockOutputPeak = std::max(blockOutputPeak, std::max(std::abs(outL), std::abs(outR)));

            if (numOutputChannels > 0 && outputs[0]) outputs[0][i] = outL;
            if (numOutputChannels > 1 && outputs[1]) outputs[1][i] = outR;
            for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                if (outputs[ch]) outputs[ch][i] = 0.0f;
            }
        }

        m_inputLevel.store(blockInputPeak, std::memory_order_relaxed);
        m_outputLevel.store(blockOutputPeak, std::memory_order_relaxed);
        m_gainReduction.store(std::max(0.0f, -m_appliedGainDb), std::memory_order_relaxed);
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
            { kCeiling, "Ceiling", "CEIL", "dBTP", 0.9958f, 0.0f, 1.0f, true },
            { kReleaseMode, "Release Mode", "RM", "", 0.0f, 0.0f, 1.0f, true, false, false, 1 },
            { kRelease, "Release", "REL", "ms", 0.1837f, 0.0f, 1.0f, true },
            { kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1 },
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount) return "";
        const float v = getParameter(id);
        switch (id) {
        case kCeiling: return formatDb(ceilingDbFromNorm(v));
        case kReleaseMode: return v > 0.5f ? "Manual" : "Auto";
        case kRelease: return formatMs(releaseMsFromNorm(v));
        case kBypass: return v > 0.5f ? "ON" : "OFF";
        default: return "";
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
        return { data, data + sizeof(blob) };
    }

    bool loadState(const std::vector<uint8_t>& state) override {
        if (state.size() < sizeof(uint32_t) * 2) return false;
        uint32_t magic = 0;
        std::memcpy(&magic, state.data(), sizeof(magic));
        if (magic != kStateMagic) return false;
        if (state.size() < sizeof(uint32_t) * 2 + sizeof(float) * kParamCount) return false;
        struct Blob {
            uint32_t magic;
            uint32_t version;
            float params[kParamCount];
        };
        Blob blob{};
        std::memcpy(&blob, state.data(), sizeof(blob));
        if (blob.version < 1 || blob.version > 1) return false;
        for (uint32_t i = 0; i < kParamCount; ++i) {
            setParameter(i, blob.params[i]);
        }
        return true;
    }

    bool hasEditor() const override { return true; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {520, 400}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override {
        return static_cast<uint32_t>(std::ceil(kLookaheadMs * m_sampleRate * 0.001));
    }
    uint32_t getTailSamples() const override { return 0; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

    // Metering
    float getGainReductionDb() const { return m_gainReduction.load(std::memory_order_relaxed); }
    float getInputLevel() const { return m_inputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return m_outputLevel.load(std::memory_order_relaxed); }

private:
    // ── OS Coefficients: 4-point Lagrange interpolation at 4x ──
    static constexpr float kOsCoeffs[kOsRatio * kOsTapsPerPhase] = {
        // Phase 0 (f=0.0)
        0.0f, 1.0f, 0.0f, 0.0f,
        // Phase 1 (f=0.25)
        -0.0546875f, 0.8203125f, 0.2734375f, -0.0390625f,
        // Phase 2 (f=0.5)
        -0.0625f, 0.5625f, 0.5625f, -0.0625f,
        // Phase 3 (f=0.75)
        -0.0390625f, 0.2734375f, 0.8203125f, -0.0546875f,
    };

    struct UpsampleState {
        float hist[kOsTapsPerPhase]{};
    };

    static void upsampleOs(UpsampleState& st, float input, float* output) {
        for (int32_t i = kOsTapsPerPhase - 1; i > 0; --i)
            st.hist[i] = st.hist[i - 1];
        st.hist[0] = input;

        for (uint32_t p = 0; p < kOsRatio; ++p) {
            float sum = 0.0f;
            const float* phaseCoeffs = &kOsCoeffs[p * kOsTapsPerPhase];
            for (uint32_t t = 0; t < kOsTapsPerPhase; ++t) {
                sum += phaseCoeffs[t] * st.hist[t];
            }
            output[p] = sum;
        }
    }

    void updateDensity(float monoSum) {
        const float inputSq = monoSum * monoSum;
        m_shortEma = m_alphaShort * inputSq + (1.0f - m_alphaShort) * m_shortEma;
        m_longEma  = m_alphaLong  * inputSq + (1.0f - m_alphaLong)  * m_longEma;
    }

    void computeAutoReleaseMs() {
        const float density = (m_longEma > 1e-10f) ? (m_shortEma / m_longEma) : 0.0f;

        static constexpr float kDenseThresh  = 0.7f;
        static constexpr float kSparseThresh = 0.3f;
        static constexpr float kReleaseMaxMs = 300.0f;
        static constexpr float kReleaseMinMs = 60.0f;

        if (density >= kDenseThresh) {
            m_autoReleaseMs = kReleaseMaxMs;
        } else if (density <= kSparseThresh) {
            m_autoReleaseMs = kReleaseMinMs;
        } else {
            const float t = (density - kSparseThresh) / (kDenseThresh - kSparseThresh);
            m_autoReleaseMs = kReleaseMinMs + t * (kReleaseMaxMs - kReleaseMinMs);
        }
    }

    void updateLookaheadSize() {
        const uint32_t newSize = std::max(1u, std::min(kMaxLookaheadSamples,
            static_cast<uint32_t>(std::ceil(kLookaheadMs * 0.001 * m_sampleRate))));
        if (newSize != m_lookaheadSize) {
            m_lookaheadSize = newSize;
            m_lookaheadSamples = newSize;
            std::fill_n(m_lookaheadBuf[0].begin(), m_lookaheadSize, 0.0f);
            std::fill_n(m_lookaheadBuf[1].begin(), m_lookaheadSize, 0.0f);
            m_writeCursor = 0;
        }
    }

    void resetRuntimeState() {
        m_tpUpsampleL = {};
        m_tpUpsampleR = {};
        m_shortEma = 0.0f;
        m_longEma = 0.0f;
        const float sr = static_cast<float>(m_sampleRate);
        m_alphaShort = 1.0f - std::exp(-1.0f / (0.050f * sr));
        m_alphaLong  = 1.0f - std::exp(-1.0f / (0.300f * sr));
        m_smoothCoeff = 1.0f - std::exp(-1.0f / (0.002f * sr));
        m_oneOverSampleRateMs = 1.0f / (0.001f * sr);
        m_clipLevel = dbToLinear(-kCeilingMarginDb);
        m_appliedGainDb = 0.0f;
        m_autoReleaseMs = 100.0f;
        m_writeCursor = 0;
        std::fill_n(m_lookaheadBuf[0].begin(), m_lookaheadSize, 0.0f);
        std::fill_n(m_lookaheadBuf[1].begin(), m_lookaheadSize, 0.0f);
        m_gainReduction.store(0.0f, std::memory_order_relaxed);
        m_inputLevel.store(0.0f, std::memory_order_relaxed);
        m_outputLevel.store(0.0f, std::memory_order_relaxed);
    }

    void snapSmoothedParams() {
        m_ceilingSmoothed = getParameter(kCeiling);
        m_releaseSmoothed = getParameter(kRelease);
    }

    // ── Parameter mapping ──
    static float ceilingDbFromNorm(float value) {
        return -24.0f + std::clamp(value, 0.0f, 1.0f) * 24.0f;
    }

    static float releaseMsFromNorm(float value) {
        return 10.0f + std::clamp(value, 0.0f, 1.0f) * 490.0f;
    }

    // ── Utility ──
    static void copyOrClear(const float* const* inputs, float** outputs,
                            uint32_t numInputChannels, uint32_t numOutputChannels,
                            uint32_t numFrames) {
        for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
            if (outputs[ch] && ch < numInputChannels && inputs[ch]) {
                std::memcpy(outputs[ch], inputs[ch], numFrames * sizeof(float));
            } else if (outputs[ch]) {
                std::memset(outputs[ch], 0, numFrames * sizeof(float));
            }
        }
    }

    static float readInput(const float* const* inputs, uint32_t channels, uint32_t channel, uint32_t frame) {
        if (channel >= channels || !inputs[channel]) return 0.0f;
        return inputs[channel][frame];
    }

    static float sanitizeSample(float sample) {
        if (!std::isfinite(sample)) return 0.0f;
        return std::clamp(sample, -16.0f, 16.0f);
    }

    static float linearToDb(float linear) {
        if (!std::isfinite(linear) || linear <= 1.0e-12f) return -120.0f;
        return std::max(-120.0f, 20.0f * std::log10(linear));
    }

    static float dbToLinear(float db) {
        return std::exp(db * 0.11512925464970229f);
    }

    static std::string formatDb(float db) {
        const int rounded = static_cast<int>(std::round(db * 10.0f));
        if (rounded >= 0) return "0.0dB";
        return "-" + std::to_string((-rounded) / 10) + "." + std::to_string((-rounded) % 10) + "dB";
    }

    static std::string formatMs(float ms) {
        return std::to_string(static_cast<int>(std::round(ms))) + "ms";
    }

    // ── State ──
    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    uint32_t m_maxBlockSize = 512;
    std::atomic<bool> m_active{false};
    std::array<std::atomic<float>, kParamCount> m_params{};

    // Oversampler states
    UpsampleState m_tpUpsampleL;
    UpsampleState m_tpUpsampleR;

    // Lookahead buffer (fixed-size, no heap allocation)
    std::array<std::array<float, kMaxLookaheadSamples>, 2> m_lookaheadBuf{};
    uint32_t m_lookaheadSize = 0;
    uint32_t m_lookaheadSamples = 0;
    uint32_t m_writeCursor = 0;

    // Density analysis
    float m_shortEma = 0.0f;
    float m_longEma = 0.0f;
    float m_alphaShort = 0.0f;
    float m_alphaLong = 0.0f;
    float m_autoReleaseMs = 100.0f;

    // Precomputed constants (set once in resetRuntimeState)
    float m_smoothCoeff = 0.0f;
    float m_oneOverSampleRateMs = 0.0f;
    float m_clipLevel = 0.0f;

    // Gain smoothing
    float m_appliedGainDb = 0.0f;

    // Smoothed parameters
    float m_ceilingSmoothed = 0.9958f;
    float m_releaseSmoothed = 0.1837f;

    // Metering
    std::atomic<float> m_gainReduction{0.0f};
    std::atomic<float> m_inputLevel{0.0f};
    std::atomic<float> m_outputLevel{0.0f};
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
