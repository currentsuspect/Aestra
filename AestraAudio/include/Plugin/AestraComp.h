// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraComp V1 — feed-forward compressor. Zero latency with oversampling Off
// (the default); 2x/4x oversampling adds DSP::Oversampler::kReportedLatency
// samples, reported via getLatencySamples().

#pragma once

#include "DSP/Oversampler.h"
#include "RealtimeThreadGuard.h"
#include "Plugin/InternalPluginBase.h"
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

class AestraComp : public InternalPluginBase {
public:
    // Canonical magic, carrying the v2 value. The reset renumbers the version
    // field, not the magic, so a pre-reset blob is still recognisably this
    // plugin's and is rejected on version rather than on magic.
    static constexpr uint32_t kStateMagic = 0x434D5002; // 'CMP' v2

    // FFT spectrum data for analyzer display
    static constexpr uint32_t kFftSize = 2048;
    static constexpr uint32_t kFftBins = kFftSize / 2 + 1; // 1025
    static constexpr uint32_t kFftStages = []() constexpr {
        uint32_t n = kFftSize, s = 0;
        while (n > 1) { n >>= 1; ++s; }
        return s;
    }(); // log2(2048) = 11
    static constexpr float kFftDisplayMinDb = -48.0f;

    struct FftSpectrum {
        float inputBins[kFftBins]{};
        float outputBins[kFftBins]{};
    };

    enum CompMode : uint32_t {
        kModeClean = 0,
        kModeClassic = 1,
        kModeOptical = 2,
    };

    enum Param : uint32_t {
        kThreshold = 0, // -60 dB to 0 dB
        kRatio,         // 1:1 to 20:1
        kAttack,        // 0.1 ms to 100 ms
        kRelease,       // 10 ms to 1000 ms
        kMakeup,        // 0 dB to +24 dB
        kKnee,          // 0 dB to 24 dB
        kMix,           // 0% to 100%
        kBypass,
        kInputGain,    // -24 dB to +24 dB
        kOutputGain,   // -24 dB to +24 dB
        kDetectorHPF,  // Off, then 20 Hz to 500 Hz
        kCompMode,     // 0=Clean, 1=Classic, 2=Optical
        kOversampling, // 0=Off, 1=2x, 2=4x (nonlinear stage anti-aliasing)
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
        kQuality
    };


