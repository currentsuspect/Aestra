// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraDelay — Stereo/ping-pong delay with BPM sync, damping, and smooth modulation.
// Arsenal effect plugin for Aestra DAW.

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
#include <vector>

namespace Aestra {
namespace Audio {
namespace Plugins {

class AestraDelay : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagicV3 = 0x444C5903; // 'DLY' v3
    static constexpr uint32_t kStateMagicV2 = 0x444C5902; // 'DLY' v2
    static constexpr uint32_t kStateMagicV1 = 0x444C5901; // 'DLY' v1
    static constexpr uint32_t kMaxDelaySec = 2;
    static constexpr uint32_t kBlockSize = 16;

    enum Param : uint32_t {
        kTime = 0,    // 10ms to 2000ms
        kFeedback,    // 0 to 0.95
        kDamping,     // 0 to 1
        kStereoShift, // -1 to 1
        kModDepth,    // 0 to 1
        kModRate,     // 0.1Hz to 10Hz
        kMix,         // wet/dry 0 to 1
        kBypass,
        kSyncMode,         // 0=Free, 1=Sync
        kNoteDivision,     // 0..12 division table
        kStereoMode,       // 0=Stereo, 1=Ping-Pong
        kFeedbackHighpass, // 20Hz to 2kHz feedback high-pass
        kOutputTrim,       // -12dB to +12dB wet/dry output trim
        kParamCount
    };

    enum NoteDivisionIndex : int {
        kDiv1_1 = 0,
        kDiv1_2,
        kDiv1_4,
        kDiv1_16,
        kDiv1_8, // default index 4 per spec
        kDiv1_32,
        kDiv1_2D,
        kDiv1_4D,
        kDiv1_8D,
        kDiv1_16D,
        kDiv1_4T,
        kDiv1_8T,
        kDiv1_16T
    };

    AestraDelay() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        (void)maxBlockSize;
        m_sampleRate = std::max(1.0, sampleRate);
        const auto defaults = getParameters();
        // Seed parameter defaults only on the first initialization of a fresh
        // instance. EffectChain::prepare() re-calls initialize() on the live
        // instance during sample-rate/device changes and must preserve the
        // user's current parameter values (and any loaded project state).
        if (!m_paramsInitialized.exchange(true)) {
            for (const auto& param : defaults) {
                if (param.id < kParamCount) {
                    m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
                }
            }
        }

        const uint32_t maxSamples = std::max<uint32_t>(
            1u, static_cast<uint32_t>(std::ceil(static_cast<double>(kMaxDelaySec) * m_sampleRate)) + 4u);
        uint32_t bufSize = 1;
        while (bufSize < maxSamples)
            bufSize <<= 1u;
        m_bufferMask = static_cast<int>(bufSize) - 1;
        m_bufL.assign(bufSize, 0.0f);
        m_bufR.assign(bufSize, 0.0f);

        resetRuntimeState();
        snapSmoothedParamsToTargets();
        return true;
    }

    void shutdown() override {}

    void activate() override {
        m_active.store(true, std::memory_order_relaxed);
        std::fill(m_bufL.begin(), m_bufL.end(), 0.0f);
        std::fill(m_bufR.begin(), m_bufR.end(), 0.0f);
        resetRuntimeState();
        snapSmoothedParamsToTargets();
    }

    void deactivate() override { m_active.store(false, std::memory_order_relaxed); }
    bool isActive() const override { return m_active.load(std::memory_order_relaxed); }

    void setBPM(float bpm) {
        if (std::isfinite(bpm))
            m_bpm.store(std::clamp(bpm, 20.0f, 999.0f), std::memory_order_relaxed);
    }

