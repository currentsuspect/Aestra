// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraComp V1 — zero-latency feed-forward compressor.

#pragma once

#include "Plugin/PluginHost.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Plugins {

class AestraComp : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagicV2 = 0x434D5002; // 'CMP' v2
    static constexpr uint32_t kStateMagicV1 = 0x434D5001; // 'CMP' v1

    enum Param : uint32_t {
        kThreshold = 0, // -60 dB to 0 dB
        kRatio,        // 1:1 to 20:1
        kAttack,       // 0.1 ms to 100 ms
        kRelease,      // 10 ms to 1000 ms
        kMakeup,       // 0 dB to +24 dB
        kKnee,         // 0 dB to 24 dB
        kMix,          // 0% to 100%
        kBypass,
        kInputGain,    // -24 dB to +24 dB
        kOutputGain,   // -24 dB to +24 dB
        kDetectorHPF,  // Off, then 20 Hz to 500 Hz
        kParamCount,

        // Deprecated aliases. These names are kept so older tests and helpers still
        // compile, but V1 treats these IDs as the new public controls or ignores
        // IDs outside kParamCount.
        kDetectorMode = kInputGain,
        kTopology = kOutputGain,
        kHold = kDetectorHPF,
        kAutoRelease = kParamCount,
        kRange,
        kLookahead,
        kStereoLink,
        kStereoLinkLaw,
        kSCHPF,
        kSCLPF,
        kSCListen,
        kOutputTrim,
        kStyle,
        kQuality,
        kLegacyParamCount
    };

    static constexpr uint32_t kLegacyDetectorModeIndex = 8;
    static constexpr uint32_t kLegacyTopologyIndex = 9;
    static constexpr uint32_t kLegacyHoldIndex = 10;
    static constexpr uint32_t kLegacyAutoReleaseIndex = 11;
    static constexpr uint32_t kLegacyRangeIndex = 12;
    static constexpr uint32_t kLegacyLookaheadIndex = 13;
    static constexpr uint32_t kLegacyStereoLinkIndex = 14;
    static constexpr uint32_t kLegacyStereoLinkLawIndex = 15;
    static constexpr uint32_t kLegacySCHPFIndex = 16;
    static constexpr uint32_t kLegacySCLPFIndex = 17;
    static constexpr uint32_t kLegacySCListenIndex = 18;
    static constexpr uint32_t kLegacyOutputTrimIndex = 19;
    static constexpr uint32_t kLegacyStyleIndex = 20;
    static constexpr uint32_t kLegacyQualityIndex = 21;

    AestraComp() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        m_sampleRate = std::max(1.0, sampleRate);
        m_maxBlockSize = maxBlockSize;
        for (const auto& param : getParameters()) {
            m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
        }
        resetRuntimeState();
        snapSmoothedParamsToTargets();
        updateDetectorHPF();
        return true;
    }

    void shutdown() override {}

    void activate() override {
        m_active.store(true, std::memory_order_relaxed);
        resetRuntimeState();
        snapSmoothedParamsToTargets();
        updateDetectorHPF();
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

        const float smoothingCoeff = 1.0f - std::exp(
            -1.0f / std::max(1.0f, static_cast<float>(m_sampleRate) * 0.005f));
        const uint32_t channels = std::min<uint32_t>(2, numOutputChannels);
        const bool stereo = channels >= 2;

        if (m_detectorHPFDirty.exchange(false, std::memory_order_acq_rel)) {
            updateDetectorHPF();
        }

        float env = m_env;
        float hpfXL = m_hpfXL;
        float hpfYL = m_hpfYL;
        float hpfXR = m_hpfXR;
        float hpfYR = m_hpfYR;
        float blockInputPeak = 0.0f;
        float blockOutputPeak = 0.0f;
        float blockGainReductionDb = 0.0f;

        for (uint32_t blockStart = 0; blockStart < numFrames; blockStart += kBlockSize) {
            smoothParams(smoothingCoeff);
            const uint32_t blockEnd = std::min(blockStart + kBlockSize, numFrames);

            const float thresholdDb = thresholdDbFromNorm(m_thresholdSmoothed);
            const float ratio = ratioFromNorm(m_ratioSmoothed);
            const float attackSec = attackSecFromNorm(m_attackSmoothed);
            const float releaseSec = releaseSecFromNorm(m_releaseSmoothed);
            const float kneeDb = kneeDbFromNorm(m_kneeSmoothed);
            const float makeupLinear = dbToLinear(makeupDbFromNorm(m_makeupSmoothed));
            const float inputLinear = dbToLinear(bipolarGainDbFromNorm(m_inputGainSmoothed));
            const float outputLinear = dbToLinear(bipolarGainDbFromNorm(m_outputGainSmoothed));
            const float mix = std::clamp(m_mixSmoothed, 0.0f, 1.0f);

            const float attackCoeff = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * attackSec));
            const float releaseCoeff = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * releaseSec));
            const float rmsCoeff = std::exp(-1.0f / std::max(1.0f, static_cast<float>(m_sampleRate) * kRmsWindowSec));
            const float makeupOut = makeupLinear * outputLinear;

            for (uint32_t i = blockStart; i < blockEnd; ++i) {
            const float rawL = readInput(inputs, numInputChannels, 0, i);
            const float rawR = stereo ? readInput(inputs, numInputChannels, 1, i) : rawL;
            const float dryL = sanitizeSample(rawL);
            const float dryR = sanitizeSample(rawR);
            const float inL = dryL * inputLinear;
            const float inR = dryR * inputLinear;
            blockInputPeak = std::max(blockInputPeak, std::max(std::abs(inL), std::abs(inR)));

            float detL = inL;
            float detR = inR;
            if (m_detectorHPFEnabled) {
                detL = m_hpfA0 * detL + m_hpfA1 * hpfXL + m_hpfB1 * hpfYL;
                hpfXL = inL;
                hpfYL = detL;
                detR = m_hpfA0 * detR + m_hpfA1 * hpfXR + m_hpfB1 * hpfYR;
                hpfXR = inR;
                hpfYR = detR;
            }

            const float powerInstant = (detL * detL + detR * detR) * 0.5f;
            m_rmsEnvelope = powerInstant + rmsCoeff * (m_rmsEnvelope - powerInstant);
            const float detector = std::sqrt(m_rmsEnvelope);

            const float coeff = detector > env ? attackCoeff : releaseCoeff;
            env = coeff * env + (1.0f - coeff) * detector;
            if (!std::isfinite(env) || env < 1.0e-12f) env = 0.0f;

            const float detectorDb = linearToDb(env);
            const float reductionDb = computeGainReductionDb(detectorDb, thresholdDb, ratio, kneeDb);
            const float reductionLinear = dbToLinear(reductionDb);
            blockGainReductionDb = std::max(blockGainReductionDb, -reductionDb);

            const float wetL = (inL * reductionLinear) * makeupLinear;
            const float wetR = (inR * reductionLinear) * makeupLinear;
            const float outL = (dryL * (1.0f - mix) + wetL * mix) * outputLinear;
            const float outR = (dryR * (1.0f - mix) + wetR * mix) * outputLinear;
            blockOutputPeak = std::max(blockOutputPeak, std::max(std::abs(outL), std::abs(outR)));

            if (numOutputChannels > 0 && outputs[0]) outputs[0][i] = flushDenormal(outL);
            if (numOutputChannels > 1 && outputs[1]) outputs[1][i] = flushDenormal(outR);
            for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                if (outputs[ch]) outputs[ch][i] = 0.0f;
            }
            }
        }

        m_env = env;
        m_hpfXL = hpfXL;
        m_hpfYL = hpfYL;
        m_hpfXR = hpfXR;
        m_hpfYR = hpfYR;
        m_currentGainReductionDb.store(blockGainReductionDb, std::memory_order_relaxed);
        m_inputLevel.store(blockInputPeak, std::memory_order_relaxed);
        m_outputLevel.store(blockOutputPeak, std::memory_order_relaxed);
        m_hasProcessed.store(true, std::memory_order_relaxed);
    }

    uint32_t getParameterCount() const override { return kParamCount; }

    float getParameter(uint32_t id) const override {
        if (id >= kParamCount) return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    void setParameter(uint32_t id, float value) override {
        if (id >= kParamCount) return;
        const float clampedValue = std::clamp(value, 0.0f, 1.0f);
        m_params[id].store(clampedValue, std::memory_order_relaxed);
        if (id == kDetectorHPF) {
            m_detectorHPFDirty.store(true, std::memory_order_release);
        }

        if (!m_hasProcessed.load(std::memory_order_relaxed)) {
            switch (id) {
            case kThreshold: m_thresholdSmoothed = clampedValue; break;
            case kRatio: m_ratioSmoothed = clampedValue; break;
            case kAttack: m_attackSmoothed = clampedValue; break;
            case kRelease: m_releaseSmoothed = clampedValue; break;
            case kMakeup: m_makeupSmoothed = clampedValue; break;
            case kKnee: m_kneeSmoothed = clampedValue; break;
            case kMix: m_mixSmoothed = clampedValue; break;
            case kInputGain: m_inputGainSmoothed = clampedValue; break;
            case kOutputGain: m_outputGainSmoothed = clampedValue; break;
            default: break;
            }
        }
    }

    std::vector<PluginParameter> getParameters() const override {
        return {
            { kThreshold, "Threshold", "THR", "dB", 0.6667f, 0.0f, 1.0f, true },
            { kRatio, "Ratio", "RAT", ":1", 0.1579f, 0.0f, 1.0f, true },
            { kAttack, "Attack", "ATK", "ms", 0.0991f, 0.0f, 1.0f, true },
            { kRelease, "Release", "REL", "ms", 0.1414f, 0.0f, 1.0f, true },
            { kMakeup, "Makeup Gain", "MKP", "dB", 0.0f, 0.0f, 1.0f, true },
            { kKnee, "Knee", "KNE", "dB", 0.0f, 0.0f, 1.0f, true },
            { kMix, "Mix", "MIX", "%", 1.0f, 0.0f, 1.0f, true },
            { kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1 },
            { kInputGain, "Input Gain", "IN", "dB", 0.5f, 0.0f, 1.0f, true },
            { kOutputGain, "Output Gain", "OUT", "dB", 0.5f, 0.0f, 1.0f, true },
            { kDetectorHPF, "Detector HPF", "HPF", "Hz", 0.0f, 0.0f, 1.0f, true },
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount) return "";
        const float v = getParameter(id);
        switch (id) {
        case kThreshold: return formatDb(thresholdDbFromNorm(v));
        case kRatio: {
            const float ratio = ratioFromNorm(v);
            if (ratio >= 19.95f) return "20:1";
            return std::to_string(static_cast<int>(std::round(ratio))) + ":1";
        }
        case kAttack: return formatMs(attackSecFromNorm(v) * 1000.0f);
        case kRelease: return formatMs(releaseSecFromNorm(v) * 1000.0f);
        case kMakeup: return formatDb(makeupDbFromNorm(v));
        case kKnee: return formatDb(kneeDbFromNorm(v));
        case kMix: return std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        case kBypass: return v > 0.5f ? "ON" : "OFF";
        case kInputGain: return formatDb(bipolarGainDbFromNorm(v));
        case kOutputGain: return formatDb(bipolarGainDbFromNorm(v));
        case kDetectorHPF:
            if (v <= 0.001f) return "Off";
            return std::to_string(static_cast<int>(std::round(detectorHPFHzFromNorm(v)))) + "Hz";
        default: return "";
        }
    }

    std::vector<uint8_t> saveState() const override {
        struct Blob {
            uint32_t magic = kStateMagicV2;
            uint32_t version = 3;
            float params[kLegacyParamCount] = {};
        } blob;

        for (uint32_t i = 0; i < kParamCount; ++i) {
            blob.params[i] = getParameter(i);
        }
        blob.params[kLegacyAutoReleaseIndex] = 0.0f;
        blob.params[kLegacyRangeIndex] = 0.0f;
        blob.params[kLegacyLookaheadIndex] = 0.0f;
        blob.params[kLegacyStereoLinkIndex] = 1.0f;
        blob.params[kLegacyStereoLinkLawIndex] = 0.0f;
        blob.params[kLegacySCHPFIndex] = getParameter(kDetectorHPF);
        blob.params[kLegacySCLPFIndex] = 0.0f;
        blob.params[kLegacySCListenIndex] = 0.0f;
        blob.params[kLegacyOutputTrimIndex] = getParameter(kOutputGain);
        blob.params[kLegacyStyleIndex] = 0.0f;
        blob.params[kLegacyQualityIndex] = 0.5f;

        const auto* data = reinterpret_cast<const uint8_t*>(&blob);
        return { data, data + sizeof(blob) };
    }

    bool loadState(const std::vector<uint8_t>& state) override {
        if (state.size() < sizeof(uint32_t) * 2) return false;

        uint32_t magic = 0;
        std::memcpy(&magic, state.data(), sizeof(magic));

        if (magic == kStateMagicV2) {
            struct BlobV2 {
                uint32_t magic;
                uint32_t version;
                float params[kLegacyParamCount];
            };
            if (state.size() < sizeof(BlobV2)) return false;
            BlobV2 blob{};
            std::memcpy(&blob, state.data(), sizeof(blob));
            loadDefaults();
            for (uint32_t i = 0; i < std::min<uint32_t>(8, kParamCount); ++i) {
                setParameter(i, blob.params[i]);
            }
            if (blob.version >= 3) {
                setParameter(kInputGain, isNormalized(blob.params[kInputGain]) ? blob.params[kInputGain] : 0.5f);
                setParameter(kOutputGain, isNormalized(blob.params[kOutputGain]) ? blob.params[kOutputGain] : 0.5f);
                setParameter(kDetectorHPF, isNormalized(blob.params[kDetectorHPF]) ? blob.params[kDetectorHPF] : 0.0f);
            } else {
                setParameter(kInputGain, 0.5f);
                setParameter(kOutputGain,
                             isNormalized(blob.params[kLegacyOutputTrimIndex]) ? blob.params[kLegacyOutputTrimIndex]
                                                                               : 0.5f);
                setParameter(kDetectorHPF,
                             isNormalized(blob.params[kLegacySCHPFIndex]) ? blob.params[kLegacySCHPFIndex] : 0.0f);
            }
            return true;
        }

        if (magic == kStateMagicV1) {
            constexpr uint32_t v1ParamCount = 8;
            struct BlobV1 {
                uint32_t magic;
                uint32_t version;
                float params[v1ParamCount];
            };
            if (state.size() < sizeof(BlobV1)) return false;
            BlobV1 blob{};
            std::memcpy(&blob, state.data(), sizeof(blob));
            loadDefaults();
            for (uint32_t i = 0; i < v1ParamCount; ++i) {
                setParameter(i, blob.params[i]);
            }
            return true;
        }

        return false;
    }

    bool hasEditor() const override { return true; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {560, 390}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 256; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

    float getCurrentGainReductionDb() const { return m_currentGainReductionDb.load(std::memory_order_relaxed); }
    float getInputLevel() const { return m_inputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return m_outputLevel.load(std::memory_order_relaxed); }

private:
    static constexpr float kMinDb = -120.0f;
    static constexpr float kMaxSample = 16.0f;
    static constexpr uint32_t kBlockSize = 16;
    static constexpr float kRmsWindowSec = 0.01f;

    void loadDefaults() {
        for (const auto& param : getParameters()) {
            m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
        }
        snapSmoothedParamsToTargets();
        updateDetectorHPF();
    }

    void resetRuntimeState() {
        m_env = 0.0f;
        m_rmsEnvelope = 0.0f;
        m_hpfXL = 0.0f;
        m_hpfYL = 0.0f;
        m_hpfXR = 0.0f;
        m_hpfYR = 0.0f;
        m_currentGainReductionDb.store(0.0f, std::memory_order_relaxed);
        m_inputLevel.store(0.0f, std::memory_order_relaxed);
        m_outputLevel.store(0.0f, std::memory_order_relaxed);
        m_hasProcessed.store(false, std::memory_order_relaxed);
    }

    void snapSmoothedParamsToTargets() {
        m_thresholdSmoothed = getParameter(kThreshold);
        m_ratioSmoothed = getParameter(kRatio);
        m_attackSmoothed = getParameter(kAttack);
        m_releaseSmoothed = getParameter(kRelease);
        m_makeupSmoothed = getParameter(kMakeup);
        m_kneeSmoothed = getParameter(kKnee);
        m_mixSmoothed = getParameter(kMix);
        m_inputGainSmoothed = getParameter(kInputGain);
        m_outputGainSmoothed = getParameter(kOutputGain);
    }

    void smoothParams(float coeff) {
        m_thresholdSmoothed += (getParameter(kThreshold) - m_thresholdSmoothed) * coeff;
        m_ratioSmoothed += (getParameter(kRatio) - m_ratioSmoothed) * coeff;
        m_attackSmoothed += (getParameter(kAttack) - m_attackSmoothed) * coeff;
        m_releaseSmoothed += (getParameter(kRelease) - m_releaseSmoothed) * coeff;
        m_makeupSmoothed += (getParameter(kMakeup) - m_makeupSmoothed) * coeff;
        m_kneeSmoothed += (getParameter(kKnee) - m_kneeSmoothed) * coeff;
        m_mixSmoothed += (getParameter(kMix) - m_mixSmoothed) * coeff;
        m_inputGainSmoothed += (getParameter(kInputGain) - m_inputGainSmoothed) * coeff;
        m_outputGainSmoothed += (getParameter(kOutputGain) - m_outputGainSmoothed) * coeff;
    }

    void updateDetectorHPF() {
        const float raw = getParameter(kDetectorHPF);
        m_detectorHPFEnabled = raw > 0.001f;
        if (!m_detectorHPFEnabled) {
            m_hpfA0 = 1.0f;
            m_hpfA1 = 0.0f;
            m_hpfB1 = 0.0f;
            return;
        }

        const float frequency = detectorHPFHzFromNorm(raw);
        const float rc = 1.0f / (2.0f * 3.14159265358979323846f * frequency);
        const float dt = 1.0f / static_cast<float>(m_sampleRate);
        const float alpha = rc / (rc + dt);
        m_hpfA0 = alpha;
        m_hpfA1 = -alpha;
        m_hpfB1 = alpha;
    }

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

    static bool isNormalized(float value) {
        return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
    }

    static float sanitizeSample(float sample) {
        if (!std::isfinite(sample)) return 0.0f;
        return std::clamp(sample, -kMaxSample, kMaxSample);
    }

    static float flushDenormal(float value) {
        return std::abs(value) < 1.0e-20f ? 0.0f : value;
    }

    static float linearToDb(float linear) {
        if (!std::isfinite(linear) || linear <= 1.0e-12f) return kMinDb;
        return std::max(kMinDb, 20.0f * std::log10(linear));
    }

    static float dbToLinear(float db) {
        return std::pow(10.0f, db / 20.0f);
    }

    static float thresholdDbFromNorm(float value) { return -60.0f + std::clamp(value, 0.0f, 1.0f) * 60.0f; }
    static float ratioFromNorm(float value) { return 1.0f + std::clamp(value, 0.0f, 1.0f) * 19.0f; }
    static float attackSecFromNorm(float value) { return (0.1f + std::clamp(value, 0.0f, 1.0f) * 99.9f) * 0.001f; }
    static float releaseSecFromNorm(float value) { return (10.0f + std::clamp(value, 0.0f, 1.0f) * 990.0f) * 0.001f; }
    static float makeupDbFromNorm(float value) { return std::clamp(value, 0.0f, 1.0f) * 24.0f; }
    static float kneeDbFromNorm(float value) { return std::clamp(value, 0.0f, 1.0f) * 24.0f; }
    static float bipolarGainDbFromNorm(float value) { return -24.0f + std::clamp(value, 0.0f, 1.0f) * 48.0f; }
    static float detectorHPFHzFromNorm(float value) { return 20.0f + std::clamp(value, 0.0f, 1.0f) * 480.0f; }

    static float computeGainReductionDb(float inputDb, float thresholdDb, float ratio, float kneeDb) {
        const float overDb = inputDb - thresholdDb;
        if (ratio <= 1.0001f) return 0.0f;
        const float slope = 1.0f - 1.0f / ratio;

        if (kneeDb <= 0.001f) {
            return overDb > 0.0f ? -overDb * slope : 0.0f;
        }

        const float halfKnee = kneeDb * 0.5f;
        if (overDb <= -halfKnee) return 0.0f;
        if (overDb >= halfKnee) return -overDb * slope;

        const float x = overDb + halfKnee;
        return -slope * x * x / (2.0f * kneeDb);
    }

    static std::string formatDb(float db) {
        const int rounded = static_cast<int>(std::round(db));
        return (rounded >= 0 ? "+" : "") + std::to_string(rounded) + "dB";
    }

    static std::string formatMs(float ms) {
        return std::to_string(static_cast<int>(std::round(ms))) + "ms";
    }

    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    uint32_t m_maxBlockSize = 512;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_hasProcessed{false};
    std::atomic<bool> m_detectorHPFDirty{false};
    std::array<std::atomic<float>, kParamCount> m_params{};

    float m_env = 0.0f;
    float m_rmsEnvelope = 0.0f;
    float m_hpfXL = 0.0f;
    float m_hpfYL = 0.0f;
    float m_hpfXR = 0.0f;
    float m_hpfYR = 0.0f;
    float m_hpfA0 = 1.0f;
    float m_hpfA1 = 0.0f;
    float m_hpfB1 = 0.0f;
    bool m_detectorHPFEnabled = false;

    float m_thresholdSmoothed = 0.6667f;
    float m_ratioSmoothed = 0.1579f;
    float m_attackSmoothed = 0.0991f;
    float m_releaseSmoothed = 0.1414f;
    float m_makeupSmoothed = 0.0f;
    float m_kneeSmoothed = 0.0f;
    float m_mixSmoothed = 1.0f;
    float m_inputGainSmoothed = 0.5f;
    float m_outputGainSmoothed = 0.5f;

    std::atomic<float> m_currentGainReductionDb{0.0f};
    std::atomic<float> m_inputLevel{0.0f};
    std::atomic<float> m_outputLevel{0.0f};
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
