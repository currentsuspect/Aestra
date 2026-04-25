// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraComp v2 — Dynamics compressor with detection modes, topology, SC filtering,
// stereo link, range, hold, auto release, and output trim.
// Arsenal effect plugin for Aestra DAW.

#pragma once

#include "Plugin/PluginHost.h"
#include "Plugin/AestraEQ.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Plugins {

// ============================================================================
// Windowed RMS Detector
// ============================================================================
class RMSDetector {
public:
    void setWindowSize(uint32_t samples, double sampleRate) {
        (void)sampleRate;
        uint32_t newSize = nextPowerOf2(std::max(samples, static_cast<uint32_t>(64)));
        if (newSize != m_windowSize) {
            m_windowSize = newSize;
            m_mask = m_windowSize - 1;
            m_buffer.assign(m_windowSize, 0.0f);
            m_writeIndex = 0;
            m_sumSquares = 0.0;
            m_recalcCounter = 0;
        }
    }

    float process(float sample) {
        if (m_windowSize == 0) return 0.0f;

        const float square = sample * sample;

        m_sumSquares -= m_buffer[m_writeIndex];
        m_buffer[m_writeIndex] = square;
        m_sumSquares += square;

        m_writeIndex = (m_writeIndex + 1) & m_mask;

        // Periodic full recompute to prevent floating-point drift
        m_recalcCounter++;
        if (m_recalcCounter >= m_windowSize) {
            m_recalcCounter = 0;
            m_sumSquares = 0.0;
            for (uint32_t i = 0; i < m_windowSize; ++i)
                m_sumSquares += m_buffer[i];
        }

        return std::sqrt(static_cast<float>(std::max(0.0, m_sumSquares) / static_cast<double>(m_windowSize)));
    }

    void reset() {
        if (m_buffer.size() >= m_windowSize)
            std::memset(m_buffer.data(), 0, m_windowSize * sizeof(float));
        m_writeIndex = 0;
        m_sumSquares = 0.0;
        m_recalcCounter = 0;
    }

private:
    static uint32_t nextPowerOf2(uint32_t v) {
        v--;
        v |= v >> 1; v |= v >> 2; v |= v >> 4;
        v |= v >> 8; v |= v >> 16;
        return v + 1;
    }

    std::vector<float> m_buffer;
    uint32_t m_writeIndex = 0;
    double m_sumSquares = 0.0;
    uint32_t m_windowSize = 0;
    uint32_t m_mask = 0;
    uint32_t m_recalcCounter = 0;
};