    float getBPM() const { return m_bpm.load(std::memory_order_relaxed); }

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* midiInput = nullptr, MidiBuffer* midiOutput = nullptr) override {
        (void)midiInput;
        (void)midiOutput;

        if (!m_active.load(std::memory_order_relaxed)) {
            for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
                if (outputs[ch] && ch < numInputChannels && inputs[ch]) {
                    if (outputs[ch] != inputs[ch])
                        std::memcpy(outputs[ch], inputs[ch], numFrames * sizeof(float));
                } else if (outputs[ch]) {
                    std::memset(outputs[ch], 0, numFrames * sizeof(float));
                }
            }
            return;
        }

        if (m_bufL.empty() || m_bufR.empty()) {
            for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
                if (outputs[ch])
                    std::memset(outputs[ch], 0, numFrames * sizeof(float));
            }
            return;
        }

        const float smoothingCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, static_cast<float>(m_sampleRate) * 0.005f));
        const float invSampleRate = 1.0f / static_cast<float>(m_sampleRate);
        const int bufMask = m_bufferMask;
        int pos = m_pos;
        uint64_t pingPongSampleCounter = m_pingPongSampleCounter;
        float lfoPhase = m_lfoPhase;
        float filtL = m_filtL;
        float filtR = m_filtR;
        float hpfPrevInL = m_hpfPrevInL;
        float hpfPrevInR = m_hpfPrevInR;
        float hpfPrevOutL = m_hpfPrevOutL;
        float hpfPrevOutR = m_hpfPrevOutR;
        float delaySamplesCurrentL = m_delaySamplesCurrentL;
        float delaySamplesCurrentR = m_delaySamplesCurrentR;
        float delaySamplesTargetL = m_delaySamplesTargetL;
        float delaySamplesTargetR = m_delaySamplesTargetR;
        uint32_t delayTransitionRemaining = m_delayTransitionRemaining;
        uint32_t delayTransitionTotal = m_delayTransitionTotal;
        bool delayTapInitialized = m_delayTapInitialized;

        constexpr float pi = 3.14159265358979323846f;
        constexpr float twoPi = 2.0f * pi;

        for (uint32_t blockStart = 0; blockStart < numFrames; blockStart += kBlockSize) {
            smoothParams(smoothingCoeff);
            const uint32_t blockEnd = std::min(blockStart + kBlockSize, numFrames);

            const float freeTimeSec = 0.01f + m_timeSmoothed * 1.99f;
            const bool sync = m_params[kSyncMode].load(std::memory_order_relaxed) > 0.5f;
            const float delaySec = sync ? getSyncedDelaySeconds() : freeTimeSec;
            const float delayMs = std::clamp(delaySec * 1000.0f, 10.0f, 2000.0f);
            const float feedback = std::clamp(m_feedbackSmoothed * 0.95f, 0.0f, 0.95f);
            const float damping = std::clamp(m_dampingSmoothed, 0.0f, 0.999f);
            const float stereoShift = m_stereoShiftSmoothed * 2.0f - 1.0f;
            const float modDepth = std::clamp(m_modDepthSmoothed, 0.0f, 1.0f);
            const float modRate = 0.1f + m_modRateSmoothed * 9.9f;
            const float mix = std::clamp(m_mixSmoothed, 0.0f, 1.0f);
            const float hpfCutoffHz = 20.0f * std::pow(100.0f, std::clamp(m_feedbackHighpassSmoothed, 0.0f, 1.0f));
            const float hpfCoeff = std::exp(-twoPi * hpfCutoffHz * invSampleRate);
            const float outputTrim = std::pow(10.0f, (m_outputTrimSmoothed * 24.0f - 12.0f) / 20.0f);
            const bool pingPong = m_params[kStereoMode].load(std::memory_order_relaxed) > 0.5f;
            const bool bypassed = m_params[kBypass].load(std::memory_order_relaxed) > 0.5f;

            const float maxShiftMs = std::min(delayMs * 0.5f, 500.0f * 1000.0f * invSampleRate);
            const float stereoOffsetMs = stereoShift * maxShiftMs;
            const float effectiveDelayMsL = std::max(delayMs - stereoOffsetMs, 10.0f);
            const float effectiveDelayMsR = std::max(delayMs + stereoOffsetMs, 10.0f);
            const float delaySamplesBaseL = std::clamp(effectiveDelayMsL * static_cast<float>(m_sampleRate) / 1000.0f,
                                                       1.0f, static_cast<float>((bufMask + 1) - 2));
            const float delaySamplesBaseR = std::clamp(effectiveDelayMsR * static_cast<float>(m_sampleRate) / 1000.0f,
                                                       1.0f, static_cast<float>((bufMask + 1) - 2));
            if (!delayTapInitialized) {
                delaySamplesCurrentL = delaySamplesTargetL = delaySamplesBaseL;
                delaySamplesCurrentR = delaySamplesTargetR = delaySamplesBaseR;
                delayTapInitialized = true;
            } else if (delayTransitionRemaining == 0 && (std::abs(delaySamplesBaseL - delaySamplesCurrentL) > 0.25f ||
                                                         std::abs(delaySamplesBaseR - delaySamplesCurrentR) > 0.25f)) {
                delaySamplesTargetL = delaySamplesBaseL;
                delaySamplesTargetR = delaySamplesBaseR;
                delayTransitionTotal = std::max<uint32_t>(32u, static_cast<uint32_t>(std::round(m_sampleRate * 0.020)));
                delayTransitionRemaining = delayTransitionTotal;
            }
            const float maxModSamples = std::min(std::min(delaySamplesBaseL, delaySamplesBaseR) * 0.25f,
                                                 static_cast<float>(m_sampleRate) * 0.0005f);
            const float pingPongPeriodBase = std::max(1.0f, std::round((delaySamplesBaseL + delaySamplesBaseR) * 0.5f));

            for (uint32_t i = blockStart; i < blockEnd; ++i) {
                lfoPhase += modRate * invSampleRate;
                if (lfoPhase >= 1.0f)
                    lfoPhase -= 1.0f;
                const float lfo = std::sin(twoPi * lfoPhase) * modDepth * maxModSamples;

                const float rawInL = (numInputChannels > 0 && inputs[0]) ? inputs[0][i] : 0.0f;
                const float rawInR = (numInputChannels > 1 && inputs[1]) ? inputs[1][i] : rawInL;
                const float inL = sanitizeDelayValue(rawInL);
                const float inR = sanitizeDelayValue(rawInR);
                const float monoIn = (inL + inR) * 0.5f;
                const bool pingPongInjectLeft =
                    ((pingPongSampleCounter / static_cast<uint64_t>(pingPongPeriodBase)) & 1ULL) == 0ULL;

                float delayOutL = 0.0f;
                float delayOutR = 0.0f;
                if (delayTransitionRemaining > 0 && delayTransitionTotal > 0) {
                    const float linearFade =
                        1.0f - static_cast<float>(delayTransitionRemaining) / static_cast<float>(delayTransitionTotal);
                    const float fade = linearFade * linearFade * (3.0f - 2.0f * linearFade);
                    const float oldL = readDelayLine(m_bufL, pos, delaySamplesCurrentL + lfo, bufMask);
                    const float oldR = readDelayLine(m_bufR, pos, delaySamplesCurrentR - lfo, bufMask);
                    const float nextL = readDelayLine(m_bufL, pos, delaySamplesTargetL + lfo, bufMask);
                    const float nextR = readDelayLine(m_bufR, pos, delaySamplesTargetR - lfo, bufMask);
                    delayOutL = oldL + (nextL - oldL) * fade;
                    delayOutR = oldR + (nextR - oldR) * fade;
                    --delayTransitionRemaining;
                    if (delayTransitionRemaining == 0) {
                        delaySamplesCurrentL = delaySamplesTargetL;
                        delaySamplesCurrentR = delaySamplesTargetR;
                    }
                } else {
                    delayOutL = readDelayLine(m_bufL, pos, delaySamplesCurrentL + lfo, bufMask);
                    delayOutR = readDelayLine(m_bufR, pos, delaySamplesCurrentR - lfo, bufMask);
                }

                constexpr float kPingPongBleed = 0.18f;
                constexpr float kPingPongBleedNorm = 1.0f / (1.0f + kPingPongBleed);
                const float wetL = pingPong ? (delayOutL + delayOutR * kPingPongBleed) * kPingPongBleedNorm : delayOutL;
                const float wetR = pingPong ? (delayOutR + delayOutL * kPingPongBleed) * kPingPongBleedNorm : delayOutR;

                const float fbSourceL = pingPong ? delayOutR : delayOutL;
                const float fbSourceR = pingPong ? delayOutL : delayOutR;
                const float dampedL = fbSourceL * (1.0f - damping) + filtL * damping;
                const float dampedR = fbSourceR * (1.0f - damping) + filtR * damping;
                filtL = dampedL;
                filtR = dampedR;
                const float filteredL = hpfCoeff * (hpfPrevOutL + dampedL - hpfPrevInL);
                const float filteredR = hpfCoeff * (hpfPrevOutR + dampedR - hpfPrevInR);
                hpfPrevInL = dampedL;
                hpfPrevInR = dampedR;
                hpfPrevOutL = sanitizeFilterState(filteredL);
                hpfPrevOutR = sanitizeFilterState(filteredR);

                if (pingPong) {
                    m_bufL[pos] = sanitizeDelayValue((pingPongInjectLeft ? monoIn : 0.0f) + hpfPrevOutL * feedback);
                    m_bufR[pos] = sanitizeDelayValue((pingPongInjectLeft ? 0.0f : monoIn) + hpfPrevOutR * feedback);
                } else {
                    m_bufL[pos] = sanitizeDelayValue(inL + hpfPrevOutL * feedback);
                    m_bufR[pos] = sanitizeDelayValue(inR + hpfPrevOutR * feedback);
                }

                pos = (pos + 1) & bufMask;
                ++pingPongSampleCounter;

                const float outL = sanitizeDelayValue((inL * (1.0f - mix) + wetL * mix) * outputTrim);
                const float outR = sanitizeDelayValue((inR * (1.0f - mix) + wetR * mix) * outputTrim);

                if (numOutputChannels > 0 && outputs[0])
                    outputs[0][i] = bypassed ? rawInL : outL;
                if (numOutputChannels > 1 && outputs[1])
                    outputs[1][i] = bypassed ? rawInR : outR;
            }
        }

        for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
            if (outputs[ch])
                std::memset(outputs[ch], 0, numFrames * sizeof(float));
        }

        m_lfoPhase = lfoPhase;
        m_filtL = filtL;
        m_filtR = filtR;
        m_hpfPrevInL = hpfPrevInL;
        m_hpfPrevInR = hpfPrevInR;
        m_hpfPrevOutL = hpfPrevOutL;
        m_hpfPrevOutR = hpfPrevOutR;
        m_pos = pos;
        m_pingPongSampleCounter = pingPongSampleCounter;
        m_delaySamplesCurrentL = delaySamplesCurrentL;
        m_delaySamplesCurrentR = delaySamplesCurrentR;
        m_delaySamplesTargetL = delaySamplesTargetL;
        m_delaySamplesTargetR = delaySamplesTargetR;
        m_delayTransitionRemaining = delayTransitionRemaining;
        m_delayTransitionTotal = delayTransitionTotal;
        m_delayTapInitialized = delayTapInitialized;
    }

    uint32_t getParameterCount() const override { return kParamCount; }

    float getParameter(uint32_t id) const override {
        if (id >= kParamCount)
            return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    void setParameter(uint32_t id, float value) override {
        if (id >= kParamCount || !std::isfinite(value))
            return;
        m_params[id].store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    std::vector<PluginParameter> getParameters() const override {
        constexpr float defaultDivision = static_cast<float>(kDiv1_8) / 12.0f;
        return {
            {kTime, "Time", "TIME", "s", 0.25f, 0.0f, 1.0f, true},
            {kFeedback, "Feedback", "FB", "", 0.3f, 0.0f, 1.0f, true},
            {kDamping, "Damping", "DMP", "", 0.2f, 0.0f, 1.0f, true},
            {kStereoShift, "Stereo", "STR", "", 0.5f, 0.0f, 1.0f, true},
            {kModDepth, "Mod Depth", "MOD", "", 0.0f, 0.0f, 1.0f, true},
            {kModRate, "Mod Rate", "RATE", "Hz", 0.1f, 0.0f, 1.0f, true},
            {kMix, "Mix", "MIX", "%", 1.0f, 0.0f, 1.0f, true},
            {kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1},
            {kSyncMode, "Sync", "SYNC", "", 0.0f, 0.0f, 1.0f, true, false, false, 1},
            {kNoteDivision, "Division", "DIV", "", defaultDivision, 0.0f, 1.0f, true, false, false, 12},
            {kStereoMode, "Stereo Mode", "MODE", "", 0.0f, 0.0f, 1.0f, true, false, false, 1},
            {kFeedbackHighpass, "Low Cut", "LCUT", "Hz", 0.0f, 0.0f, 1.0f, true},
            {kOutputTrim, "Output", "OUT", "dB", 0.5f, 0.0f, 1.0f, true},
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount)
            return "";
        const float v = getParameter(id);
        switch (id) {
        case kTime:
            return formatTimeMs((0.01f + v * 1.99f) * 1000.0f);
        case kFeedback:
            return std::to_string(static_cast<int>(std::round(v * 95.0f))) + "%";
        case kDamping:
            return std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        case kStereoShift: {
            const int pct = static_cast<int>(std::round((v * 2.0f - 1.0f) * 100.0f));
            return (pct >= 0 ? "+" : "") + std::to_string(pct) + "%";
        }
        case kModDepth:
            return std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        case kModRate: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1fHz", 0.1f + v * 9.9f);
            return buf;
        }
        case kMix:
            return std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        case kFeedbackHighpass: {
            char buf[32];
            const float hz = 20.0f * std::pow(100.0f, v);
            if (hz < 1000.0f)
                std::snprintf(buf, sizeof(buf), "%dHz", static_cast<int>(std::round(hz)));
            else
                std::snprintf(buf, sizeof(buf), "%.1fkHz", hz / 1000.0f);
            return buf;
        }
        case kOutputTrim: {
            char buf[32];
            const float db = v * 24.0f - 12.0f;
            std::snprintf(buf, sizeof(buf), "%+.1fdB", db);
            return buf;
        }
        case kBypass:
            return v > 0.5f ? "ON" : "OFF";
        case kSyncMode:
            return v > 0.5f ? "Sync" : "Free";
        case kNoteDivision:
            return noteDivisionLabel(noteDivisionIndexFromParam(v));
        case kStereoMode:
            return v > 0.5f ? "Ping-Pong" : "Stereo";
        default:
            return "";
        }
    }

    float getEffectiveDelayMs() const {
        if (getParameter(kSyncMode) > 0.5f) {
            return std::clamp(getSyncedDelaySeconds() * 1000.0f, 10.0f, 2000.0f);
        }
        return std::clamp((0.01f + getParameter(kTime) * 1.99f) * 1000.0f, 10.0f, 2000.0f);
    }

    static int noteDivisionIndexFromParam(float value) {
        return std::clamp(static_cast<int>(std::round(std::clamp(value, 0.0f, 1.0f) * 12.0f)), 0, 12);
    }

    static float noteDivisionParamFromIndex(int index) { return static_cast<float>(std::clamp(index, 0, 12)) / 12.0f; }

    static float noteDivisionMultiplier(int index) {
        switch (std::clamp(index, 0, 12)) {
        case kDiv1_1:
            return 4.0f;
        case kDiv1_2:
            return 2.0f;
        case kDiv1_4:
            return 1.0f;
        case kDiv1_16:
            return 0.25f;
        case kDiv1_8:
            return 0.5f;
        case kDiv1_32:
            return 0.125f;
        case kDiv1_2D:
            return 3.0f;
        case kDiv1_4D:
            return 1.5f;
        case kDiv1_8D:
            return 0.75f;
        case kDiv1_16D:
            return 0.375f;
        case kDiv1_4T:
            return 2.0f / 3.0f;
        case kDiv1_8T:
            return 1.0f / 3.0f;
        case kDiv1_16T:
            return 1.0f / 6.0f;
        default:
            return 0.5f;
        }
    }

    static const char* noteDivisionLabel(int index) {
        switch (std::clamp(index, 0, 12)) {
        case kDiv1_1:
            return "1/1";
        case kDiv1_2:
            return "1/2";
        case kDiv1_4:
            return "1/4";
        case kDiv1_16:
            return "1/16";
        case kDiv1_8:
            return "1/8";
        case kDiv1_32:
            return "1/32";
        case kDiv1_2D:
            return "1/2D";
        case kDiv1_4D:
            return "1/4D";
        case kDiv1_8D:
            return "1/8D";
        case kDiv1_16D:
            return "1/16D";
        case kDiv1_4T:
            return "1/4T";
        case kDiv1_8T:
            return "1/8T";
        case kDiv1_16T:
            return "1/16T";
        default:
            return "1/8";
        }
    }

    std::vector<uint8_t> saveState() const override {
        struct Blob {
            uint32_t magic = kStateMagicV3;
            uint32_t version = 3;
            float params[kParamCount];
        } blob;
        for (uint32_t i = 0; i < kParamCount; ++i)
            blob.params[i] = getParameter(i);
        const auto* data = reinterpret_cast<const uint8_t*>(&blob);
        return {data, data + sizeof(blob)};
    }

    bool loadState(const std::vector<uint8_t>& state) override {
        if (state.size() < sizeof(uint32_t) * 2)
            return false;
        struct Header {
            uint32_t magic;
            uint32_t version;
        };
        Header header_local;
        std::memcpy(&header_local, state.data(), sizeof(header_local));
        if (header_local.magic == kStateMagicV3) {
            struct StateBlobV3 {
                uint32_t magic;
                uint32_t version;
                float params[kParamCount];
            };
            if (state.size() < sizeof(StateBlobV3))
                return false;
            StateBlobV3 blob_local;
            std::memcpy(&blob_local, state.data(), sizeof(blob_local));
            if (blob_local.version != 3 || !allFinite(blob_local.params))
                return false;
            for (uint32_t i = 0; i < kParamCount; ++i)
                setParameter(i, blob_local.params[i]);
            snapSmoothedParamsToTargets();
            return true;
        }
        if (header_local.magic == kStateMagicV2) {
            struct LegacyStateBlobV2 {
                uint32_t magic;
                uint32_t version;
                float params[11];
            };
            if (state.size() < sizeof(LegacyStateBlobV2))
                return false;
            LegacyStateBlobV2 blob_local;
            std::memcpy(&blob_local, state.data(), sizeof(blob_local));
            if (blob_local.version != 2 || !allFinite(blob_local.params))
                return false;
            for (uint32_t i = 0; i < 11; ++i)
                setParameter(i, blob_local.params[i]);
            setParameter(kFeedbackHighpass, 0.0f);
            setParameter(kOutputTrim, 0.5f);
            snapSmoothedParamsToTargets();
            return true;
        }
        if (header_local.magic == kStateMagicV1) {
            struct StateBlobV1 {
                uint32_t magic;
                uint32_t version;
                float params[8];
            };
            if (state.size() < sizeof(StateBlobV1))
                return false;
            StateBlobV1 blob_local;
            std::memcpy(&blob_local, state.data(), sizeof(blob_local));
            if (blob_local.version != 1 || !allFinite(blob_local.params))
                return false;
            {
                const auto defaults = getParameters();
                for (uint32_t i = 0; i < kParamCount; ++i)
                    setParameter(i, defaults[i].defaultValue);
            }
            for (uint32_t i = 0; i < 8; ++i)
                setParameter(i, blob_local.params[i]);
            snapSmoothedParamsToTargets();
            return true;
        }
        return false;
    }

    bool hasEditor() const override { return true; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {760, 480}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }

    uint32_t getTailSamples() const override {
        const float feedback = std::clamp(m_feedbackSmoothed * 0.95f, 0.0f, 0.95f);
        if (feedback <= 0.0001f)
            return 0;
        const float repeatsTo60dB = -60.0f / (20.0f * std::log10(feedback + 1.0e-6f));
        const float delaySeconds =
            (getParameter(kSyncMode) > 0.5f) ? getSyncedDelaySeconds() : (0.01f + m_timeSmoothed * 1.99f);
        const float tailSeconds = std::clamp(repeatsTo60dB * delaySeconds, 0.0f, 30.0f);
        return static_cast<uint32_t>(tailSeconds * static_cast<float>(m_sampleRate));
    }

    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

private:
    template <size_t N> static bool allFinite(const float (&values)[N]) {
        for (float value : values) {
            if (!std::isfinite(value))
                return false;
        }
        return true;
    }

    static std::string formatTimeMs(float ms) {
        return std::to_string(static_cast<int>(std::round(std::clamp(ms, 10.0f, 2000.0f)))) + "ms";
    }

    static float sanitizeDelayValue(float x) {
        if (!std::isfinite(x))
            return 0.0f;
        return std::clamp(x, -64.0f, 64.0f);
    }

    static float sanitizeFilterState(float x) {
        if (!std::isfinite(x) || std::abs(x) < 1.0e-20f)
            return 0.0f;
        return std::clamp(x, -64.0f, 64.0f);
    }

    static float readDelayLine(const std::vector<float>& buffer, int writePos, float delaySamples, int mask) {
        float readPosF = static_cast<float>(writePos) - std::max(delaySamples, 0.0f);
        int readPosI = static_cast<int>(readPosF);
        float frac = readPosF - static_cast<float>(readPosI);
        if (frac < 0.0f) {
            frac += 1.0f;
            --readPosI;
        }
        readPosI &= mask;
        const int i0 = (readPosI - 1) & mask;
        const int i2 = (readPosI + 1) & mask;
        const int i3 = (readPosI + 2) & mask;
        if (i0 >= static_cast<int>(buffer.size()) || readPosI >= static_cast<int>(buffer.size()) ||
            i2 >= static_cast<int>(buffer.size()) || i3 >= static_cast<int>(buffer.size()))
            return 0.0f;
        return cubicHermite(buffer[static_cast<size_t>(i0)], buffer[static_cast<size_t>(readPosI)],
                            buffer[static_cast<size_t>(i2)], buffer[static_cast<size_t>(i3)], frac);
    }

    static float cubicHermite(float y0, float y1, float y2, float y3, float t) {
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 1.5f * (y1 - y2) + 0.5f * (y3 - y0);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    float getSyncedDelaySeconds() const {
        const float bpm = std::clamp(m_bpm.load(std::memory_order_relaxed), 20.0f, 999.0f);
        const float beatSeconds = 60.0f / bpm;
        const int division = noteDivisionIndexFromParam(getParameter(kNoteDivision));
        return std::clamp(beatSeconds * noteDivisionMultiplier(division), 0.010f, 2.0f);
    }

    void resetRuntimeState() {
        m_pos = 0;
        m_pingPongSampleCounter = 0;
        m_lfoPhase = 0.0f;
        m_filtL = 0.0f;
        m_filtR = 0.0f;
        m_hpfPrevInL = 0.0f;
        m_hpfPrevInR = 0.0f;
        m_hpfPrevOutL = 0.0f;
        m_hpfPrevOutR = 0.0f;
        m_delaySamplesCurrentL = 0.0f;
        m_delaySamplesCurrentR = 0.0f;
        m_delaySamplesTargetL = 0.0f;
        m_delaySamplesTargetR = 0.0f;
        m_delayTransitionRemaining = 0;
        m_delayTransitionTotal = 0;
        m_delayTapInitialized = false;
    }

    void snapSmoothedParamsToTargets() {
        m_timeSmoothed = getParameter(kTime);
        m_feedbackSmoothed = getParameter(kFeedback);
        m_dampingSmoothed = getParameter(kDamping);
        m_stereoShiftSmoothed = getParameter(kStereoShift);
        m_modDepthSmoothed = getParameter(kModDepth);
        m_modRateSmoothed = getParameter(kModRate);
        m_mixSmoothed = getParameter(kMix);
        m_feedbackHighpassSmoothed = getParameter(kFeedbackHighpass);
        m_outputTrimSmoothed = getParameter(kOutputTrim);
    }

    void smoothParams(float coeff) {
        m_timeSmoothed += (getParameter(kTime) - m_timeSmoothed) * coeff;
        m_feedbackSmoothed += (getParameter(kFeedback) - m_feedbackSmoothed) * coeff;
        m_dampingSmoothed += (getParameter(kDamping) - m_dampingSmoothed) * coeff;
        m_stereoShiftSmoothed += (getParameter(kStereoShift) - m_stereoShiftSmoothed) * coeff;
        m_modDepthSmoothed += (getParameter(kModDepth) - m_modDepthSmoothed) * coeff;
        m_modRateSmoothed += (getParameter(kModRate) - m_modRateSmoothed) * coeff;
        m_mixSmoothed += (getParameter(kMix) - m_mixSmoothed) * coeff;
        m_feedbackHighpassSmoothed += (getParameter(kFeedbackHighpass) - m_feedbackHighpassSmoothed) * coeff;
        m_outputTrimSmoothed += (getParameter(kOutputTrim) - m_outputTrimSmoothed) * coeff;
    }

    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_paramsInitialized{false};
    std::array<std::atomic<float>, kParamCount> m_params;
    std::atomic<float> m_bpm{120.0f};

    std::vector<float> m_bufL;
    std::vector<float> m_bufR;
    int m_bufferMask = 0;
    int m_pos = 0;
    uint64_t m_pingPongSampleCounter = 0;
    float m_lfoPhase = 0.0f;
    float m_filtL = 0.0f;
    float m_filtR = 0.0f;
    float m_hpfPrevInL = 0.0f;
    float m_hpfPrevInR = 0.0f;
    float m_hpfPrevOutL = 0.0f;
    float m_hpfPrevOutR = 0.0f;
    float m_delaySamplesCurrentL = 0.0f;
    float m_delaySamplesCurrentR = 0.0f;
    float m_delaySamplesTargetL = 0.0f;
    float m_delaySamplesTargetR = 0.0f;
    uint32_t m_delayTransitionRemaining = 0;
    uint32_t m_delayTransitionTotal = 0;
    bool m_delayTapInitialized = false;

    float m_timeSmoothed = 0.25f;
    float m_feedbackSmoothed = 0.3f;
    float m_dampingSmoothed = 0.2f;
    float m_stereoShiftSmoothed = 0.5f;
    float m_modDepthSmoothed = 0.0f;
    float m_modRateSmoothed = 0.1f;
    float m_mixSmoothed = 1.0f;
    float m_feedbackHighpassSmoothed = 0.0f;
    float m_outputTrimSmoothed = 0.5f;
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
