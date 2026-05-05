// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraVerb — Modulated FDN stereo reverb with predelay and pre-diffusion.

#pragma once

#include "Plugin/PluginHost.h"
#include "DSP/ReverbSIMD.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

// MSVC does not define M_PI by default
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstring>
#include <random>
#include <string>
#include <vector>

#ifdef AESTRA_REVERB_PROFILE
#include <chrono>
#endif

namespace Aestra {
namespace Audio {
namespace Plugins {

// Lab-only profiling macros. Zero overhead when AESTRA_REVERB_PROFILE is undefined.
#ifdef AESTRA_REVERB_PROFILE
#define AESTRA_PROFILE_STAGE(stageEnum) \
    auto _aestra_prof_t0_##stageEnum = std::chrono::high_resolution_clock::now()
#define AESTRA_PROFILE_STAGE_END(stageEnum) \
    do { \
        auto _aestra_prof_t1_##stageEnum = std::chrono::high_resolution_clock::now(); \
        size_t _aestra_prof_idx_##stageEnum = static_cast<size_t>(ProfileStage::stageEnum); \
        this->m_profileData[_aestra_prof_idx_##stageEnum].totalNs += static_cast<uint64_t>( \
            std::chrono::duration_cast<std::chrono::nanoseconds>( \
                _aestra_prof_t1_##stageEnum - _aestra_prof_t0_##stageEnum).count()); \
        this->m_profileData[_aestra_prof_idx_##stageEnum].sampleCount++; \
    } while(0)
#else
#define AESTRA_PROFILE_STAGE(stageEnum) do { } while(0)
#define AESTRA_PROFILE_STAGE_END(stageEnum) do { } while(0)
#endif

// Lab-only clamp diagnostics. Zero overhead when AESTRA_REVERB_DIAGNOSTICS is undefined.
#ifdef AESTRA_REVERB_DIAGNOSTICS
#define AESTRA_DIAG_PRECLAMP(l, r) \
    do { \
        const float _absL = std::abs(l); \
        const float _absR = std::abs(r); \
        const float _pk = _absL > _absR ? _absL : _absR; \
        if (_pk > m_diagPreClampPeak) m_diagPreClampPeak = _pk; \
    } while(0)
#define AESTRA_DIAG_POSTCLAMP(l, r) \
    do { \
        const float _absL = std::abs(l); \
        const float _absR = std::abs(r); \
        const float _pk = _absL > _absR ? _absL : _absR; \
        if (_pk > m_diagPostClampPeak) m_diagPostClampPeak = _pk; \
        if (std::abs(l) >= 0.9999f || std::abs(r) >= 0.9999f) m_diagClampSampleCount += 2; \
        m_diagTotalSamples += 2; \
    } while(0)
#define AESTRA_DIAG_WETPRE(l, r) \
    do { \
        const float _absL = std::abs(l); \
        const float _absR = std::abs(r); \
        const float _pk = _absL > _absR ? _absL : _absR; \
        if (_pk > m_diagWetPreClampPeak) m_diagWetPreClampPeak = _pk; \
    } while(0)
#else
#define AESTRA_DIAG_PRECLAMP(l, r) do { } while(0)
#define AESTRA_DIAG_POSTCLAMP(l, r) do { } while(0)
#define AESTRA_DIAG_WETPRE(l, r) do { } while(0)
#endif

class AestraVerb : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagic = 0x52564203; // 'RVB' v3

    enum Param : uint32_t {
        kDecay = 0,
        kDamping,
        kPredelayMs,
        kWidth,
        kMix,
        kBypass,
        kSize,
        kDiffusion,
        kModRate,
        kModDepth,
        kMode,
        kParamCount
    };

    enum class Mode : int {
        Room = 0,
        Hall = 1,
        Plate = 2
    };

    static constexpr size_t kFDNLineCount = 8;
    static constexpr size_t kDiffuserCount = 4;
    static constexpr size_t kPlatePostAllpassCount = 2;
    static constexpr size_t kEarlyTapCount = 12;
    static constexpr float kReferenceSampleRate = 44100.0f;
    static constexpr float kMaxPredelayMs = 500.0f;
    static constexpr float kWetMakeupGain = 4.2f;
    // Session 004: per-mode wet compensation for box-cut/air reductions (linear gain).
    static constexpr float kRoomWetCompGain = 1.096f;   // +0.8 dB
    static constexpr float kHallWetCompGain = 0.96f;    // -0.35 dB; level-matches Hall without letting it jump forward.
    static constexpr float kPlateWetCompGain = 1.122f;  // +1.0 dB
    static constexpr float kTwoPi = 6.28318530718f;
    static constexpr uint32_t kControlInterval = 64;
    static constexpr std::array<float, kFDNLineCount> kLfoBaseRates = {
        0.158f, 0.278f, 0.398f, 0.533f, 0.668f, 0.803f, 0.923f, 1.058f
    };
    static constexpr std::array<uint32_t, kDiffuserCount> kBaseDiffuserLengths = {
        142, 107, 379, 277
    };
    static constexpr std::array<uint32_t, kPlatePostAllpassCount> kPlatePostAllpassLengths = {
        89, 67
    };
    static constexpr std::array<float, kFDNLineCount> kLfoSecondaryRates = {
        0.062f, 0.095f, 0.130f, 0.172f, 0.211f, 0.253f, 0.292f, 0.332f
    };
    // Session 004: per-mode mud cleanup HP cutoff frequencies (Hz).
    // Index 0=Room, 1=Hall, 2=Plate.
    static constexpr std::array<float, 3> kMudHpCutoffHz = {110.0f, 86.0f, 100.0f};
    // Session 006: reduce mud HP blend — Session 004 values were too aggressive,
    // thinning the low-end especially on Room. Keep only gentle rumble cleanup.
    static constexpr std::array<float, 3> kMudHpBlend = {0.40f, 0.36f, 0.35f};
    static constexpr std::array<float, kFDNLineCount> kInjectL = {
        0.93f, -0.37f, 0.61f, -0.79f, 0.23f, 0.84f, -0.51f, 0.42f
    };
    static constexpr std::array<float, kFDNLineCount> kInjectR = {
        0.29f, 0.88f, -0.73f, -0.19f, 0.96f, -0.44f, 0.57f, -0.82f
    };
    static constexpr std::array<float, kFDNLineCount> kOutputL = {
        0.42f, -0.31f, 0.37f, 0.23f, -0.36f, 0.29f, 0.33f, -0.27f
    };
    static constexpr std::array<float, kFDNLineCount> kOutputR = {
        0.24f, 0.39f, -0.28f, 0.35f, 0.31f, -0.34f, 0.26f, 0.41f
    };

    static uint32_t nextPowerOfTwo(uint32_t v) {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
    }

    AestraVerb() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        (void)maxBlockSize;
        m_sampleRate = sampleRate > 1.0 ? sampleRate : 48000.0;
        const auto defaults = getParameters();
        for (const auto& param : defaults) {
            if (param.id < kParamCount) {
                m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
                m_smoothedParams[param.id] = param.defaultValue;
            }
        }
        prepareDelayLines(true);
        return true;
    }

    void shutdown() override {}
    void activate() override {
        m_active.store(true, std::memory_order_relaxed);
        clearBuffers(true);
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
            copyDry(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            return;
        }

        // RT-safe: prepareDelayLines only runs in activate()/loadState(), never concurrently with process()
        // No lock needed - audio thread reads current state, control thread updates atomically

        if (m_predelayL.empty() || m_delayLines[0].empty()) {
            copyDry(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            return;
        }

        const Mode mode = currentMode();
        const ModeConstants constants = constantsForMode(mode);
        const float sampleScale = static_cast<float>(m_sampleRate) / kReferenceSampleRate;
        const float blockSmoothingCoeff = 1.0f - std::exp(-4.0f / std::max(1.0f, static_cast<float>(m_sampleRate) * 0.015f));
        static constexpr uint32_t kSmoothBlock = 4;
        const float lowDampCoeff = 1.0f - std::exp(-kTwoPi * 110.0f / static_cast<float>(m_sampleRate));
        // Session 006: restore random modulation smoothing coefficient.
        const float randomCoeff = 1.0f - std::exp(-1.0f / std::max(1.0f, static_cast<float>(m_sampleRate) * 0.22f));
        const int preMask = m_predelayMask;

        int predelayPos = m_predelayPos;
        int earlyPos = m_earlyPos;
        auto delayPos = m_delayPos;
        auto diffuserPos = m_diffuserPos;
        auto platePostPos = m_platePostPos;
        auto dampingState = m_dampingState;
        auto lowDampState = m_lowDampState;
        auto lfoPhase = m_lfoPhase;
        auto lfoPhase2 = m_lfoPhase2;
        auto lfoSin = m_lfoSin;
        auto lfoCos = m_lfoCos;
        auto lfoSin2 = m_lfoSin2;
        auto lfoCos2 = m_lfoCos2;
        auto randomMod = m_randomMod;
        auto randomTarget = m_randomTarget;
        auto randomCounter = m_randomCounter;
        auto randomState = m_randomState;
        auto smoothedParams = m_smoothedParams;

        predelayPos &= m_predelayMask;
        earlyPos &= m_earlyMask;
        for (size_t line = 0; line < kFDNLineCount; ++line) {
            delayPos[line] &= m_delayLineMasks[line];
        }
        for (size_t stage = 0; stage < kDiffuserCount; ++stage) {
            diffuserPos[stage] &= m_diffuserMasks[stage];
        }
        for (size_t stage = 0; stage < kPlatePostAllpassCount; ++stage) {
            platePostPos[stage] &= m_platePostMasks[stage];
        }

        std::array<float*, kFDNLineCount> delayPtrs{};
        std::array<int, kFDNLineCount> delayMasks{};
        for (size_t line = 0; line < kFDNLineCount; ++line) {
            delayPtrs[line] = m_delayLines[line].data();
            delayMasks[line] = m_delayLineMasks[line];
        }

        std::array<float*, kDiffuserCount> diffuserPtrsL{};
        std::array<float*, kDiffuserCount> diffuserPtrsR{};
        std::array<int, kDiffuserCount> diffuserMasks{};
        for (size_t stage = 0; stage < kDiffuserCount; ++stage) {
            diffuserPtrsL[stage] = m_diffuserL[stage].data();
            diffuserPtrsR[stage] = m_diffuserR[stage].data();
            diffuserMasks[stage] = m_diffuserMasks[stage];
        }

        std::array<float, kFDNLineCount> lineOut{};
        ControlCache control{};
        uint32_t controlCountdown = 0;

        uint32_t smoothCountdown = 0;
        for (uint32_t i = 0; i < numFrames; ++i) {
            AESTRA_PROFILE_STAGE(kParamSmooth);
            if (smoothCountdown == 0) {
                for (uint32_t p = 0; p < kParamCount; ++p) {
                    if (p == kBypass || p == kMode) continue;
                    const float target = m_params[p].load(std::memory_order_relaxed);
                    smoothedParams[p] += (target - smoothedParams[p]) * blockSmoothingCoeff;
                }
                smoothCountdown = kSmoothBlock;
            }
            --smoothCountdown;
            AESTRA_PROFILE_STAGE_END(kParamSmooth);

            AESTRA_PROFILE_STAGE(kLFOControl);
            if (controlCountdown == 0) {
#ifdef AESTRA_REVERB_HAS_AVX2
                static const bool useAVX2 =
                    Aestra::Core::CPUDetection::get().hasAVX2() && Aestra::Core::CPUDetection::get().hasFMA();
                if (useAVX2) {
                    DSP::ReverbSIMD::normalizeLFOsAVX2(lfoSin.data(), lfoCos.data());
                    DSP::ReverbSIMD::normalizeLFOsAVX2(lfoSin2.data(), lfoCos2.data());
                } else
#endif
                {
                    for (size_t line = 0; line < kFDNLineCount; ++line) {
                        normalizeOscillator(lfoSin[line], lfoCos[line]);
                        normalizeOscillator(lfoSin2[line], lfoCos2[line]);
                    }
                }
                updateControlCache(control, smoothedParams, constants, mode, sampleScale);
                controlCountdown = kControlInterval;
            }
            --controlCountdown;
            AESTRA_PROFILE_STAGE_END(kLFOControl);

            AESTRA_PROFILE_STAGE(kInputPrep);
            const float inL = (numInputChannels > 0 && inputs[0]) ? inputs[0][i] : 0.0f;
            const float inR = (numInputChannels > 1 && inputs[1]) ? inputs[1][i] : inL;
            const float dryL = inL;
            const float dryR = inR;

            m_predelayL[predelayPos] = inL;
            m_predelayR[predelayPos] = inR;
            int predelayRead = (predelayPos - control.predelaySamples) & preMask;
            float delayedL = m_predelayL[predelayRead];
            float delayedR = m_predelayR[predelayRead];
            predelayPos = (predelayPos + 1) & preMask;
            AESTRA_PROFILE_STAGE_END(kInputPrep);

            AESTRA_PROFILE_STAGE(kEarlyReflections);
            float earlyL = 0.0f;
            float earlyR = 0.0f;
            processEarlyReflections(delayedL, delayedR, control, earlyL, earlyR, earlyPos);
            AESTRA_PROFILE_STAGE_END(kEarlyReflections);

            AESTRA_PROFILE_STAGE(kDiffuser);
            if (control.diffusionEnabled) {
#ifdef AESTRA_REVERB_HAS_SSE
                static const bool useSSE = Aestra::Core::CPUDetection::get().hasSSE41();
                if (useSSE) {
                    DSP::ReverbSIMD::processDiffusersSSE(delayedL, delayedR, control.diffusionG,
                                                         diffuserPtrsL.data(), diffuserPtrsR.data(),
                                                         diffuserPos.data(), diffuserMasks.data(),
                                                         control.diffuserLengths.data(), kDiffuserCount);
                } else
#endif
                {
                    processDiffusers(delayedL, delayedR, control, diffuserPos);
                }
            }

            AESTRA_PROFILE_STAGE_END(kDiffuser);

            delayedL += earlyL * 0.22f;
            delayedR += earlyR * 0.22f;

            AESTRA_PROFILE_STAGE(kModulationLFO);
            // Vectorized LFO updates (sin/cos quadrature oscillators)
            if (control.modulationEnabled) {
#ifdef AESTRA_REVERB_HAS_AVX2
                static const bool useAVX2 =
                    Aestra::Core::CPUDetection::get().hasAVX2() && Aestra::Core::CPUDetection::get().hasFMA();
                if (useAVX2) {
                    DSP::ReverbSIMD::updateLFOsAVX2(
                        lfoSin.data(), lfoCos.data(),
                        lfoSin2.data(), lfoCos2.data(),
                        control.lfoCosInc.data(), control.lfoSinInc.data(),
                        control.lfoCosInc2.data(), control.lfoSinInc2.data());
                } else
#endif
#ifdef AESTRA_REVERB_HAS_SSE
                {
                    DSP::ReverbSIMD::updateLFOsSSE(lfoSin.data(), lfoCos.data(),
                                                   control.lfoCosInc.data(), control.lfoSinInc.data());
                    DSP::ReverbSIMD::updateLFOsSSE(&lfoSin[4], &lfoCos[4],
                                                   &control.lfoCosInc[4], &control.lfoSinInc[4]);
                    DSP::ReverbSIMD::updateLFOsSSE(lfoSin2.data(), lfoCos2.data(),
                                                   control.lfoCosInc2.data(), control.lfoSinInc2.data());
                    DSP::ReverbSIMD::updateLFOsSSE(&lfoSin2[4], &lfoCos2[4],
                                                   &control.lfoCosInc2[4], &control.lfoSinInc2[4]);
                }
#else
                {
                    for (size_t line = 0; line < kFDNLineCount; ++line) {
                        const float nextSin = lfoSin[line] * control.lfoCosInc[line] + lfoCos[line] * control.lfoSinInc[line];
                        const float nextCos = lfoCos[line] * control.lfoCosInc[line] - lfoSin[line] * control.lfoSinInc[line];
                        lfoSin[line] = nextSin;
                        lfoCos[line] = nextCos;
                        const float nextSin2 = lfoSin2[line] * control.lfoCosInc2[line] + lfoCos2[line] * control.lfoSinInc2[line];
                        const float nextCos2 = lfoCos2[line] * control.lfoCosInc2[line] - lfoSin2[line] * control.lfoSinInc2[line];
                        lfoSin2[line] = nextSin2;
                        lfoCos2[line] = nextCos2;
                    }
                }
#endif
            }

            AESTRA_PROFILE_STAGE_END(kModulationLFO);

            AESTRA_PROFILE_STAGE(kFDNDelayRead);
            for (size_t line = 0; line < kFDNLineCount; ++line) {
                float lfoOffset = 0.0f;
                if (control.modulationEnabled) {
                    // Session 006: restore random/aperiodic modulation component.
                    // Blend: 0.40 primary LFO + 0.48 secondary LFO + 0.12 random.
                    // The random component provides organic, non-periodic motion.
                    if (--randomCounter[line] <= 0) {
                        randomTarget[line] = nextRandomBipolar(randomState[line]);
                        randomCounter[line] = std::max(64, static_cast<int>((0.075f + 0.013f * static_cast<float>(line)) * static_cast<float>(m_sampleRate)));
                    }
                    randomMod[line] += (randomTarget[line] - randomMod[line]) * randomCoeff;
                    const float multi = lfoSin[line] * 0.40f +
                                        lfoSin2[line] * 0.48f +
                                        randomMod[line] * 0.12f;
                    lfoOffset = multi * control.modDepthSamples;

                    lfoPhase[line] += control.lfoPhaseInc[line];
                    lfoPhase2[line] += control.lfoPhaseInc2[line];
                    if (lfoPhase[line] >= kTwoPi) lfoPhase[line] -= kTwoPi;
                    if (lfoPhase2[line] >= kTwoPi) lfoPhase2[line] -= kTwoPi;
                }
                lineOut[line] = readDelayLine(line, delayPos[line], static_cast<float>(control.lineLengths[line]) + lfoOffset);
            }
            AESTRA_PROFILE_STAGE_END(kFDNDelayRead);

            AESTRA_PROFILE_STAGE(kFDNFeedbackMatrix);
            float wetL = 0.0f;
            float wetR = 0.0f;

            // SIMD-accelerated FDN Householder matrix + feedback + output mixing
            // Falls back to scalar path on non-SIMD platforms.
            {
                DSP::ReverbSIMD::processFDNSample(
                    lineOut.data(), delayedL, delayedR,
                    dampingState.data(), lowDampState.data(),
                    delayPtrs.data(), delayPos.data(), delayMasks.data(),
                    control.feedbackGains.data(),
                    kInjectL.data(), kInjectR.data(),
                    kOutputL.data(), kOutputR.data(),
                    control.dampingCoeff, lowDampCoeff, control.lowDampAmount,
                    wetL, wetR);
            }
            AESTRA_PROFILE_STAGE_END(kFDNFeedbackMatrix);

            AESTRA_PROFILE_STAGE(kOutputMix);
            wetL += earlyL * 0.12f;
            wetR += earlyR * 0.12f;

            wetL *= kWetMakeupGain;
            wetR *= kWetMakeupGain;

            // Session 004: per-mode wet compensation for box-cut/air energy loss
            {
                const float compGain = (mode == Mode::Room) ? kRoomWetCompGain
                                     : (mode == Mode::Hall) ? kHallWetCompGain
                                     : kPlateWetCompGain;
                wetL *= compGain;
                wetR *= compGain;
            }

            // Session 004: mud cleanup — gentle one-pole HP filter, mode-dependent
            {
                const int modeIdx = static_cast<int>(mode);
                const float alpha = m_mudHpCoeff[static_cast<size_t>(modeIdx)];
                const float blend = kMudHpBlend[static_cast<size_t>(modeIdx)];
                // One-pole HP: y[n] = x[n] - state; state += (1-alpha)*y[n]
                const float hpL = wetL - m_mudHpStateL;
                const float hpR = wetR - m_mudHpStateR;
                m_mudHpStateL += (1.0f - alpha) * hpL;
                m_mudHpStateR += (1.0f - alpha) * hpR;
                wetL = wetL + (hpL - wetL) * blend;
                wetR = wetR + (hpR - wetR) * blend;
            }

            if (mode == Mode::Plate) {
                AESTRA_PROFILE_STAGE(kPlatePostAllpass);
                processPlatePostAllpass(wetL, wetR, platePostPos);
                AESTRA_PROFILE_STAGE_END(kPlatePostAllpass);

                // Session 012: gentle peaking cut at ~562 Hz to tame metallic ringing.
                // Direct Form 2 (transposed) biquad — 2 states per channel.
                const auto& c = m_platePeakCoeff;
                const float inL = wetL, inR = wetR;
                wetL = c[0] * inL + m_platePeakZ1L;
                m_platePeakZ1L = c[1] * inL - c[3] * wetL + m_platePeakZ2L;
                m_platePeakZ2L = c[2] * inL - c[4] * wetL;
                wetR = c[0] * inR + m_platePeakZ1R;
                m_platePeakZ1R = c[1] * inR - c[3] * wetR + m_platePeakZ2R;
                m_platePeakZ2R = c[2] * inR - c[4] * wetR;
                wetL = sanitize(wetL);
                wetR = sanitize(wetR);
            }

            processWetVoicing(wetL, wetR, mode, control);

            // Late-tail stereo decorrelation (Session 011).
            // Anti-correlated diff boost: wetL' = wetL + k*(wetL-wetR), wetR' = wetR - k*(wetL-wetR).
            // Preserves mono fold-down exactly: (wetL+diff) + (wetR-diff) = wetL + wetR.
            float kDecorr = 0.0f;
            if (mode == Mode::Room) kDecorr = 0.22f;
            else if (mode == Mode::Plate) kDecorr = 0.26f;
            if (kDecorr > 0.0f) {
                const float diff = (wetL - wetR) * kDecorr;
                wetL += diff;
                wetR -= diff;
            }

            const float widthL = wetL * control.widthMain + wetR * control.widthCross;
            const float widthR = wetR * control.widthMain + wetL * control.widthCross;
            wetL = widthL * 0.5f;
            wetR = widthR * 0.5f;

            wetL = sanitize(wetL);
            wetR = sanitize(wetR);

            AESTRA_DIAG_WETPRE(wetL, wetR);

            const float sumL = dryL * control.dryGain + wetL * control.wetGain;
            const float sumR = dryR * control.dryGain + wetR * control.wetGain;

            AESTRA_DIAG_PRECLAMP(sumL, sumR);

            const float outL = sanitize(sumL);
            const float outR = sanitize(sumR);

            AESTRA_DIAG_POSTCLAMP(outL, outR);

            if (numOutputChannels > 0 && outputs[0]) {
                outputs[0][i] = outL;
            }
            if (numOutputChannels > 1 && outputs[1]) {
                outputs[1][i] = outR;
            }
            AESTRA_PROFILE_STAGE_END(kOutputMix);
        }

        m_predelayPos = predelayPos;
        m_earlyPos = earlyPos;
        m_delayPos = delayPos;
        m_diffuserPos = diffuserPos;
        m_platePostPos = platePostPos;
        m_dampingState = dampingState;
        m_lowDampState = lowDampState;
        m_lfoPhase = lfoPhase;
        m_lfoPhase2 = lfoPhase2;
        m_lfoSin = lfoSin;
        m_lfoCos = lfoCos;
        m_lfoSin2 = lfoSin2;
        m_lfoCos2 = lfoCos2;
        m_randomMod = randomMod;
        m_randomTarget = randomTarget;
        m_randomCounter = randomCounter;
        m_randomState = randomState;
        m_smoothedParams = smoothedParams;
    }

    uint32_t getParameterCount() const override { return kParamCount; }
    float getParameter(uint32_t id) const override {
        if (id >= kParamCount) return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    void setParameter(uint32_t id, float value) override {
        if (id >= kParamCount) return;
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        m_params[id].store(clamped, std::memory_order_relaxed);
        if (!m_active.load(std::memory_order_relaxed)) {
            m_smoothedParams[id] = clamped;
        }
    }

    std::vector<PluginParameter> getParameters() const override {
        return {
            { kDecay, "Decay", "DEC", "", 0.56f, 0.0f, 1.0f, true },
            { kDamping, "Damping", "DMP", "", 0.50f, 0.0f, 1.0f, true },
            { kPredelayMs, "Predelay", "PRE", "ms", 0.02f, 0.0f, 1.0f, true },
            { kWidth, "Width", "WID", "", 0.68f, 0.0f, 1.0f, true },
            { kMix, "Mix", "MIX", "%", 0.36f, 0.0f, 1.0f, true },
            { kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1 },
            { kSize, "Size", "SIZ", "x", 0.52f, 0.0f, 1.0f, true },
            { kDiffusion, "Diffusion", "DIF", "%", 0.64f, 0.0f, 1.0f, true },
            { kModRate, "Mod Rate", "RTE", "x", 0.42f, 0.0f, 1.0f, true },
            { kModDepth, "Mod Depth", "DEP", "smpl", 0.14f, 0.0f, 1.0f, true },
            { kMode, "Mode", "MOD", "", 0.0f, 0.0f, 1.0f, true, false, false, 2 },
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount) return "";
        const float v = getParameter(id);
        switch (id) {
        case kDecay: return std::to_string(static_cast<int>((0.3f + v * 9.7f) * 10.0f) / 10.0f) + "s";
        case kDamping: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kPredelayMs: return std::to_string(static_cast<int>(v * kMaxPredelayMs)) + "ms";
        case kWidth: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kMix: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kBypass: return v > 0.5f ? "ON" : "OFF";
        case kSize: {
            const float size = 0.1f + v * 1.9f;
            return std::to_string(static_cast<int>(size * 100.0f) / 100.0f) + "x";
        }
        case kDiffusion: return std::to_string(static_cast<int>(v * 100)) + "%";
        case kModRate: return std::to_string(static_cast<int>(v * 200)) + "%";
        case kModDepth: return std::to_string(static_cast<int>(v * 80.0f) / 10.0f) + " smp";
        case kMode:
            switch (modeIndex()) {
            case 0: return "Room";
            case 1: return "Hall";
            default: return "Plate";
            }
        default: return "";
        }
    }

    std::vector<uint8_t> saveState() const override {
        struct Blob { uint32_t magic = kStateMagic; uint32_t version = 3; float params[kParamCount]; } blob;
        for (uint32_t i = 0; i < kParamCount; ++i) blob.params[i] = getParameter(i);
        const auto* data = reinterpret_cast<const uint8_t*>(&blob);
        return { data, data + sizeof(blob) };
    }

    bool loadState(const std::vector<uint8_t>& state) override {
        if (state.size() < sizeof(uint32_t) * 2) return false;
        struct Header { uint32_t magic; uint32_t version; };
        const auto* header = reinterpret_cast<const Header*>(state.data());
        if (header->magic != 0x52564201 && header->magic != 0x52564202 && header->magic != kStateMagic) return false;
        const size_t availableParams = (state.size() - sizeof(uint32_t) * 2) / sizeof(float);
        const auto* params = reinterpret_cast<const float*>(state.data() + sizeof(uint32_t) * 2);
        for (size_t i = 0; i < std::min<size_t>(availableParams, kParamCount); ++i) {
            setParameter(static_cast<uint32_t>(i), params[i]);
        }
        prepareDelayLines(false);
        return true;
    }

    bool hasEditor() const override { return false; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {760, 560}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return static_cast<uint32_t>(m_sampleRate * 20.0); }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

    // ============================================================================
    // Lab-only stage profiler (compile-time gated, zero overhead when disabled)
    // ============================================================================
#ifdef AESTRA_REVERB_PROFILE
public:
    enum class ProfileStage : uint8_t {
        kInputPrep = 0,
        kParamSmooth,
        kLFOControl,
        kEarlyReflections,
        kDiffuser,
        kModulationLFO,
        kFDNDelayRead,
        kFDNFeedbackMatrix,
        kPlatePostAllpass,
        kOutputMix,
        kStageCount
    };

    struct StageProfileData {
        uint64_t totalNs = 0;
        uint64_t sampleCount = 0;
    };

    void resetProfileData() {
        for (auto& s : m_profileData) {
            s.totalNs = 0;
            s.sampleCount = 0;
        }
    }

    const std::array<StageProfileData, static_cast<size_t>(ProfileStage::kStageCount)>& getProfileData() const {
        return m_profileData;
    }

    static const char* stageName(ProfileStage stage) {
        switch (stage) {
        case ProfileStage::kInputPrep: return "Input/Predelay";
        case ProfileStage::kParamSmooth: return "Parameter Smoothing";
        case ProfileStage::kLFOControl: return "LFO Normalize + Control";
        case ProfileStage::kEarlyReflections: return "Early Reflections";
        case ProfileStage::kDiffuser: return "Diffuser";
        case ProfileStage::kModulationLFO: return "Modulation/LFO";
        case ProfileStage::kFDNDelayRead: return "FDN Delay Read";
        case ProfileStage::kFDNFeedbackMatrix: return "FDN Feedback/Matrix";
        case ProfileStage::kPlatePostAllpass: return "Plate Post-Allpass";
        case ProfileStage::kOutputMix: return "Output/Mix";
        default: return "Unknown";
        }
    }

private:
#else
    // Dummy types for non-profile builds so member declaration is valid
    struct StageProfileData { uint64_t totalNs = 0; uint64_t sampleCount = 0; };
    enum class ProfileStage : uint8_t { kStageCount = 10 };
#endif

    // ============================================================================
    // Lab-only clamp diagnostics (compile-time gated, zero overhead when disabled)
    // ============================================================================
#ifdef AESTRA_REVERB_DIAGNOSTICS
public:
    struct ClampDiagnostics {
        float preClampPeak = 0.0f;      // Peak absolute value before std::clamp
        float postClampPeak = 0.0f;     // Peak absolute value after std::clamp
        float wetPreClampPeak = 0.0f;   // Peak absolute value of wet signal before final mix
        uint64_t clampSampleCount = 0;  // Number of output samples at |value| >= 0.9999
        uint64_t totalSamples = 0;      // Total output samples processed
        float sourcePeak = 0.0f;        // Peak of the dry input source
    };

    void resetDiagnostics() {
        m_diagPreClampPeak = 0.0f;
        m_diagPostClampPeak = 0.0f;
        m_diagWetPreClampPeak = 0.0f;
        m_diagClampSampleCount = 0;
        m_diagTotalSamples = 0;
        m_diagSourcePeak = 0.0f;
    }

    void setSourcePeak(float peak) { m_diagSourcePeak = peak; }

    ClampDiagnostics getClampDiagnostics() const {
        ClampDiagnostics d;
        d.preClampPeak = m_diagPreClampPeak;
        d.postClampPeak = m_diagPostClampPeak;
        d.wetPreClampPeak = m_diagWetPreClampPeak;
        d.clampSampleCount = m_diagClampSampleCount;
        d.totalSamples = m_diagTotalSamples;
        d.sourcePeak = m_diagSourcePeak;
        return d;
    }

private:
    float m_diagPreClampPeak = 0.0f;
    float m_diagPostClampPeak = 0.0f;
    float m_diagWetPreClampPeak = 0.0f;
    uint64_t m_diagClampSampleCount = 0;
    uint64_t m_diagTotalSamples = 0;
    float m_diagSourcePeak = 0.0f;
#endif

    struct ModeConstants {
        std::array<uint32_t, kFDNLineCount> fdnBase{};
        float dampingScalar{1.0f};
        float modDepthScalar{1.0f};
        float diffusionScalar{1.0f};
        float decayScalar{1.0f};
    };

    struct ControlCache {
        float dampingCoeff{0.7f};
        float lowDampAmount{0.45f};
        float diffusionG{0.7f};
        float modDepthSamples{2.5f};
        float wetToneCoeff{0.6f};
        float wetAirBlend{0.2f};
        float dryGain{0.0f};
        float wetGain{1.0f};
        float widthMain{1.7f};
        float widthCross{0.3f};
        int predelaySamples{0};
        bool modulationEnabled{true};
        bool diffusionEnabled{true};
        std::array<int, kFDNLineCount> lineLengths{};
        std::array<float, kFDNLineCount> feedbackGains{};
        std::array<float, kFDNLineCount> lfoSinInc{};
        std::array<float, kFDNLineCount> lfoCosInc{};
        std::array<float, kFDNLineCount> lfoSinInc2{};
        std::array<float, kFDNLineCount> lfoCosInc2{};
        std::array<float, kFDNLineCount> lfoPhaseInc{};
        std::array<float, kFDNLineCount> lfoPhaseInc2{};
        std::array<int, kDiffuserCount> diffuserLengths{};
        std::array<int, kEarlyTapCount> earlyDelayL{};
        std::array<int, kEarlyTapCount> earlyDelayR{};
        std::array<float, kEarlyTapCount> earlyGains{};
    };

    static ModeConstants constantsForMode(Mode mode) {
        switch (mode) {
        case Mode::Hall:
            return {{{2557, 2617, 2491, 2422, 2277, 2356, 2188, 2116}}, 1.18f, 0.64f, 0.96f, 1.36f};
        case Mode::Plate:
            return {{{1313, 1451, 1247, 1163, 1123, 1219, 1043, 977}}, 1.38f, 0.50f, 0.98f, 1.45f};
        case Mode::Room:
        default:
            // Session 006: restore modDepthScalar from 0.15 to 0.45.
            // 0.15 made Room modulation effectively inaudible.
            return {{{1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116}}, 1.22f, 0.45f, 0.82f, 1.20f};
        }
    }

    int modeIndex() const {
        return std::clamp(static_cast<int>(std::round(getParameter(kMode) * 2.0f)), 0, 2);
    }

    Mode currentMode() const {
        switch (modeIndex()) {
        case 1: return Mode::Hall;
        case 2: return Mode::Plate;
        default: return Mode::Room;
        }
    }

    float sizeScalar() const {
        return 0.1f + std::clamp(getParameter(kSize), 0.0f, 1.0f) * 1.9f;
    }

    static uint32_t maxFdnBaseForLine(size_t line) {
        const auto room = constantsForMode(Mode::Room);
        const auto hall = constantsForMode(Mode::Hall);
        const auto plate = constantsForMode(Mode::Plate);
        return std::max({room.fdnBase[line], hall.fdnBase[line], plate.fdnBase[line]});
    }

    void updateControlCache(ControlCache& cache,
                            const std::array<float, kParamCount>& smoothedParams,
                            const ModeConstants& constants,
                            Mode mode,
                            float sampleScale) const {
        static constexpr std::array<float, kEarlyTapCount> tapMs = {
            5.1f, 7.9f, 11.3f, 13.7f, 17.1f, 19.9f, 23.5f, 29.7f, 34.1f, 41.9f, 53.3f, 67.7f
        };
        static constexpr std::array<float, kEarlyTapCount> tapGain = {
            0.34f, -0.24f, 0.21f, 0.18f, -0.16f, 0.14f, -0.12f, 0.105f, 0.092f, -0.078f, 0.062f, -0.052f
        };

        const float sr = static_cast<float>(m_sampleRate);
        const float size = 0.1f + std::clamp(smoothedParams[kSize], 0.0f, 1.0f) * 1.9f;
        const float decayTime = (0.3f + smoothedParams[kDecay] * 9.7f) * constants.decayScalar;
        const float dampingParam = std::clamp(smoothedParams[kDamping], 0.0f, 1.0f);
        const float damping = std::clamp(dampingParam * constants.dampingScalar, 0.0f, 0.98f);
        cache.dampingCoeff = 1.0f - damping;
        cache.lowDampAmount = mode == Mode::Hall ? 0.43f : (mode == Mode::Plate ? 0.42f : 0.48f);

        // Keep Hall darker than Plate, but leave enough top-end through for air after the low-mid cleanup.
        const float toneCutHz = mode == Mode::Hall
            ? 8600.0f - dampingParam * 2300.0f
            : (mode == Mode::Plate ? 9300.0f - dampingParam * 1800.0f : 9600.0f - dampingParam * 3000.0f);
        cache.wetToneCoeff = 1.0f - std::exp(-kTwoPi * std::clamp(toneCutHz, 4800.0f, 14000.0f) / sr);
        // Session 006: restore air blend — Session 004 values were too low (~6%),
        // removing most high-frequency content above the tone cutoff.
        cache.wetAirBlend = mode == Mode::Hall ? 0.21f : (mode == Mode::Plate ? 0.18f : 0.15f);

        const float predelayMs = std::clamp(smoothedParams[kPredelayMs], 0.0f, 1.0f) * kMaxPredelayMs;
        cache.predelaySamples = std::clamp(static_cast<int>(std::round((predelayMs / 1000.0f) * sr)),
                                           0, std::max(0, m_maxPredelaySamples));

        cache.diffusionG = std::clamp(smoothedParams[kDiffusion] * 0.74f * constants.diffusionScalar, 0.0f, 0.78f);
        cache.diffusionEnabled = cache.diffusionG > 0.0001f;

        const float modRateScalar = smoothedParams[kModRate] * 2.0f;
        // Session 006: increase mod depth multiplier from 5.0 to 7.0.
        // Effective depth at default (0.14): Room 0.14×7×0.45=0.44 smp, Hall 0.14×7×0.64=0.63 smp,
        // Plate 0.14×7×0.50=0.49 smp. Subtle but audible — avoids the flangey extremes.
        cache.modDepthSamples = smoothedParams[kModDepth] * 7.0f * constants.modDepthScalar;
        cache.modulationEnabled = cache.modDepthSamples > 0.0001f && modRateScalar > 0.0001f;

        const float width = std::clamp(smoothedParams[kWidth], 0.0f, 1.0f);
        cache.widthMain = 1.0f + width;
        cache.widthCross = 1.0f - width;

        const float mix = std::clamp(smoothedParams[kMix], 0.0f, 1.0f);
        const float mixAngle = mix * (kTwoPi * 0.25f);
        cache.dryGain = std::cos(mixAngle);
        cache.wetGain = std::sin(mixAngle);

        for (size_t line = 0; line < kFDNLineCount; ++line) {
            cache.lineLengths[line] = logicalFdnLength(line, constants, sampleScale, size);
            const float delaySeconds = static_cast<float>(cache.lineLengths[line]) / sr;
            cache.feedbackGains[line] = std::pow(10.0f, -3.0f * delaySeconds / std::max(0.1f, decayTime));

            const float inc = (kTwoPi * kLfoBaseRates[line] * modRateScalar) / sr;
            cache.lfoPhaseInc[line] = inc;
            cache.lfoSinInc[line] = std::sin(inc);
            cache.lfoCosInc[line] = std::cos(inc);

            const float inc2 = (kTwoPi * kLfoSecondaryRates[line] * modRateScalar) / sr;
            cache.lfoPhaseInc2[line] = inc2;
            cache.lfoSinInc2[line] = std::sin(inc2);
            cache.lfoCosInc2[line] = std::cos(inc2);
        }

        for (size_t stage = 0; stage < kDiffuserCount; ++stage) {
            cache.diffuserLengths[stage] = logicalDiffuserLength(stage, sampleScale, size);
        }

        // Hall wants a dense front edge, not an obvious second echo before the tail blooms.
        const float modeSpread = mode == Mode::Hall ? 1.14f : (mode == Mode::Plate ? 0.55f : 0.82f);
        const float modeLevel = mode == Mode::Hall ? 0.42f : (mode == Mode::Plate ? 0.40f : 0.68f);
        const float sizeTerm = std::sqrt(std::max(0.1f, size));
        const int ringSize = static_cast<int>(m_earlyL.size());
        const int maxDelay = std::max(1, ringSize - 1);
        for (size_t tap = 0; tap < kEarlyTapCount; ++tap) {
            const int delay = std::clamp(
                static_cast<int>(std::round(tapMs[tap] * modeSpread * sizeTerm * sr / 1000.0f)),
                1,
                maxDelay
            );
            const int decorrelate = static_cast<int>((tap % 3U) + 1U);
            cache.earlyDelayL[tap] = delay;
            cache.earlyDelayR[tap] = std::min(maxDelay, delay + decorrelate);
            cache.earlyGains[tap] = tapGain[tap] * modeLevel;
        }
    }

    void prepareDelayLines(bool randomizeLfos) {
        const float sampleScale = static_cast<float>(m_sampleRate) / kReferenceSampleRate;

        m_maxPredelaySamples = std::max(1, static_cast<int>(std::ceil(m_sampleRate * 0.5)));
        const uint32_t predelayPow2 = nextPowerOfTwo(static_cast<uint32_t>(m_maxPredelaySamples) + 1);
        m_predelayL.assign(static_cast<size_t>(predelayPow2), 0.0f);
        m_predelayR.assign(static_cast<size_t>(predelayPow2), 0.0f);
        m_predelayMask = static_cast<int>(predelayPow2) - 1;

        for (size_t line = 0; line < kFDNLineCount; ++line) {
            const int maxLength = std::max(
                128,
                static_cast<int>(std::ceil(maxFdnBaseForLine(line) * sampleScale * 2.0f)) + 64
            );
            const uint32_t pow2Size = nextPowerOfTwo(static_cast<uint32_t>(maxLength));
            m_delayLines[line].assign(static_cast<size_t>(pow2Size), 0.0f);
            m_delayLineMasks[line] = static_cast<int>(pow2Size) - 1;
            m_delayLengths[line] = std::min(maxLength - 2, std::max(100, maxLength / 2));
        }

        for (size_t stage = 0; stage < kDiffuserCount; ++stage) {
            const int maxLength = std::max(
                2,
                static_cast<int>(std::ceil(kBaseDiffuserLengths[stage] * sampleScale * 2.0f)) + 8
            );
            const uint32_t pow2Size = nextPowerOfTwo(static_cast<uint32_t>(maxLength));
            m_diffuserLengths[stage] = maxLength / 2;
            m_diffuserL[stage].assign(static_cast<size_t>(pow2Size), 0.0f);
            m_diffuserR[stage].assign(static_cast<size_t>(pow2Size), 0.0f);
            m_diffuserMasks[stage] = static_cast<int>(pow2Size) - 1;
        }

        for (size_t stage = 0; stage < kPlatePostAllpassCount; ++stage) {
            const int maxLength = std::max(
                2,
                static_cast<int>(std::ceil(kPlatePostAllpassLengths[stage] * sampleScale)) + 4
            );
            const uint32_t pow2Size = nextPowerOfTwo(static_cast<uint32_t>(maxLength));
            m_platePostL[stage].assign(static_cast<size_t>(pow2Size), 0.0f);
            m_platePostR[stage].assign(static_cast<size_t>(pow2Size), 0.0f);
            m_platePostDelayLengths[stage] = maxLength;
            m_platePostMasks[stage] = static_cast<int>(pow2Size) - 1;
        }

        const int earlyMaxSamples = std::max(2, static_cast<int>(std::ceil(m_sampleRate * 0.14)));
        const uint32_t earlyPow2 = nextPowerOfTwo(static_cast<uint32_t>(earlyMaxSamples));
        m_earlyL.assign(static_cast<size_t>(earlyPow2), 0.0f);
        m_earlyR.assign(static_cast<size_t>(earlyPow2), 0.0f);
        m_earlyMask = static_cast<int>(earlyPow2) - 1;

        const float sr = static_cast<float>(m_sampleRate);
        m_platePeakCoeff = peakingCoefficients(-5.5f, 620.0f, sr, 1.15f);
        // Session 006: reduce box-cut depth — Session 004 values removed too much body.
        // Room: -4.6→-3.3 dB, Hall: -3.0→-2.8 dB, Plate: -4.2→-3.3 dB
        m_boxCutCoeff[0] = peakingCoefficients(-3.3f, 430.0f, sr, 0.85f);
        m_boxCutCoeff[1] = peakingCoefficients(-2.8f, 520.0f, sr, 0.75f);
        m_boxCutCoeff[2] = peakingCoefficients(-3.3f, 1250.0f, sr, 1.10f);

        // Session 004: precompute per-mode mud HP filter coefficients (one-pole)
        for (size_t m = 0; m < 3; ++m) {
            const float omega = kTwoPi * kMudHpCutoffHz[m] / sr;
            m_mudHpCoeff[m] = std::exp(-omega);
        }

        clearBuffers(randomizeLfos);
    }

    void clearBuffers(bool randomizeLfos) {
        for (auto& b : m_delayLines) std::fill(b.begin(), b.end(), 0.0f);
        for (auto& b : m_diffuserL) std::fill(b.begin(), b.end(), 0.0f);
        for (auto& b : m_diffuserR) std::fill(b.begin(), b.end(), 0.0f);
        for (auto& b : m_platePostL) std::fill(b.begin(), b.end(), 0.0f);
        for (auto& b : m_platePostR) std::fill(b.begin(), b.end(), 0.0f);
        std::fill(m_earlyL.begin(), m_earlyL.end(), 0.0f);
        std::fill(m_earlyR.begin(), m_earlyR.end(), 0.0f);
        std::fill(m_predelayL.begin(), m_predelayL.end(), 0.0f);
        std::fill(m_predelayR.begin(), m_predelayR.end(), 0.0f);
        m_delayPos.fill(0);
        m_diffuserPos.fill(0);
        m_platePostPos.fill(0);
        m_dampingState.fill(0.0f);
        m_lowDampState.fill(0.0f);
        m_predelayPos = 0;
        m_earlyPos = 0;

        if (randomizeLfos) {
            std::mt19937 rng{0xAE57A000u}; // deterministic seed for reproducible tests
            std::uniform_real_distribution<float> dist(0.0f, kTwoPi);
            for (auto& phase : m_lfoPhase) phase = dist(rng);
            for (auto& phase : m_lfoPhase2) phase = dist(rng);
            std::uniform_int_distribution<uint32_t> stateDist(1U, 0x7fffffffU);
            for (auto& state : m_randomState) state = stateDist(rng);
        }
        for (size_t line = 0; line < kFDNLineCount; ++line) {
            m_lfoSin[line] = std::sin(m_lfoPhase[line]);
            m_lfoCos[line] = std::cos(m_lfoPhase[line]);
            m_lfoSin2[line] = std::sin(m_lfoPhase2[line]);
            m_lfoCos2[line] = std::cos(m_lfoPhase2[line]);
        }
        m_randomMod.fill(0.0f);
        m_randomTarget.fill(0.0f);
        m_randomCounter.fill(0);

        m_platePeakZ1L = m_platePeakZ2L = 0.0f;
        m_platePeakZ1R = m_platePeakZ2R = 0.0f;
        m_boxCutZ1L = m_boxCutZ2L = 0.0f;
        m_boxCutZ1R = m_boxCutZ2R = 0.0f;
        m_wetToneStateL = 0.0f;
        m_wetToneStateR = 0.0f;
        m_mudHpStateL = 0.0f;
        m_mudHpStateR = 0.0f;
    }

    void copyDry(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                 uint32_t numOutputChannels, uint32_t numFrames) const {
        for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
            if (outputs[ch] && ch < numInputChannels && inputs[ch]) {
                std::memcpy(outputs[ch], inputs[ch], numFrames * sizeof(float));
            } else if (outputs[ch]) {
                std::memset(outputs[ch], 0, numFrames * sizeof(float));
            }
        }
    }

    float readDelayLine(size_t line, int writePos, float delaySamples) const {
        const auto& buffer = m_delayLines[line];
        const int mask = m_delayLineMasks[line];
        if (mask <= 0) return 0.0f;

        float readPosF = static_cast<float>(writePos) - delaySamples;
        int readPosI = static_cast<int>(readPosF);
        float frac = readPosF - static_cast<float>(readPosI);
        if (frac < 0.0f) {
            frac += 1.0f;
            --readPosI;
        }
        readPosI &= mask;

        if (Aestra::Audio::DSP::ReverbSIMD::g_forceLinearInterpolation) {
            const int i2 = (readPosI + 1) & mask;
            return buffer[static_cast<size_t>(readPosI)] * (1.0f - frac) +
                   buffer[static_cast<size_t>(i2)] * frac;
        } else {
            const int i0 = (readPosI - 1) & mask;
            const int i2 = (readPosI + 1) & mask;
            const int i3 = (readPosI + 2) & mask;
            return Aestra::Audio::DSP::ReverbSIMD::cubicHermite(
                buffer[static_cast<size_t>(i0)],
                buffer[static_cast<size_t>(readPosI)],
                buffer[static_cast<size_t>(i2)],
                buffer[static_cast<size_t>(i3)],
                frac);
        }
    }

    int logicalFdnLength(size_t line, const ModeConstants& constants, float sampleScale, float size) const {
        const int maxSize = static_cast<int>(m_delayLines[line].size());
        return std::clamp(
            static_cast<int>(std::round(constants.fdnBase[line] * sampleScale * size)),
            100,
            std::max(100, maxSize - 12)
        );
    }

    int logicalDiffuserLength(size_t stage, float sampleScale, float size) const {
        const int maxSize = static_cast<int>(m_diffuserL[stage].size());
        return std::clamp(
            static_cast<int>(std::round(kBaseDiffuserLengths[stage] * sampleScale * size)),
            1,
            std::max(1, maxSize - 1)
        );
    }

    void processEarlyReflections(float left, float right, const ControlCache& control,
                                 float& earlyL, float& earlyR, int& pos) {
        if (m_earlyMask <= 0) return;

        const int mask = m_earlyMask;
        pos &= mask;

        m_earlyL[static_cast<size_t>(pos)] = left;
        m_earlyR[static_cast<size_t>(pos)] = right;

        for (size_t tap = 0; tap < kEarlyTapCount; ++tap) {
            const int readL = (pos - control.earlyDelayL[tap]) & mask;
            const int readR = (pos - control.earlyDelayR[tap]) & mask;
            const float gain = control.earlyGains[tap];
            earlyL += m_earlyL[static_cast<size_t>(readL)] * gain +
                      m_earlyR[static_cast<size_t>(readR)] * gain * 0.28f;
            earlyR += m_earlyR[static_cast<size_t>(readR)] * gain -
                      m_earlyL[static_cast<size_t>(readL)] * gain * 0.22f;
        }

        pos = (pos + 1) & mask;
    }

    void processDiffusers(float& left, float& right, const ControlCache& control,
                          std::array<int, kDiffuserCount>& pos) {
        for (size_t stage = 0; stage < kDiffuserCount; ++stage) {
            auto& bufL = m_diffuserL[stage];
            auto& bufR = m_diffuserR[stage];
            if (bufL.empty() || bufR.empty()) continue;

            const int mask = m_diffuserMasks[stage];
            const int len = control.diffuserLengths[stage];
            int p = pos[stage] & mask;
            const int readP = (p - len) & mask;

            const float delayedL = bufL[static_cast<size_t>(readP)];
            const float delayedR = bufR[static_cast<size_t>(readP)];
            const float yL = delayedL - control.diffusionG * left;
            const float yR = delayedR - control.diffusionG * right;
            bufL[static_cast<size_t>(p)] = left + control.diffusionG * yL;
            bufR[static_cast<size_t>(p)] = right + control.diffusionG * yR;
            left = yL;
            right = yR;
            pos[stage] = (p + 1) & mask;
        }
    }

    void processPlatePostAllpass(float& left, float& right,
                                 std::array<int, kPlatePostAllpassCount>& pos) {
        const float g = 0.32f;
        for (size_t stage = 0; stage < kPlatePostAllpassCount; ++stage) {
            auto& bufL = m_platePostL[stage];
            auto& bufR = m_platePostR[stage];
            if (bufL.empty() || bufR.empty()) continue;

            const int mask = m_platePostMasks[stage];
            const int delayLength = m_platePostDelayLengths[stage];
            const int p = pos[stage] & mask;
            const int readP = (p - delayLength) & mask;

            const float delayedL = bufL[static_cast<size_t>(readP)];
            const float delayedR = bufR[static_cast<size_t>(readP)];
            const float yL = delayedL - g * left;
            const float yR = delayedR - g * right;
            bufL[static_cast<size_t>(p)] = left + g * yL;
            bufR[static_cast<size_t>(p)] = right + g * yR;
            left = yL;
            right = yR;
            pos[stage] = (p + 1) & mask;
        }
    }

    void processWetVoicing(float& left, float& right, Mode mode, const ControlCache& control) {
        const auto& c = m_boxCutCoeff[static_cast<size_t>(mode)];
        const float inL = left;
        const float inR = right;
        left = c[0] * inL + m_boxCutZ1L;
        m_boxCutZ1L = c[1] * inL - c[3] * left + m_boxCutZ2L;
        m_boxCutZ2L = c[2] * inL - c[4] * left;
        right = c[0] * inR + m_boxCutZ1R;
        m_boxCutZ1R = c[1] * inR - c[3] * right + m_boxCutZ2R;
        m_boxCutZ2R = c[2] * inR - c[4] * right;

        m_wetToneStateL += control.wetToneCoeff * (left - m_wetToneStateL);
        m_wetToneStateR += control.wetToneCoeff * (right - m_wetToneStateR);
        left = m_wetToneStateL + (left - m_wetToneStateL) * control.wetAirBlend;
        right = m_wetToneStateR + (right - m_wetToneStateR) * control.wetAirBlend;

        left = sanitize(left);
        right = sanitize(right);
    }

    static int wrapIndex(int index, int size) {
        if (size <= 0) return 0;
        index %= size;
        if (index < 0) index += size;
        return index;
    }

    static float sanitize(float value) {
        if (value != value || value <= -32.0f || value >= 32.0f ||
            (value > -1.0e-20f && value < 1.0e-20f)) {
            return 0.0f;
        }
        return value;
    }

    // Session 006: restored for random modulation component.
    static float nextRandomBipolar(uint32_t& state) {
        state = state * 1664525u + 1013904223u;
        const float normalized = static_cast<float>((state >> 8) & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
        return normalized * 2.0f - 1.0f;
    }

    static void normalizeOscillator(float& s, float& c) {
        const float mag2 = s * s + c * c;
        if (mag2 > 0.25f && mag2 < 4.0f) {
            const float scale = 1.0f / std::sqrt(mag2);
            s *= scale;
            c *= scale;
        } else {
            s = 0.0f;
            c = 1.0f;
        }
    }

    // Compute peaking EQ coefficients (b0,b1,b2,a1,a2 with a0 normalized to 1).
    // dbGain: positive = boost, negative = cut.  Q: bandwidth at -3dB (or -dBgain/2 for peaking).
    // Uses standard RBJ Audio EQ Cookbook formula (same equation works for boost and cut).
    static std::array<float, 5> peakingCoefficients(float dbGain, float freqHz, float sampleRate, float Q) {
        const float A = std::sqrt(std::pow(10.0f, dbGain / 20.0f));
        const float w0 = 2.0f * static_cast<float>(M_PI) * freqHz / sampleRate;
        const float cosw0 = std::cos(w0);
        const float sinw0 = std::sin(w0);
        const float alpha = sinw0 / (2.0f * Q);

        const float b0 = 1.0f + alpha * A;
        const float b1 = -2.0f * cosw0;
        const float b2 = 1.0f - alpha * A;
        const float a0 = 1.0f + alpha / A;
        const float a1 = -2.0f * cosw0;
        const float a2 = 1.0f - alpha / A;

        const float invA0 = 1.0f / a0;
        return {b0 * invA0, b1 * invA0, b2 * invA0, a1 * invA0, a2 * invA0};
    }

    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    std::atomic<bool> m_active{false};
    std::array<std::atomic<float>, kParamCount> m_params{};
    std::array<float, kParamCount> m_smoothedParams{};

    std::array<std::vector<float>, kFDNLineCount> m_delayLines;
    std::array<int, kFDNLineCount> m_delayLineMasks{};
    std::array<int, kFDNLineCount> m_delayLengths{};
    std::array<int, kFDNLineCount> m_delayPos{};
    std::array<float, kFDNLineCount> m_dampingState{};
    std::array<float, kFDNLineCount> m_lowDampState{};
    std::array<float, kFDNLineCount> m_lfoPhase{};
    std::array<float, kFDNLineCount> m_lfoPhase2{};
    std::array<float, kFDNLineCount> m_lfoSin{};
    std::array<float, kFDNLineCount> m_lfoCos{};
    std::array<float, kFDNLineCount> m_lfoSin2{};
    std::array<float, kFDNLineCount> m_lfoCos2{};
    std::array<float, kFDNLineCount> m_randomMod{};
    std::array<float, kFDNLineCount> m_randomTarget{};
    std::array<int, kFDNLineCount> m_randomCounter{};
    std::array<uint32_t, kFDNLineCount> m_randomState{};

    std::array<std::vector<float>, kDiffuserCount> m_diffuserL;
    std::array<std::vector<float>, kDiffuserCount> m_diffuserR;
    std::array<int, kDiffuserCount> m_diffuserLengths{};
    std::array<int, kDiffuserCount> m_diffuserMasks{};
    std::array<int, kDiffuserCount> m_diffuserPos{};

    std::array<std::vector<float>, kPlatePostAllpassCount> m_platePostL;
    std::array<std::vector<float>, kPlatePostAllpassCount> m_platePostR;
    std::array<int, kPlatePostAllpassCount> m_platePostDelayLengths{};
    std::array<int, kPlatePostAllpassCount> m_platePostMasks{};
    std::array<int, kPlatePostAllpassCount> m_platePostPos{};

    std::vector<float> m_earlyL;
    std::vector<float> m_earlyR;
    int m_earlyMask = 0;
    int m_earlyPos = 0;

    std::vector<float> m_predelayL;
    std::vector<float> m_predelayR;
    int m_maxPredelaySamples = 1;
    int m_predelayMask = 0;
    int m_predelayPos = 0;

    // Session 012: gentle peaking EQ to tame Plate metallic ringing (~562 Hz).
    // Coefficients precomputed at init; only applied in Plate mode.
    std::array<float, 5> m_platePeakCoeff{}; // b0, b1, b2, a1, a2 (a0 normalized to 1)
    std::array<std::array<float, 5>, 3> m_boxCutCoeff{};
    float m_platePeakZ1L = 0.0f;
    float m_platePeakZ2L = 0.0f;
    float m_platePeakZ1R = 0.0f;
    float m_platePeakZ2R = 0.0f;
    float m_boxCutZ1L = 0.0f;
    float m_boxCutZ2L = 0.0f;
    float m_boxCutZ1R = 0.0f;
    float m_boxCutZ2R = 0.0f;
    float m_wetToneStateL = 0.0f;
    float m_wetToneStateR = 0.0f;
    // Session 004: mud cleanup one-pole HP filter state
    std::array<float, 3> m_mudHpCoeff{};  // per-mode alpha coefficients
    float m_mudHpStateL = 0.0f;
    float m_mudHpStateR = 0.0f;

    // TODO: separate hi/lo damping (kHFDamping + kLFDamping) for more tonal control
    // TODO: Convolution mode — IR loader using same drag system as Arsenal
    // TODO: Early reflections as discrete pre-FDN tapped delay line network

#ifdef AESTRA_REVERB_PROFILE
    mutable std::array<StageProfileData, static_cast<size_t>(ProfileStage::kStageCount)> m_profileData{};
#endif
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