// ============================================================================
// AestraComp v2 — Full DSP Compressor
// ============================================================================
class AestraComp : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagicV2 = 0x434D5002; // 'CMP' v2
    static constexpr uint32_t kStateMagicV1 = 0x434D5001; // 'CMP' v1

    // Parameters
    enum Param : uint32_t {
        kThreshold = 0,   // -60dB to 0dB
        kRatio,           // 1:1 to 20:1
        kAttack,          // 0.1ms to 100ms
        kRelease,         // 10ms to 1000ms
        kMakeup,          // 0dB to +24dB
        kKnee,            // 0dB to 24dB
        kMix,             // 0% to 100%
        kBypass,
        kDetectorMode,    // 0=Peak, 1=RMS
        kTopology,        // 0=Feed-forward, 1=Feedback
        kHold,            // 0-1 → 0-50ms hold time
        kAutoRelease,     // 0=Off, 1=On
        kRange,           // 0-1 → 0 to -60dB max GR cap
        kLookahead,       // TODO: lookahead — allocate delay buffer, compensate latency via getLatencySamples()
        kStereoLink,      // 0-1 → 0-100%
        kStereoLinkLaw,   // 0=Max, 0.5=Average, 1=Energy
        kSCHPF,           // 0-1 → 20-500Hz
        kSCLPF,           // 0-1 → 1k-20kHz
        kSCListen,        // 0=Off, 1=On
        kOutputTrim,      // 0-1 → -24dB to +24dB
        kStyle,           // 0=Clean, 1=Punch, 2=Glue, 3=Smooth
        kQuality,         // 0=Live, 1=Normal, 2=High Quality
        kParamCount
    };

    AestraComp() = default;

    float getCurrentGainReductionDb() const { return m_currentGainReductionDb.load(std::memory_order_relaxed); }

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        const auto defaults = getParameters();
        for (const auto& param : defaults) {
            if (param.id < kParamCount) {
                m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
            }
        }
        m_envL = m_envR = -120.0f;
        m_attackCoeffSmoothed = 0.999f;
        m_releaseCoeffSmoothed = 0.999f;
        m_prevOutput[0] = m_prevOutput[1] = 0.0f;
        m_holdCounter[0] = m_holdCounter[1] = 0;
        m_heldLevel[0] = m_heldLevel[1] = -120.0f;
        m_lastSCHPF = -1.0f;
        m_lastSCLPF = -1.0f;

        // Initialize per-channel RMS detectors (10ms window)
        uint32_t rmsWindow = static_cast<uint32_t>(sampleRate * 0.010);
        m_rmsDetector[0].setWindowSize(rmsWindow, sampleRate);
        m_rmsDetector[1].setWindowSize(rmsWindow, sampleRate);

        // Initialize SC filters (passthrough)
        m_scHPF[0].reset(); m_scHPF[1].reset();
        m_scLPF[0].reset(); m_scLPF[1].reset();

        // Initialize param smoothers to defaults/current values.
        m_thresholdSmoothed = getParameter(kThreshold);
        m_ratioSmoothed = getParameter(kRatio);
        m_attackSmoothed = getParameter(kAttack);
        m_releaseSmoothed = getParameter(kRelease);
        m_kneeSmoothed = getParameter(kKnee);
        m_makeupSmoothed = getParameter(kMakeup);
        m_mixSmoothed = getParameter(kMix);
        m_stereoLinkSmoothed = getParameter(kStereoLink);
        m_gainSmoothedL = 1.0f;
        m_gainSmoothedR = 1.0f;
        m_outputSmoothedL = 0.0f;
        m_outputSmoothedR = 0.0f;
        m_inputLevel.store(0.0f, std::memory_order_relaxed);
        m_outputLevel.store(0.0f, std::memory_order_relaxed);
        m_hasProcessed.store(false, std::memory_order_relaxed);

        return true;
    }

    void shutdown() override {}
    void activate() override {
        m_active.store(true, std::memory_order_relaxed);
        m_envL = m_envR = -120.0f;
        m_prevOutput[0] = m_prevOutput[1] = 0.0f;
        m_holdCounter[0] = m_holdCounter[1] = 0;
        m_heldLevel[0] = m_heldLevel[1] = -120.0f;
        m_rmsDetector[0].reset();
        m_rmsDetector[1].reset();
        m_scHPF[0].reset(); m_scHPF[1].reset();
        m_scLPF[0].reset(); m_scLPF[1].reset();
        m_currentGainReductionDb.store(0.0f, std::memory_order_relaxed);

        // Reset param smoothers to current param values
        m_thresholdSmoothed = getParameter(kThreshold);
        m_ratioSmoothed = getParameter(kRatio);
        m_attackSmoothed = getParameter(kAttack);
        m_releaseSmoothed = getParameter(kRelease);
        m_kneeSmoothed = getParameter(kKnee);
        m_makeupSmoothed = getParameter(kMakeup);
        m_mixSmoothed = getParameter(kMix);
        m_stereoLinkSmoothed = getParameter(kStereoLink);
        m_gainSmoothedL = 1.0f;
        m_gainSmoothedR = 1.0f;
        m_outputSmoothedL = 0.0f;
        m_outputSmoothedR = 0.0f;
        m_inputLevel.store(0.0f, std::memory_order_relaxed);
        m_outputLevel.store(0.0f, std::memory_order_relaxed);
        m_hasProcessed.store(false, std::memory_order_relaxed);
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

        const float detectorMode = getParameter(kDetectorMode);
        const float topology = getParameter(kTopology);
        const float holdRaw = getParameter(kHold);
        const float autoRelease = getParameter(kAutoRelease);
        const float rangeRaw = getParameter(kRange);
        const float stereoLinkLaw = getParameter(kStereoLinkLaw);
        const float scHPFRaw = getParameter(kSCHPF);
        const float scLPFRaw = getParameter(kSCLPF);
        const float scListen = getParameter(kSCListen);
        const float outputTrimRaw = getParameter(kOutputTrim);

        const float rangeDb = -rangeRaw * 60.0f;
        const float holdMs = holdRaw * 50.0f;
        const uint32_t holdSamples = static_cast<uint32_t>(holdMs * static_cast<float>(m_sampleRate) / 1000.0f);
        const float trimDb = -24.0f + outputTrimRaw * 48.0f;
        const float trimLinear = std::pow(10.0f, trimDb / 20.0f);
        const float smoothingCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, static_cast<float>(m_sampleRate) * 0.005f));

        // Update per-channel RMS windows — 10ms program-material detector.
        uint32_t rmsWindow = std::max(
            static_cast<uint32_t>(0.010f * static_cast<float>(m_sampleRate)),
            static_cast<uint32_t>(64));
        m_rmsDetector[0].setWindowSize(rmsWindow, m_sampleRate);
        m_rmsDetector[1].setWindowSize(rmsWindow, m_sampleRate);

        // Update SC filter coefficients if changed
        if (std::abs(scHPFRaw - m_lastSCHPF) > 0.001f || std::abs(scLPFRaw - m_lastSCLPF) > 0.001f) {
            m_lastSCHPF = scHPFRaw;
            m_lastSCLPF = scLPFRaw;

            float hpfFreq = 20.0f + scHPFRaw * 480.0f;
            float lpfFreq = 1000.0f + scLPFRaw * 19000.0f;

            if (scHPFRaw > 0.001f) {
                auto hpfCoeffs = designBiquad(FilterType::LowCut, hpfFreq, 0.0f, 0.707f, static_cast<float>(m_sampleRate));
                m_scHPF[0].setCoeffs(hpfCoeffs.b0, hpfCoeffs.b1, hpfCoeffs.b2, hpfCoeffs.a0, hpfCoeffs.a1, hpfCoeffs.a2);
                m_scHPF[1].setCoeffs(hpfCoeffs.b0, hpfCoeffs.b1, hpfCoeffs.b2, hpfCoeffs.a0, hpfCoeffs.a1, hpfCoeffs.a2);
            }
            if (scLPFRaw > 0.001f) {
                auto lpfCoeffs = designBiquad(FilterType::HighCut, lpfFreq, 0.0f, 0.707f, static_cast<float>(m_sampleRate));
                m_scLPF[0].setCoeffs(lpfCoeffs.b0, lpfCoeffs.b1, lpfCoeffs.b2, lpfCoeffs.a0, lpfCoeffs.a1, lpfCoeffs.a2);
                m_scLPF[1].setCoeffs(lpfCoeffs.b0, lpfCoeffs.b1, lpfCoeffs.b2, lpfCoeffs.a0, lpfCoeffs.a1, lpfCoeffs.a2);
            }
        }

        const uint32_t channels = std::min<uint32_t>(2, std::min(numInputChannels, numOutputChannels));
        const bool stereo = channels >= 2;
        const bool hasSidechain = numInputChannels >= 4 && inputs[2] && inputs[3];

        float envL = m_envL;
        float envR = m_envR;
        float blockGainReductionDb = 0.0f;
        float blockInputPeak = 0.0f;
        float blockOutputPeak = 0.0f;

        for (uint32_t i = 0; i < numFrames; ++i) {
            m_thresholdSmoothed += (getParameter(kThreshold) - m_thresholdSmoothed) * smoothingCoeff;
            m_ratioSmoothed += (getParameter(kRatio) - m_ratioSmoothed) * smoothingCoeff;
            m_attackSmoothed += (getParameter(kAttack) - m_attackSmoothed) * smoothingCoeff;
            m_releaseSmoothed += (getParameter(kRelease) - m_releaseSmoothed) * smoothingCoeff;
            m_kneeSmoothed += (getParameter(kKnee) - m_kneeSmoothed) * smoothingCoeff;
            m_makeupSmoothed += (getParameter(kMakeup) - m_makeupSmoothed) * smoothingCoeff;
            m_mixSmoothed += (getParameter(kMix) - m_mixSmoothed) * smoothingCoeff;
            m_stereoLinkSmoothed += (getParameter(kStereoLink) - m_stereoLinkSmoothed) * smoothingCoeff;

            const float thresholdDb = -60.0f + m_thresholdSmoothed * 60.0f;
            const float ratioVal = 1.0f + m_ratioSmoothed * 19.0f;
            const float kneeWidth = m_kneeSmoothed * 24.0f;
            const float makeupDb = m_makeupSmoothed * 24.0f;
            const float wetMix = m_mixSmoothed;

            float attackTime = 0.0001f + m_attackSmoothed * 0.0999f;
            if (detectorMode < 0.5f) attackTime = std::min(attackTime, 0.0005f);
            const float releaseTime = 0.01f + m_releaseSmoothed * 0.99f;
            const float attackCoeff = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * attackTime));
            const float releaseCoeff = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * releaseTime));

            // 1. Get input samples
            float inL = (inputs[0]) ? inputs[0][i] : 0.0f;
            float inR = stereo ? (inputs[1] ? inputs[1][i] : 0.0f) : inL;
            blockInputPeak = std::max(blockInputPeak, std::max(std::abs(inL), std::abs(inR)));

            // 2. Get sidechain samples
            float detL = hasSidechain ? inputs[2][i] : inL;
            float detR = hasSidechain ? inputs[3][i] : inR;

            // 3. Apply SC filters
            if (scHPFRaw > 0.001f) {
                detL = m_scHPF[0].process(detL);
                detR = m_scHPF[1].process(detR);
            }
            if (scLPFRaw > 0.001f) {
                detL = m_scLPF[0].process(detL);
                detR = m_scLPF[1].process(detR);
            }

            // 4. Detection source (per-channel for dual mono, max for fully linked)
            float detSrcL = (topology > 0.5f) ? m_prevOutput[0] : detL;
            float detSrcR = (topology > 0.5f) ? m_prevOutput[1] : detR;

            // Per-channel peak and RMS
            float peakL = std::abs(detSrcL);
            float peakR = std::abs(detSrcR);
            float rmsL = m_rmsDetector[0].process(detSrcL);
            float rmsR = m_rmsDetector[1].process(detSrcR);

            float detectedL = detectorMode > 0.5f ? rmsL : peakL;
            float detectedR = detectorMode > 0.5f ? rmsR : peakR;

            // When fully linked, use max detection for shared envelope
            float detectedDbL, detectedDbR;
            if (stereo && m_stereoLinkSmoothed >= 0.999f) {
                // Fully linked: max of both channels drives shared envelope
                float maxDet = std::max(detectedL, detectedR);
                float maxDetDb = maxDet > 1e-12f ? 20.0f * std::log10(maxDet) : -120.0f;
                detectedDbL = maxDetDb;
                detectedDbR = maxDetDb;
            } else {
                detectedDbL = detectedL > 1e-12f ? 20.0f * std::log10(detectedL) : -120.0f;
                detectedDbR = detectedR > 1e-12f ? 20.0f * std::log10(detectedR) : -120.0f;
            }

            // 5. Envelope follower with hold (per-channel)
            // For peak mode, apply envelope follower. For RMS, use detected value directly
            // (RMS is already smoothed by the window).
            float envDbL, envDbR;

            // Channel L
            if (detectorMode > 0.5f) {
                // RMS mode: use detected value directly (already smoothed)
                envDbL = detectedDbL;
            } else if (m_holdCounter[0] > 0) {
                m_holdCounter[0]--;
                envDbL = m_heldLevel[0];
            } else if (detectedDbL > envL) {
                envL = m_attackCoeffSmoothed * envL + (1.0f - m_attackCoeffSmoothed) * detectedDbL;
                if (holdSamples > 0) {
                    m_holdCounter[0] = holdSamples;
                    m_heldLevel[0] = envL;
                }
                envDbL = envL;
            } else {
                float relCoeff = m_releaseCoeffSmoothed;
                if (autoRelease > 0.5f) {
                    float currentGR = std::max(0.0f, -envL + thresholdDb);
                    float grDepth = std::min(currentGR / 60.0f, 1.0f);
                    float autoReleaseTime = 0.050f + grDepth * 0.950f;
                    relCoeff = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * autoReleaseTime));
                }
                envL = relCoeff * envL + (1.0f - relCoeff) * detectedDbL;
                envDbL = envL;
            }

            // Channel R
            if (detectorMode > 0.5f) {
                envDbR = detectedDbR;
            } else if (m_holdCounter[1] > 0) {
                m_holdCounter[1]--;
                envDbR = m_heldLevel[1];
            } else if (detectedDbR > envR) {
                envR = m_attackCoeffSmoothed * envR + (1.0f - m_attackCoeffSmoothed) * detectedDbR;
                if (holdSamples > 0) {
                    m_holdCounter[1] = holdSamples;
                    m_heldLevel[1] = envR;
                }
                envDbR = envR;
            } else {
                float relCoeff = m_releaseCoeffSmoothed;
                if (autoRelease > 0.5f) {
                    float currentGR = std::max(0.0f, -envR + thresholdDb);
                    float grDepth = std::min(currentGR / 60.0f, 1.0f);
                    float autoReleaseTime = 0.050f + grDepth * 0.950f;
                    relCoeff = std::exp(-1.0f / (static_cast<float>(m_sampleRate) * autoReleaseTime));
                }
                envR = relCoeff * envR + (1.0f - relCoeff) * detectedDbR;
                envDbR = envR;
            }

            // 6. Per-channel gain computer
            float reductionL = computeGainReduction(envDbL, thresholdDb, ratioVal, kneeWidth);
            float reductionR = stereo ? computeGainReduction(envDbR, thresholdDb, ratioVal, kneeWidth) : reductionL;

            // Range: limit max GR
            if (rangeRaw > 0.001f) {
                reductionL = std::max(reductionL, rangeDb);
                reductionR = std::max(reductionR, rangeDb);
            }

            float currentGR = std::max(std::max(0.0f, -reductionL), std::max(0.0f, -reductionR));
            blockGainReductionDb = std::max(blockGainReductionDb, currentGR);

            // 7. Stereo link: blend per-channel GR
            float finalReductionL = reductionL;
            float finalReductionR = reductionR;

            if (stereo && m_stereoLinkSmoothed > 0.001f) {
                float linkedGR;
                int lawIdx = static_cast<int>(stereoLinkLaw * 2.0f + 0.5f);
                switch (lawIdx) {
                    case 0: linkedGR = std::max(reductionL, reductionR); break;
                    case 1: linkedGR = (reductionL + reductionR) * 0.5f; break;
                    case 2: linkedGR = -std::sqrt(reductionL * reductionL + reductionR * reductionR) * 0.707f; break;
                    default: linkedGR = std::max(reductionL, reductionR); break;
                }
                finalReductionL = reductionL * (1.0f - m_stereoLinkSmoothed) + linkedGR * m_stereoLinkSmoothed;
                finalReductionR = reductionR * (1.0f - m_stereoLinkSmoothed) + linkedGR * m_stereoLinkSmoothed;
            }

            float gainLinearL, gainLinearR;

            if (stereo && m_stereoLinkSmoothed >= 0.999f) {
                // Fully linked: compute shared target output level from max detection,
                // then per-channel gains to equalize outputs to that target.
                float sharedReduction = computeGainReduction(std::max(envDbL, envDbR), thresholdDb, ratioVal, kneeWidth);
                if (rangeRaw > 0.001f) {
                    sharedReduction = std::max(sharedReduction, rangeDb);
                }
                // Target output dB = max(inputDb) + sharedReduction
                // Use max(detectedL, detectedR) as proxy for max input
                float maxDetLinear = std::max(detectedL, detectedR);
                float targetLinear = maxDetLinear * std::pow(10.0f, sharedReduction / 20.0f);
                // Per-channel gain: bring each channel to the shared target
                gainLinearL = (detectedL > 1e-12f) ? (targetLinear / detectedL) : 1.0f;
                gainLinearR = (detectedR > 1e-12f) ? (targetLinear / detectedR) : 1.0f;
            } else {
                // Dual mono or partial link: per-channel GR
                gainLinearL = std::pow(10.0f, finalReductionL / 20.0f);
                gainLinearR = std::pow(10.0f, finalReductionR / 20.0f);
            }

            // Smooth attack/release coefficients per-sample.
            m_attackCoeffSmoothed += (attackCoeff - m_attackCoeffSmoothed) * smoothingCoeff;
            m_releaseCoeffSmoothed += (releaseCoeff - m_releaseCoeffSmoothed) * smoothingCoeff;

            // 8. Apply gain reduction + makeup once in dB space before linear conversion.
            const float makeupLinear = std::pow(10.0f, makeupDb / 20.0f);
            float compressedL = inL * gainLinearL * makeupLinear;
            float compressedR = inR * gainLinearR * makeupLinear;
            if (!stereo) compressedR = compressedL;

            // 9. Store output for feedback topology
            m_prevOutput[0] = compressedL;
            m_prevOutput[1] = compressedR;

            // 10. Wet/dry mix
            float outL = inL * (1.0f - wetMix) + compressedL * wetMix;
            float outR = inR * (1.0f - wetMix) + compressedR * wetMix;

            // 11. Apply output trim only. Makeup has already been applied once above.
            outL = outL * trimLinear;
            outR = outR * trimLinear;

            // SC listen mode: output filtered sidechain signal
            if (scListen > 0.5f) {
                outL = detL;
                outR = detR;
            }

            outL = softClip(outL);
            outR = softClip(outR);
            blockOutputPeak = std::max(blockOutputPeak, std::max(std::abs(outL), std::abs(outR)));

            // 12. Write to output buffers
            if (outputs[0]) outputs[0][i] = outL;
            if (stereo && outputs[1]) outputs[1][i] = outR;
        }

        m_envL = envL;
        m_envR = envR;
        m_currentGainReductionDb.store(blockGainReductionDb, std::memory_order_relaxed);
        m_inputLevel.store(blockInputPeak, std::memory_order_relaxed);
        m_outputLevel.store(blockOutputPeak, std::memory_order_relaxed);
        m_hasProcessed.store(true, std::memory_order_relaxed);
    }

    // ---- Parameters ----
    uint32_t getParameterCount() const override { return kParamCount; }

    float getParameter(uint32_t id) const override {
        if (id >= kParamCount) return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    void setParameter(uint32_t id, float value) override {
        if (id >= kParamCount) return;
        const float clampedValue = std::clamp(value, 0.0f, 1.0f);
        m_params[id].store(clampedValue, std::memory_order_relaxed);

        // Treat setup-time edits as the new initial state. Once audio has
        // processed, parameter changes use the per-sample smoothers above.
        if (!m_hasProcessed.load(std::memory_order_relaxed)) {
            switch (id) {
            case kThreshold: m_thresholdSmoothed = clampedValue; break;
            case kRatio: m_ratioSmoothed = clampedValue; break;
            case kAttack: m_attackSmoothed = clampedValue; break;
            case kRelease: m_releaseSmoothed = clampedValue; break;
            case kKnee: m_kneeSmoothed = clampedValue; break;
            case kMakeup: m_makeupSmoothed = clampedValue; break;
            case kMix: m_mixSmoothed = clampedValue; break;
            case kStereoLink: m_stereoLinkSmoothed = clampedValue; break;
            default: break;
            }
        }
    }

    std::vector<PluginParameter> getParameters() const override {
        return {
            { kThreshold, "Threshold", "THR", "dB", 0.7f, 0.0f, 1.0f, true },
            { kRatio, "Ratio", "RAT", ":1", 0.158f, 0.0f, 1.0f, true, false, false, 0 },
            { kAttack, "Attack", "ATK", "ms", 0.0991f, 0.0f, 1.0f, true },
            { kRelease, "Release", "REL", "ms", 0.1414f, 0.0f, 1.0f, true },
            { kMakeup, "Makeup", "MKP", "dB", 0.0f, 0.0f, 1.0f, true },
            { kKnee, "Knee", "KNE", "dB", 0.25f, 0.0f, 1.0f, true },
            { kMix, "Mix", "MIX", "%", 1.0f, 0.0f, 1.0f, true },
            { kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1 },
            { kDetectorMode, "Detector", "DET", "", 0.0f, 0.0f, 1.0f, true, false, false, 1 },
            { kTopology, "Topology", "TOP", "", 0.0f, 0.0f, 1.0f, true, false, false, 1 },
            { kHold, "Hold", "HLD", "ms", 0.0f, 0.0f, 1.0f, true },
            { kAutoRelease, "Auto Release", "AR", "", 0.0f, 0.0f, 1.0f, true, false, false, 1 },
            { kRange, "Range", "RNG", "dB", 0.0f, 0.0f, 1.0f, true },
            { kLookahead, "Lookahead", "LA", "ms", 0.0f, 0.0f, 1.0f, true },
            { kStereoLink, "Stereo Link", "SLK", "%", 1.0f, 0.0f, 1.0f, true },
            { kStereoLinkLaw, "Link Law", "SLL", "", 0.0f, 0.0f, 1.0f, true, false, false, 2 },
            { kSCHPF, "SC HPF", "HPF", "Hz", 0.0f, 0.0f, 1.0f, true },
            { kSCLPF, "SC LPF", "LPF", "Hz", 0.0f, 0.0f, 1.0f, true },
            { kSCListen, "SC Listen", "SCL", "", 0.0f, 0.0f, 1.0f, true, false, false, 1 },
            { kOutputTrim, "Output Trim", "TRM", "dB", 0.5f, 0.0f, 1.0f, true },
            { kStyle, "Style", "STY", "", 0.0f, 0.0f, 1.0f, true, false, false, 3 },
            { kQuality, "Quality", "QLT", "", 0.5f, 0.0f, 1.0f, true, false, false, 2 },
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount) return "";
        float v = getParameter(id);
        switch (id) {
        case kThreshold: { float db = -60.0f + v * 60.0f; return std::to_string(static_cast<int>(db)) + "dB"; }
        case kRatio: {
            if (v > 0.99f) return std::string("inf:1");
            float r = 1.0f + v * 19.0f;
            return std::to_string(static_cast<int>(r)) + ":1";
        }
        case kAttack: { float ms = 0.1f + v * 99.9f; return std::to_string(static_cast<int>(ms)) + "ms"; }
        case kRelease: { float ms = 10.0f + v * 990.0f; return std::to_string(static_cast<int>(ms)) + "ms"; }
        case kMakeup: {
            float db = v * 24.0f;
            return (db >= 0 ? "+" : "") + std::to_string(static_cast<int>(db)) + "dB";
        }
        case kKnee: { float db = v * 24.0f; return std::to_string(static_cast<int>(db)) + "dB"; }
        case kMix: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kBypass: return v > 0.5f ? "ON" : "OFF";
        case kDetectorMode: {
            if (v < 0.25f) return "Peak";
            if (v > 0.75f) return "RMS";
            return "Blend";
        }
        case kTopology: return v > 0.5f ? "Feedback" : "Feed-forward";
        case kHold: { float ms = v * 50.0f; return std::to_string(static_cast<int>(ms)) + "ms"; }
        case kAutoRelease: return v > 0.5f ? "ON" : "OFF";
        case kRange: { float db = -v * 60.0f; return std::to_string(static_cast<int>(db)) + "dB"; }
        case kLookahead: { float ms = v * 20.0f; return std::to_string(static_cast<int>(ms)) + "ms"; }
        case kStereoLink: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kStereoLinkLaw: {
            if (v < 0.25f) return "Max";
            if (v > 0.75f) return "Energy";
            return "Average";
        }
        case kSCHPF: { float hz = 20.0f + v * 480.0f; return std::to_string(static_cast<int>(hz)) + "Hz"; }
        case kSCLPF: {
            float hz = 1000.0f + v * 19000.0f;
            if (hz >= 1000.0f) return std::to_string(static_cast<int>(hz / 100.0f) / 10.0f) + "kHz";
            return std::to_string(static_cast<int>(hz)) + "Hz";
        }
        case kSCListen: return v > 0.5f ? "ON" : "OFF";
        case kOutputTrim: {
            float db = -24.0f + v * 48.0f;
            return (db >= 0 ? "+" : "") + std::to_string(static_cast<int>(db)) + "dB";
        }
        case kStyle: {
            static const char* names[] = {"Clean", "Punch", "Glue", "Smooth"};
            uint32_t idx = static_cast<uint32_t>(v * 3.0f + 0.5f);
            idx = std::min(idx, 3u);
            return names[idx];
        }
        case kQuality: {
            static const char* names[] = {"Live", "Normal", "High"};
            uint32_t idx = static_cast<uint32_t>(v * 2.0f + 0.5f);
            idx = std::min(idx, 2u);
            return names[idx];
        }
        default: return "";
        }
    }

    // ---- State ----
    std::vector<uint8_t> saveState() const override {
        struct Blob {
            uint32_t magic = kStateMagicV2;
            uint32_t version = 2;
            float params[kParamCount];
        } blob;
        for (uint32_t i = 0; i < kParamCount; ++i) blob.params[i] = getParameter(i);
        const auto* data = reinterpret_cast<const uint8_t*>(&blob);
        return { data, data + sizeof(blob) };
    }

    bool loadState(const std::vector<uint8_t>& state) override {
        if (state.size() < sizeof(uint32_t) * 2) return false;
        const uint32_t magic = *reinterpret_cast<const uint32_t*>(state.data());

        if (magic == kStateMagicV2) {
            struct BlobV2 { uint32_t magic; uint32_t version; float params[kParamCount]; };
            if (state.size() < sizeof(BlobV2)) return false;
            const auto* blob = reinterpret_cast<const BlobV2*>(state.data());
            for (uint32_t i = 0; i < kParamCount; ++i) setParameter(i, blob->params[i]);
            return true;
        } else if (magic == kStateMagicV1) {
            constexpr uint32_t v1ParamCount = 8;
            struct BlobV1 { uint32_t magic; uint32_t version; float params[v1ParamCount]; };
            if (state.size() < sizeof(BlobV1)) return false;
            const auto* blob = reinterpret_cast<const BlobV1*>(state.data());
            for (uint32_t i = 0; i < v1ParamCount; ++i) setParameter(i, blob->params[i]);
            for (uint32_t i = v1ParamCount; i < kParamCount; ++i) setParameter(i, 0.0f);
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

    float getInputLevel() const { return m_inputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return m_outputLevel.load(std::memory_order_relaxed); }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 256; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

private:
    // Gain computer with soft knee
    static float computeGainReduction(float envDb, float thresholdDb, float ratioVal, float kneeWidth) {
        float diff = envDb - thresholdDb;

        if (kneeWidth < 0.01f) {
            // Hard knee
            return (envDb > thresholdDb) ? -(envDb - thresholdDb) * (1.0f - 1.0f / ratioVal) : 0.0f;
        }

        if (std::abs(diff) < kneeWidth * 0.5f) {
            // Soft knee region — quadratic interpolation
            float kneeDiff = diff + kneeWidth * 0.5f;
            return -(1.0f - 1.0f / ratioVal) * kneeDiff * kneeDiff / (2.0f * kneeWidth);
        } else if (envDb > thresholdDb + kneeWidth * 0.5f) {
            // Above knee — full ratio
            float aboveKnee = envDb - thresholdDb - kneeWidth * 0.5f;
            return -aboveKnee * (1.0f - 1.0f / ratioVal) - (1.0f - 1.0f / ratioVal) * kneeWidth * 0.25f;
        }
        return 0.0f; // Below threshold
    }

    static float softClip(float x) {
        if (x > 1.0f)  return 1.0f - 1.0f / (1.0f + (x - 1.0f) * 4.0f);
        if (x < -1.0f) return -1.0f + 1.0f / (1.0f - (x + 1.0f) * 4.0f);
        return x;
    }

    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    uint32_t m_maxBlockSize = 512;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_hasProcessed{false};

    std::array<std::atomic<float>, kParamCount> m_params;

    // Per-channel envelope state
    float m_envL = -120.0f, m_envR = -120.0f;
    float m_attackCoeffSmoothed = 0.999f;
    float m_releaseCoeffSmoothed = 0.999f;
    std::atomic<float> m_currentGainReductionDb{0.0f};

    // Feedback state (per-channel previous output)
    float m_prevOutput[2] = {0.0f, 0.0f};

    // Hold state (per-channel)
    uint32_t m_holdCounter[2] = {0, 0};
    float m_heldLevel[2] = {-120.0f, -120.0f};

    // Per-channel RMS detectors
    RMSDetector m_rmsDetector[2];

    // Sidechain filters (per-channel)
    BiquadFilter m_scHPF[2];
    BiquadFilter m_scLPF[2];
    float m_lastSCHPF = -1.0f;
    float m_lastSCLPF = -1.0f;

    // Smoothed parameters (prevent zipper noise on param changes)
    float m_thresholdSmoothed = 0.5f;
    float m_ratioSmoothed = 0.2f;
    float m_attackSmoothed = 0.1f;
    float m_releaseSmoothed = 0.3f;
    float m_kneeSmoothed = 0.25f;
    float m_makeupSmoothed = 0.0f;
    float m_mixSmoothed = 1.0f;
    float m_stereoLinkSmoothed = 1.0f;

    // Gain smoothing (prevent zipper on gain changes at block boundaries)
    float m_gainSmoothedL = 1.0f;
    float m_gainSmoothedR = 1.0f;

    // Output smoothing (prevent zipper on output signal)
    float m_outputSmoothedL = 0.0f;
    float m_outputSmoothedR = 0.0f;

    std::atomic<float> m_inputLevel{0.0f};
    std::atomic<float> m_outputLevel{0.0f};
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