    AestraComp() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        m_sampleRate = std::max(1.0, sampleRate);
        m_maxBlockSize = maxBlockSize;
        // Seed parameter defaults only on the first initialization of a fresh
        // instance. EffectChain::prepare() re-calls initialize() on the live
        // instance during sample-rate/device changes and must preserve the
        // user's current parameter values (and any loaded project state).
        seedDefaultsOnce();
        m_osL.prepareKernels();
        m_osR.prepareKernels();
        applyOversamplingConfig();
        m_oversamplingDirty.store(false, std::memory_order_release);
        resetRuntimeState();
        snapSmoothedParamsToTargets();
        updateDetectorHPF();
        updateRmsCoeff();
        return true;
    }

    void shutdown() override {}

    void activate() override {
        m_active.store(true, std::memory_order_relaxed);
        m_osL.prepareKernels();
        m_osR.prepareKernels();
        applyOversamplingConfig();
        m_oversamplingDirty.store(false, std::memory_order_release);
        resetRuntimeState();
        snapSmoothedParamsToTargets();
        updateDetectorHPF();
        updateRmsCoeff();
    }

    void deactivate() override { m_active.store(false, std::memory_order_relaxed); }
    bool isActive() const override { return m_active.load(std::memory_order_relaxed); }

    void process(const float* const* inputs, float** outputs,
                 uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* midiInput = nullptr,
                 MidiBuffer* midiOutput = nullptr) AESTRA_RT_NONBLOCKING override {
        (void)midiInput;
        (void)midiOutput;

        if (m_oversamplingDirty.exchange(false, std::memory_order_acq_rel)) {
            applyOversamplingConfig();
        }
        const uint32_t osFactor = m_osFactor;

        if (!m_active.load(std::memory_order_relaxed) ||
            isBypassed()) {
            if (osFactor <= 1u) {
                copyOrClear(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            } else {
                // With oversampling active the plugin reports nonzero latency;
                // internal bypass must delay by the same amount to stay aligned.
                copyDelayed(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            }
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

        // FFT accumulation — mono sum of input and output
        float fftInBlock[kBlockSize]{};
        float fftOutBlock[kBlockSize]{};

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

            const uint32_t prevMode = m_mode;
            const float rawMode = paramValue(kCompMode) * 2.0f;
            m_mode = static_cast<uint32_t>(rawMode + 0.5f);
            if (m_mode != prevMode) {
                m_feedbackL = 0.0f;
                m_feedbackR = 0.0f;
            }

            // Envelope coefficients live at the detector rate: native fs with
            // oversampling Off, fs * factor when the nonlinear stage runs
            // oversampled. m_detectorRate == (float)m_sampleRate for factor 1.
            const float attackCoeff = std::exp(-1.0f / (m_detectorRate * attackSec));
            const float releaseCoeff = std::exp(-1.0f / (m_detectorRate * releaseSec));
            const float makeupOut = makeupLinear * outputLinear;

            for (uint32_t i = blockStart; i < blockEnd; ++i) {
            const float rawL = readInput(inputs, numInputChannels, 0, i);
            const float rawR = stereo ? readInput(inputs, numInputChannels, 1, i) : rawL;
            const float dryL = sanitizeSample(rawL);
            const float dryR = sanitizeSample(rawR);
            const float inL = dryL * inputLinear;
            const float inR = dryR * inputLinear;
            blockInputPeak = std::max(blockInputPeak, std::max(std::abs(inL), std::abs(inR)));

            float wetL = 0.0f;
            float wetR = 0.0f;
            float outL;
            float outR;
            if (osFactor <= 1u) {
                processCore(inL, inR, attackCoeff, releaseCoeff, thresholdDb, ratio, kneeDb, makeupLinear, env, hpfXL,
                            hpfYL, hpfXR, hpfYR, blockGainReductionDb, wetL, wetR);
                outL = (dryL * (1.0f - mix) + wetL * mix) * outputLinear;
                outR = (dryR * (1.0f - mix) + wetR * mix) * outputLinear;
                if (m_mode == kModeClassic) {
                    m_feedbackL = outL;
                    m_feedbackR = outR;
                }
            } else {
                // Oversampled nonlinear stage: detector + gain computer + VCA run
                // at m_detectorRate; only the wet path is filtered, the dry path
                // is delayed by the same amount so mix stays phase-aligned.
                float osInL[4];
                float osInR[4];
                float osWetL[4];
                float osWetR[4];
                m_osL.upsample(inL, osInL);
                m_osR.upsample(inR, osInR);
                for (uint32_t sub = 0; sub < osFactor; ++sub) {
                    processCore(osInL[sub], osInR[sub], attackCoeff, releaseCoeff, thresholdDb, ratio, kneeDb,
                                makeupLinear, env, hpfXL, hpfYL, hpfXR, hpfYR, blockGainReductionDb, osWetL[sub],
                                osWetR[sub]);
                    if (m_mode == kModeClassic) {
                        // Feedback topology needs an output estimate per subsample;
                        // synthesize it from the oversampled dry/wet pair.
                        m_feedbackL = (osInL[sub] * (1.0f - mix) + osWetL[sub] * mix) * outputLinear;
                        m_feedbackR = (osInR[sub] * (1.0f - mix) + osWetR[sub] * mix) * outputLinear;
                    }
                }
                wetL = m_osL.downsample(osWetL);
                wetR = m_osR.downsample(osWetR);
                float dryDelayedL;
                float dryDelayedR;
                pushDryDelay(dryL, dryR, dryDelayedL, dryDelayedR);
                outL = (dryDelayedL * (1.0f - mix) + wetL * mix) * outputLinear;
                outR = (dryDelayedR * (1.0f - mix) + wetR * mix) * outputLinear;
            }
            blockOutputPeak = std::max(blockOutputPeak, std::max(std::abs(outL), std::abs(outR)));

            // Accumulate mono sum for FFT
            const uint32_t fftIdx = i - blockStart;
            fftInBlock[fftIdx] = (inL + inR) * 0.5f;
            fftOutBlock[fftIdx] = (outL + outR) * 0.5f;

            if (numOutputChannels > 0 && outputs[0]) outputs[0][i] = flushDenormal(outL);
            if (numOutputChannels > 1 && outputs[1]) outputs[1][i] = flushDenormal(outR);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
            for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                if (outputs[ch]) outputs[ch][i] = 0.0f;
            }
#pragma GCC diagnostic pop
            }

            // Feed block samples into FFT pipeline
            processFftBlock(fftInBlock, fftOutBlock, blockEnd - blockStart);
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

    // The base's setParameter does the storage, the non-finite rejection and the
    // clamp; this hook carries everything Comp did on top of them.
    //
    // Two groups, both of which used to live inside setParameter:
    //
    // 1. Dirty flags, read by the audio thread to decide whether derived state
    //    needs recomputing. Release ordering pairs with the acquire the reader
    //    uses.
    //
    // 2. The pre-first-process snap. Until the compressor has processed a block,
    //    a changed parameter is snapped straight into the smoothed value instead
    //    of gliding toward it. Without this the first block after a parameter
    //    change compresses against the PREVIOUS smoothed value, which is
    //    measurable: the transparent-path test (ratio 1:1 set after activate)
    //    reads -12.8 dB of error instead of -91 dB. Smooth glide is correct for
    //    a running compressor and wrong before the first block.
    void onParameterChanged(uint32_t id, float value) AESTRA_RT_NONBLOCKING override {
        if (id == kDetectorHPF) {
            m_detectorHPFDirty.store(true, std::memory_order_release);
        } else if (id == kOversampling) {
            m_oversamplingDirty.store(true, std::memory_order_release);
        }

        if (!m_hasProcessed.load(std::memory_order_relaxed)) {
            switch (id) {
            case kThreshold: m_thresholdSmoothed = value; break;
            case kRatio: m_ratioSmoothed = value; break;
            case kAttack: m_attackSmoothed = value; break;
            case kRelease: m_releaseSmoothed = value; break;
            case kMakeup: m_makeupSmoothed = value; break;
            case kKnee: m_kneeSmoothed = value; break;
            case kMix: m_mixSmoothed = value; break;
            case kInputGain: m_inputGainSmoothed = value; break;
            case kOutputGain: m_outputGainSmoothed = value; break;
            default: break;
            }
        }
    }

    // Declarative parameter table: this is the whole plugin-side surface now.
    // The rows are the table this plugin already shipped in getParameters(),
    // unchanged, so this is a type rename rather than a behavioural change.
    inline static constexpr ParamSpec kSpecs[] = {
        {kThreshold, "Threshold", "THR", "dB", 0.6667f, 0.0f, 1.0f, true},
        {kRatio, "Ratio", "RAT", ":1", 0.1579f, 0.0f, 1.0f, true},
        {kAttack, "Attack", "ATK", "ms", 0.0991f, 0.0f, 1.0f, true},
        {kRelease, "Release", "REL", "ms", 0.1414f, 0.0f, 1.0f, true},
        {kMakeup, "Makeup Gain", "MKP", "dB", 0.0f, 0.0f, 1.0f, true},
        {kKnee, "Knee", "KNE", "dB", 0.0f, 0.0f, 1.0f, true},
        {kMix, "Mix", "MIX", "%", 1.0f, 0.0f, 1.0f, true},
        {kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1},
        {kInputGain, "Input Gain", "IN", "dB", 0.5f, 0.0f, 1.0f, true},
        {kOutputGain, "Output Gain", "OUT", "dB", 0.5f, 0.0f, 1.0f, true},
        {kDetectorHPF, "Detector HPF", "HPF", "Hz", 0.0f, 0.0f, 1.0f, true},
        {kCompMode, "Mode", "MODE", "", 0.0f, 0.0f, 1.0f, true},
        {kOversampling, "Oversampling", "OS", "", 0.0f, 0.0f, 1.0f, true, false, false, 2},
    };

    const ParamSpec* paramSpecs() const override { return kSpecs; }
    uint32_t paramSpecCount() const override { return kParamCount; }
    AESTRA_VALIDATE_PARAM_SPECS(kSpecs, kParamCount);
    uint32_t stateMagic() const override { return kStateMagic; }


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
        case kCompMode: {
            auto mode = static_cast<uint32_t>(std::round(std::clamp(v * 2.0f, 0.0f, 2.0f)));
            if (mode == kModeClassic) return "Classic";
            if (mode == kModeOptical) return "Optical";
            return "Clean";
        }
        case kOversampling: {
            const uint32_t factor = oversamplingFactorFromNorm(v);
            if (factor == 4u)
                return "4x";
            if (factor == 2u)
                return "2x";
            return "Off";
        }
        default: return "";
        }
    }

// Canonical base format, version 1, with NO override at all -- the first
    // built-in to reach this. This plugin previously wrote version 5 into a
    // 24-slot array to carry 13 live parameters, then overwrote seven of those
    // slots with frozen constants:
    //
    //     blob.params[kLegacyLookaheadIndex]      = 0.0f;
    //     blob.params[kLegacyStereoLinkIndex]     = 1.0f;
    //     blob.params[kLegacyStereoLinkLawIndex]  = 0.0f;
    //     blob.params[kLegacySCLPFIndex]          = 0.0f;
    //     blob.params[kLegacySCListenIndex]       = 0.0f;
    //     blob.params[kLegacyStyleIndex]          = 0.0f;
    //     blob.params[kLegacyQualityIndex]        = 0.5f;
    //
    // Those were constants that happened to live in a float array, not
    // parameters. They are deleted rather than preserved: a frozen value does
    // not belong in a persisted format, and preserving them would require
    // Comp to keep hand-writing a blob the base already owns. FD-24 records the
    // decision; the blob is now 60 bytes instead of 104.
    //
    // kLegacyParamCount and the fourteen kLegacy*Index constants go with them --
    // they have no remaining reader.

    // Kept as overrides: the base's editor stubs report hasEditor() == false and
    // size {0,0}, and this plugin has a real editor.

    bool hasEditor() const override { return true; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {680, 555}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override {
        // NOTE: latency changes when kOversampling toggles Off <-> On, but the
        // host currently only re-reads plugin latency on graph rebuild
        // (insert/remove/bypass/reload) — see #270. Both oversampled modes
        // report the same value so switching 2x <-> 4x never misaligns.
        return m_reportedLatency.load(std::memory_order_relaxed);
    }
    uint32_t getTailSamples() const override { return 256; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

    float getCurrentGainReductionDb() const { return m_currentGainReductionDb.load(std::memory_order_relaxed); }
    float getInputLevel() const { return m_inputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return m_outputLevel.load(std::memory_order_relaxed); }

    FftSpectrum getFftSpectrum() const {
        return m_fftBuffers[m_fftReadIndex.load(std::memory_order_acquire)];
    }

private:
    static constexpr float kMinDb = -120.0f;
    static constexpr float kMaxSample = 16.0f;
    static constexpr uint32_t kBlockSize = 16;
    static constexpr float kRmsWindowSec = 0.01f;

    void loadDefaults() {
        seedDefaults();
        snapSmoothedParamsToTargets();
        updateDetectorHPF();
        updateRmsCoeff();
    }

    /// One step of the nonlinear stage (detector -> envelope -> gain computer ->
    /// VCA) at the detector rate. With oversampling Off this is called once per
    /// input sample and matches the pre-oversampling implementation exactly;
    /// with 2x/4x it is called once per subsample.
    // Optical-mode detector constants. Hoisted to class scope from
    // function-local `static constexpr` inside processCore(): Clang refuses any
    // static local in a function annotated AESTRA_RT_NONBLOCKING, because
    // thread-safe static initialisation is a guard variable and a guard on an
    // audio-thread path is a lock. The old code declared K_OPTICAL_ENVELOPE_REF
    // TWICE, once per branch; both were 0.1f, so there is now one declaration.
    inline static constexpr float K_OPTICAL_WINDOW_MIN = 0.005f;
    inline static constexpr float K_OPTICAL_WINDOW_MAX = 0.030f;
    inline static constexpr float K_OPTICAL_ENVELOPE_REF = 0.1f;

    void processCore(float inL, float inR, float attackCoeff, float releaseCoeff, float thresholdDb, float ratio,
                     float kneeDb, float makeupLinear, float& env, float& hpfXL, float& hpfYL, float& hpfXR,
                     float& hpfYR, float& blockGainReductionDb, float& wetL, float& wetR)  AESTRA_RT_NONBLOCKING {
        float detInputL, detInputR;
        if (m_mode == kModeClassic) {
            detInputL = m_feedbackL;
            detInputR = m_feedbackR;
        } else {
            detInputL = inL;
            detInputR = inR;
        }

        float detL = detInputL;
        float detR = detInputR;
        if (m_detectorHPFEnabled) {
            detL = m_hpfA0 * detL + m_hpfA1 * hpfXL + m_hpfB1 * hpfYL;
            hpfXL = detInputL;
            hpfYL = detL;
            detR = m_hpfA0 * detR + m_hpfA1 * hpfXR + m_hpfB1 * hpfYR;
            hpfXR = detInputR;
            hpfYR = detR;
        }

        const float powerInstant = (detL * detL + detR * detR) * 0.5f;
        float sampleRmsCoeff;
        if (m_mode == kModeOptical) {
            float t = std::clamp(m_rmsEnvelope / K_OPTICAL_ENVELOPE_REF, 0.0f, 1.0f);
            float window = K_OPTICAL_WINDOW_MAX - t * (K_OPTICAL_WINDOW_MAX - K_OPTICAL_WINDOW_MIN);
            // Gate exp() — only recompute when window changes by >0.5ms
            if (std::abs(window - m_prevOpticalWindow) > 0.0005f || m_prevOpticalWindow < 0.0f) {
                m_prevOpticalWindow = window;
                m_opticalRmsCoeff = std::exp(-1.0f / (m_detectorRate * window));
            }
            sampleRmsCoeff = m_opticalRmsCoeff;
        } else {
            sampleRmsCoeff = m_rmsCoeff;
            m_prevOpticalWindow = -1.0f; // force recompute on next optical entry
        }
        m_rmsEnvelope = powerInstant + sampleRmsCoeff * (m_rmsEnvelope - powerInstant);
        const float detector = std::sqrt(m_rmsEnvelope);

        float aCoeff, rCoeff;
        if (m_mode == kModeOptical) {
            float grNorm = std::clamp(m_rmsEnvelope / K_OPTICAL_ENVELOPE_REF, 0.0f, 1.0f);
            aCoeff = attackCoeff * (1.0f + grNorm * 2.0f);
            rCoeff = releaseCoeff * (1.0f - grNorm * 0.5f);
        } else {
            aCoeff = attackCoeff;
            rCoeff = releaseCoeff;
        }
        const float coeff = detector > env ? aCoeff : rCoeff;
        env = coeff * env + (1.0f - coeff) * detector;
        if (env != env || env < 1.0e-12f)
            env = 0.0f; // NaN check + denormal flush

        const float detectorDb = linearToDb(env);
        const float reductionDb = computeGainReductionDb(detectorDb, thresholdDb, ratio, kneeDb);
        const float reductionLinear = dbToLinear(reductionDb);
        blockGainReductionDb = std::max(blockGainReductionDb, -reductionDb);

        wetL = (inL * reductionLinear) * makeupLinear;
        wetR = (inR * reductionLinear) * makeupLinear;
    }

    static uint32_t oversamplingFactorFromNorm(float value) {
        const auto step = static_cast<uint32_t>(std::round(std::clamp(value * 2.0f, 0.0f, 2.0f)));
        if (step == 2u)
            return 4u;
        if (step == 1u)
            return 2u;
        return 1u;
    }

    /// Reconfigure the oversampling stage from the current parameter value.
    /// Kernels are designed once in initialize/activate (non-RT); this path
    /// only switches the factor, resets filter state, and rescales the
    /// detector-rate coefficients, so it is safe on the audio thread.
    void applyOversamplingConfig() {
        const uint32_t factor = oversamplingFactorFromNorm(paramValue(kOversampling));
        m_osL.setFactor(factor);
        m_osR.setFactor(factor);
        m_osFactor = m_osL.factor();
        m_detectorRate = static_cast<float>(m_sampleRate) * static_cast<float>(m_osFactor);
        m_dryDelayL.fill(0.0f);
        m_dryDelayR.fill(0.0f);
        m_dryDelayPos = 0;
        m_reportedLatency.store(m_osFactor > 1u ? DSP::Oversampler::kReportedLatency : 0u, std::memory_order_relaxed);
        m_prevOpticalWindow = -1.0f;
        updateRmsCoeff();
        updateDetectorHPF();
    }

    void pushDryDelay(float inL, float inR, float& outL, float& outR) {
        outL = m_dryDelayL[m_dryDelayPos];
        outR = m_dryDelayR[m_dryDelayPos];
        m_dryDelayL[m_dryDelayPos] = inL;
        m_dryDelayR[m_dryDelayPos] = inR;
        m_dryDelayPos = (m_dryDelayPos + 1u) % DSP::Oversampler::kReportedLatency;
    }

    /// Bypass copy delayed by the reported oversampling latency. Channels >= 2
    /// pass through like copyOrClear (the active path zeroes them instead, which
    /// matches the pre-oversampling bypass behavior).
    void copyDelayed(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                     uint32_t numFrames) {
        for (uint32_t i = 0; i < numFrames; ++i) {
            const float inL = readInput(inputs, numInputChannels, 0, i);
            const float inR = readInput(inputs, numInputChannels, 1, i);
            float outL;
            float outR;
            pushDryDelay(inL, inR, outL, outR);
            if (numOutputChannels > 0 && outputs[0])
                outputs[0][i] = outL;
            if (numOutputChannels > 1 && outputs[1])
                outputs[1][i] = outR;
        }
        for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
            if (outputs[ch] && ch < numInputChannels && inputs[ch]) {
                std::memcpy(outputs[ch], inputs[ch], numFrames * sizeof(float));
            } else if (outputs[ch]) {
                std::memset(outputs[ch], 0, numFrames * sizeof(float));
            }
        }
    }

    void resetRuntimeState() {
        m_env = 0.0f;
        m_rmsEnvelope = 0.0f;
        m_rmsCoeff = 0.0f;
        m_hpfXL = 0.0f;
        m_hpfYL = 0.0f;
        m_hpfXR = 0.0f;
        m_hpfYR = 0.0f;
        m_feedbackL = 0.0f;
        m_feedbackR = 0.0f;
        m_osL.reset();
        m_osR.reset();
        m_dryDelayL.fill(0.0f);
        m_dryDelayR.fill(0.0f);
        m_dryDelayPos = 0;
        m_currentGainReductionDb.store(0.0f, std::memory_order_relaxed);
        m_inputLevel.store(0.0f, std::memory_order_relaxed);
        m_outputLevel.store(0.0f, std::memory_order_relaxed);
        m_hasProcessed.store(false, std::memory_order_relaxed);
        m_fftWritePos = 0;
        m_fftBuffers[0] = {};
        m_fftBuffers[1] = {};
        m_fftReadIndex.store(0, std::memory_order_relaxed);
        initHannWindow();
        updateRmsCoeff();
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
        const float dt = 1.0f / m_detectorRate;
        const float alpha = rc / (rc + dt);
        m_hpfA0 = alpha;
        m_hpfA1 = -alpha;
        m_hpfB1 = alpha;
    }

    void updateRmsCoeff() { m_rmsCoeff = std::exp(-1.0f / std::max(1.0f, m_detectorRate * kRmsWindowSec)); }

    void initHannWindow() {
        for (uint32_t i = 0; i < kFftSize; ++i) {
            m_hannWindow[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979323846f
                                                         * static_cast<float>(i) / static_cast<float>(kFftSize - 1)));
        }
        for (uint32_t len = 2, stage = 0; stage < kFftStages; len <<= 1, ++stage) {
            const float angle = -2.0f * 3.14159265358979323846f / static_cast<float>(len);
            m_twiddleRe[stage] = std::cos(angle);
            m_twiddleIm[stage] = std::sin(angle);
        }
    }

    void processFftBlock(const float* input, const float* output, uint32_t count) {
        uint32_t remaining = count;
        uint32_t inOffset = 0;
        while (remaining > 0) {
            const uint32_t space = kFftSize - m_fftWritePos;
            const uint32_t toCopy = std::min(remaining, space);
            for (uint32_t i = 0; i < toCopy; ++i) {
                m_fftAccumInput[m_fftWritePos + i] = input[inOffset + i];
                m_fftAccumOutput[m_fftWritePos + i] = output[inOffset + i];
            }
            m_fftWritePos += toCopy;
            inOffset += toCopy;
            remaining -= toCopy;
            if (m_fftWritePos >= kFftSize) {
                publishFft();
                m_fftWritePos = 0;
            }
        }
    }

    void publishFft() {
        const uint32_t writeSlot = m_fftReadIndex.load(std::memory_order_relaxed) ^ 1u;
        FftSpectrum& spec = m_fftBuffers[writeSlot];

        constexpr float kMinPower = 1.0e-24f;
        constexpr float kMinDb = -90.0f;
        constexpr float kInvN = 1.0f / static_cast<float>(kFftSize);
        constexpr float kInvN2 = kInvN * kInvN;

        // Process input spectrum
        {
            float* z = m_fftScratch;
            for (uint32_t i = 0; i < kFftSize; ++i) {
                z[i * 2] = m_fftAccumInput[i] * m_hannWindow[i];
                z[i * 2 + 1] = 0.0f;
            }
            computeComplexFft(z, kFftSize);
            for (uint32_t i = 0; i < kFftBins; ++i) {
                const float re = z[i * 2];
                const float im = z[i * 2 + 1];
                const float power = std::max((re * re + im * im) * kInvN2, kMinPower);
                spec.inputBins[i] = fastPowerToDb(power);
            }
        }

        // Process output spectrum
        {
            float* z = m_fftScratch;
            for (uint32_t i = 0; i < kFftSize; ++i) {
                z[i * 2] = m_fftAccumOutput[i] * m_hannWindow[i];
                z[i * 2 + 1] = 0.0f;
            }
            computeComplexFft(z, kFftSize);
            for (uint32_t i = 0; i < kFftBins; ++i) {
                const float re = z[i * 2];
                const float im = z[i * 2 + 1];
                const float power = std::max((re * re + im * im) * kInvN2, kMinPower);
                spec.outputBins[i] = fastPowerToDb(power);
            }
        }

        m_fftReadIndex.store(writeSlot, std::memory_order_release);
    }

    // Fast power-to-dB: 10*log10(power) via IEEE 754 bit trick for log2
    static float fastPowerToDb(float power) {
        // Reinterpret float as int to extract exponent
        uint32_t bits;
        std::memcpy(&bits, &power, sizeof(bits));
        const int exponent = static_cast<int>((bits >> 23) & 0xFF) - 127;
        // Clear exponent, set to mantissa in [0.5, 1.0)
        bits = (bits & 0x007FFFFF) | 0x3F800000;
        float mantissa;
        std::memcpy(&mantissa, &bits, sizeof(mantissa));
        // Polynomial approx of log2(mantissa) on [0.5, 1.0]
        // log2(x) ≈ x - 1 - 0.3413*(x-1)^2 for x in [0.5, 1.0]
        const float x = mantissa - 1.0f;
        const float log2approx = x - 0.3413f * x * x;
        const float log2val = static_cast<float>(exponent) + log2approx;
        // 10*log10(power) = 10 * log2(power) / log2(10) = 3.01029995664 * log2(power)
        return 3.01029995664f * log2val;
    }

    void computeComplexFft(float* z, uint32_t n) {
        // Bit-reversal permutation
        for (uint32_t i = 1, j = 0; i < n; ++i) {
            uint32_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                std::swap(z[i * 2], z[j * 2]);
                std::swap(z[i * 2 + 1], z[j * 2 + 1]);
            }
        }

        // Cooley-Tukey butterfly with precomputed twiddle factors
        for (uint32_t len = 2, stage = 0; len <= n; len <<= 1, ++stage) {
            const float wRe = m_twiddleRe[stage];
            const float wIm = m_twiddleIm[stage];
            for (uint32_t i = 0; i < n; i += len) {
                float curRe = 1.0f, curIm = 0.0f;
                for (uint32_t j = 0; j < len / 2; ++j) {
                    const uint32_t u = i + j;
                    const uint32_t v = u + len / 2;
                    const float vRe = z[v * 2];
                    const float vIm = z[v * 2 + 1];
                    const float tRe = curRe * vRe - curIm * vIm;
                    const float tIm = curRe * vIm + curIm * vRe;
                    z[v * 2] = z[u * 2] - tRe;
                    z[v * 2 + 1] = z[u * 2 + 1] - tIm;
                    z[u * 2] += tRe;
                    z[u * 2 + 1] += tIm;
                    const float newCurRe = curRe * wRe - curIm * wIm;
                    curIm = curRe * wIm + curIm * wRe;
                    curRe = newCurRe;
                }
            }
        }
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
        return std::exp(db * 0.11512925464970229f); // ln(10)/20
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
    float m_rmsCoeff = 0.0f;
    float m_hpfXL = 0.0f;
    float m_hpfYL = 0.0f;
    float m_hpfXR = 0.0f;
    float m_hpfYR = 0.0f;
    float m_hpfA0 = 1.0f;
    float m_hpfA1 = 0.0f;
    float m_hpfB1 = 0.0f;
    bool m_detectorHPFEnabled = false;
    float m_feedbackL = 0.0f;
    float m_feedbackR = 0.0f;
    uint32_t m_mode = 0;
    float m_prevOpticalWindow = -1.0f;
    float m_opticalRmsCoeff = 0.0f;

    // Oversampling (issue #228). m_detectorRate is the rate the nonlinear
    // stage runs at: (float)m_sampleRate * m_osFactor.
    std::atomic<bool> m_oversamplingDirty{false};
    std::atomic<uint32_t> m_reportedLatency{0};
    uint32_t m_osFactor = 1;
    float m_detectorRate = 48000.0f;
    DSP::Oversampler m_osL;
    DSP::Oversampler m_osR;
    std::array<float, DSP::Oversampler::kReportedLatency> m_dryDelayL{};
    std::array<float, DSP::Oversampler::kReportedLatency> m_dryDelayR{};
    uint32_t m_dryDelayPos = 0;

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

    // FFT analyzer state
    float m_hannWindow[kFftSize]{};
    float m_twiddleRe[kFftStages]{};
    float m_twiddleIm[kFftStages]{};
    float m_fftAccumInput[kFftSize]{};
    float m_fftAccumOutput[kFftSize]{};
    float m_fftScratch[kFftSize * 2]{};
    uint32_t m_fftWritePos = 0;
    FftSpectrum m_fftBuffers[2]{};
    std::atomic<uint32_t> m_fftReadIndex{0};
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
