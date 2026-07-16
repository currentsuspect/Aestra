// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraDrift V3 — real-time pitch shifter with complementary granular taps.

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
        kPitch = 0, // -12 to +12 semitones
        kGrain,     // 0% to 100% — grain/overlap size
        kMix,       // 0% to 100%
        kBypass,
        kFine,       // -100 to +100 cents
        kSpread,     // stereo grain-phase spread
        kMotion,     // optional pitch-motion depth
        kMotionRate, // 0.05Hz to 2Hz
        kOutput,     // -12dB to +12dB
        kTexture,    // broad overlap character; 0=pure, 1=textured
        kParamCount
    };

    AestraDrift() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        (void)maxBlockSize;
        m_sampleRate = std::max(1.0, sampleRate);
        // Seed parameter defaults only on the first initialization of a fresh
        // instance. EffectChain::prepare() re-calls initialize() on the live
        // instance during sample-rate/device changes and must preserve the
        // user's current parameter values (and any loaded project state).
        if (!m_paramsInitialized.exchange(true)) {
            for (const auto& param : getParameters()) {
                m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
            }
        }
        const uint32_t maxGrainSamples =
            std::max(8u, static_cast<uint32_t>(std::ceil(kMaxGrainMilliseconds * 0.001 * m_sampleRate)));
        m_latencySamples = maxGrainSamples / 2u + 4u;
        const uint32_t requiredSamples = m_latencySamples + maxGrainSamples / 2u + 8u;
        uint32_t bufferSize = 1u;
        while (bufferSize < requiredSamples)
            bufferSize <<= 1u;
        m_bufferMask = static_cast<int>(bufferSize - 1u);
        m_buffer.assign(bufferSize, 0.0f);
        m_bufferR.assign(bufferSize, 0.0f);
        resetRuntimeState();
        return true;
    }

    void shutdown() override {}

    void activate() override {
        m_active.store(true, std::memory_order_relaxed);
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
        std::fill(m_bufferR.begin(), m_bufferR.end(), 0.0f);
        resetRuntimeState();
    }

    void deactivate() override { m_active.store(false, std::memory_order_relaxed); }
    bool isActive() const override { return m_active.load(std::memory_order_relaxed); }

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* midiInput = nullptr, MidiBuffer* midiOutput = nullptr) override {
        (void)midiInput;
        (void)midiOutput;

        if (!m_active.load(std::memory_order_relaxed) || m_params[kBypass].load(std::memory_order_relaxed) > 0.5f) {
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
        if (m_buffer.empty() || m_bufferR.empty()) {
            for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
                if (outputs[ch])
                    std::memset(outputs[ch], 0, numFrames * sizeof(float));
            }
            return;
        }
        int writePos = m_writePos;
        std::array<float, kTapCount> tapPhase = m_tapPhase;
        float motionPhase = m_motionPhase;
        const int bufferMask = m_bufferMask;
        const float latencySamples = static_cast<float>(m_latencySamples);

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
        const float grainTarget = m_params[kGrain].load(std::memory_order_relaxed);
        const float mixTarget = m_params[kMix].load(std::memory_order_relaxed);
        const float fineTarget = m_params[kFine].load(std::memory_order_relaxed);
        const float spreadTarget = m_params[kSpread].load(std::memory_order_relaxed);
        const float motionTarget = m_params[kMotion].load(std::memory_order_relaxed);
        const float motionRateTarget = m_params[kMotionRate].load(std::memory_order_relaxed);
        const float outputTarget = m_params[kOutput].load(std::memory_order_relaxed);
        const float textureTarget = m_params[kTexture].load(std::memory_order_relaxed);
        float pitchSmoothed = m_pitchSmoothed;
        float grainSmoothed = m_grainSmoothed;
        float mixSmoothed = m_mixSmoothed;
        float fineSmoothed = m_fineSmoothed;
        float spreadSmoothed = m_spreadSmoothed;
        float motionSmoothed = m_motionSmoothed;
        float motionRateSmoothed = m_motionRateSmoothed;
        float outputSmoothed = m_outputSmoothed;
        float textureSmoothed = m_textureSmoothed;

        for (uint32_t i = 0; i < numFrames; ++i) {
            const float inL = (numInputChannels > 0 && inputs[0]) ? inputs[0][i] : 0.0f;
            const float inR = stereo ? ((numInputChannels > 1 && inputs[1]) ? inputs[1][i] : inL) : inL;

            m_buffer[writePos] = inL;
            m_bufferR[writePos] = inR;

            pitchSmoothed += smoothCoeff * (pitchTarget - pitchSmoothed);
            if (std::fabs(pitchTarget - pitchSmoothed) < kSmoothSnapEps)
                pitchSmoothed = pitchTarget;
            grainSmoothed += smoothCoeff * (grainTarget - grainSmoothed);
            if (std::fabs(grainTarget - grainSmoothed) < kSmoothSnapEps)
                grainSmoothed = grainTarget;
            mixSmoothed += smoothCoeff * (mixTarget - mixSmoothed);
            if (std::fabs(mixTarget - mixSmoothed) < kSmoothSnapEps)
                mixSmoothed = mixTarget;
            fineSmoothed += smoothCoeff * (fineTarget - fineSmoothed);
            if (std::fabs(fineTarget - fineSmoothed) < kSmoothSnapEps)
                fineSmoothed = fineTarget;
            spreadSmoothed += smoothCoeff * (spreadTarget - spreadSmoothed);
            if (std::fabs(spreadTarget - spreadSmoothed) < kSmoothSnapEps)
                spreadSmoothed = spreadTarget;
            motionSmoothed += smoothCoeff * (motionTarget - motionSmoothed);
            if (std::fabs(motionTarget - motionSmoothed) < kSmoothSnapEps)
                motionSmoothed = motionTarget;
            motionRateSmoothed += smoothCoeff * (motionRateTarget - motionRateSmoothed);
            if (std::fabs(motionRateTarget - motionRateSmoothed) < kSmoothSnapEps)
                motionRateSmoothed = motionRateTarget;
            outputSmoothed += smoothCoeff * (outputTarget - outputSmoothed);
            if (std::fabs(outputTarget - outputSmoothed) < kSmoothSnapEps)
                outputSmoothed = outputTarget;
            textureSmoothed += smoothCoeff * (textureTarget - textureSmoothed);
            if (std::fabs(textureTarget - textureSmoothed) < kSmoothSnapEps)
                textureSmoothed = textureTarget;

            const float grainSamples =
                std::max(8.0f, grainMsFromNorm(grainSmoothed) * 0.001f * static_cast<float>(m_sampleRate));
            const float baseDelay = latencySamples + grainSamples * 0.5f;

            // Complementary Hann taps crossfade at their zero-energy wrap points.
            // The normalized grain phase makes this behavior sample-rate independent.
            // When one normalized tap wraps, its window is ~0
            // and the other tap dominates. No audible discontinuity.
            float outL = 0.0f;
            float outR = 0.0f;
            float winSumL = 0.0f;
            float winSumR = 0.0f;
            const float stereoPhaseOffset = spreadSmoothed * 0.0625f;
            for (size_t t = 0; t < kTapCount; ++t) {
                float phaseNormL = tapPhase[t] - stereoPhaseOffset;
                float phaseNormR = tapPhase[t] + stereoPhaseOffset;
                if (phaseNormL < 0.0f)
                    phaseNormL += 1.0f;
                if (phaseNormR >= 1.0f)
                    phaseNormR -= 1.0f;
                // Texture OFF raises the Hann window to the sixth power, narrowing
                // the overlap region where phase interference causes cyclic color.
                // Texture blends toward the broader Hann overlap for users who want
                // that motion as an intentional character control.
                const float baseWinL = 0.5f * (1.0f - std::cos(twoPi * phaseNormL));
                const float baseWinR = 0.5f * (1.0f - std::cos(twoPi * phaseNormR));
                const float baseSquaredL = baseWinL * baseWinL;
                const float baseSquaredR = baseWinR * baseWinR;
                const float pureWinL = baseSquaredL * baseSquaredL * baseSquaredL;
                const float pureWinR = baseSquaredR * baseSquaredR * baseSquaredR;
                const float winL = pureWinL + textureSmoothed * (baseWinL - pureWinL);
                const float winR = pureWinR + textureSmoothed * (baseWinR - pureWinR);
                winSumL += winL;
                winSumR += winR;

                const float readPosL = static_cast<float>(writePos) - baseDelay + phaseNormL * grainSamples;
                const float readPosR = static_cast<float>(writePos) - baseDelay + phaseNormR * grainSamples;
                outL += readBuffer(m_buffer, readPosL, bufferMask) * winL;
                outR += readBuffer(m_bufferR, readPosR, bufferMask) * winR;
            }
            outL /= std::max(winSumL, 0.001f);
            outR /= std::max(winSumR, 0.001f);

            const float pitchNorm = pitchSmoothed;
            const float coarseSemitones = -12.0f + pitchNorm * 24.0f;
            const float fineSemitones = fineSmoothed * 2.0f - 1.0f;
            const float motionDepthSemitones = motionSmoothed * motionSmoothed * 0.25f;
            const float motionRateHz = 0.05f * std::pow(40.0f, motionRateSmoothed);
            motionPhase += motionRateHz / static_cast<float>(m_sampleRate);
            if (motionPhase >= 1.0f)
                motionPhase -= 1.0f;
            const float pitchSemitones =
                coarseSemitones + fineSemitones + std::sin(twoPi * motionPhase) * motionDepthSemitones;
            const float pitchRatio = std::pow(2.0f, pitchSemitones / 12.0f);
            const float delta = pitchRatio - 1.0f;

            for (size_t t = 0; t < kTapCount; ++t) {
                tapPhase[t] += delta / grainSamples;
                // Wrap: when one tap wraps, its Hann window is near 0
                if (tapPhase[t] >= 1.0f)
                    tapPhase[t] -= 1.0f;
                if (tapPhase[t] < 0.0f)
                    tapPhase[t] += 1.0f;
            }

            const float dryL = readBuffer(m_buffer, static_cast<float>(writePos) - latencySamples, bufferMask);
            const float dryR = readBuffer(m_bufferR, static_cast<float>(writePos) - latencySamples, bufferMask);
            if (std::fabs(pitchSemitones) < 1.0e-6f) {
                outL = dryL;
                outR = dryR;
            }

            writePos = (writePos + 1) & bufferMask;

            const float mix = mixSmoothed;
            const float wet = mix;
            const float dry = 1.0f - wet;
            const float outputGain = std::pow(10.0f, (outputSmoothed * 24.0f - 12.0f) / 20.0f);

            if (numOutputChannels > 0 && outputs[0])
                outputs[0][i] = sanitizeOutput((dryL * dry + outL * wet) * outputGain);
            if (numOutputChannels > 1 && outputs[1])
                outputs[1][i] = sanitizeOutput((dryR * dry + outR * wet) * outputGain);
            for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                if (outputs[ch])
                    outputs[ch][i] = 0.0f;
            }
        }

        m_writePos = writePos;
        m_tapPhase = tapPhase;
        m_motionPhase = motionPhase;
        m_pitchSmoothed = pitchSmoothed;
        m_grainSmoothed = grainSmoothed;
        m_mixSmoothed = mixSmoothed;
        m_fineSmoothed = fineSmoothed;
        m_spreadSmoothed = spreadSmoothed;
        m_motionSmoothed = motionSmoothed;
        m_motionRateSmoothed = motionRateSmoothed;
        m_outputSmoothed = outputSmoothed;
        m_textureSmoothed = textureSmoothed;
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
            return; // NaN survives clamp and would poison the parameter smoothers
        m_params[id].store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    std::vector<PluginParameter> getParameters() const override {
        return {
            {kPitch, "Pitch", "PIT", "st", 0.5f, 0.0f, 1.0f, true},
            {kGrain, "Grain", "GRN", "ms", 0.0f, 0.0f, 1.0f, true},
            {kMix, "Mix", "MIX", "%", 1.0f, 0.0f, 1.0f, true},
            {kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1},
            {kFine, "Fine Tune", "FINE", "ct", 0.5f, 0.0f, 1.0f, true},
            {kSpread, "Stereo Spread", "SPRD", "%", 0.0f, 0.0f, 1.0f, true},
            {kMotion, "Motion", "MOT", "ct", 0.0f, 0.0f, 1.0f, true},
            {kMotionRate, "Motion Rate", "RATE", "Hz", 0.35f, 0.0f, 1.0f, true},
            {kOutput, "Output", "OUT", "dB", 0.5f, 0.0f, 1.0f, true},
            {kTexture, "Texture", "TEX", "%", 0.0f, 0.0f, 1.0f, true},
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount)
            return "";
        const float v = getParameter(id);
        switch (id) {
        case kPitch: {
            const float st = -12.0f + v * 24.0f;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%+.1f st", st);
            return buf;
        }
        case kGrain: {
            if (v <= 0.001f)
                return "PURE";
            const float ms = grainMsFromNorm(v);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f ms", ms);
            return buf;
        }
        case kMix:
            return std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        case kBypass:
            return v > 0.5f ? "ON" : "OFF";
        case kFine: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%+.0f ct", v * 200.0f - 100.0f);
            return buf;
        }
        case kSpread:
            return std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        case kMotion: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f ct", v * v * 25.0f);
            return buf;
        }
        case kMotionRate: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f Hz", 0.05f * std::pow(40.0f, v));
            return buf;
        }
        case kOutput: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%+.1f dB", v * 24.0f - 12.0f);
            return buf;
        }
        case kTexture:
            return v <= 0.001f ? "OFF" : std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        default:
            return "";
        }
    }

    std::vector<uint8_t> saveState() const override {
        std::vector<uint8_t> state(sizeof(uint32_t) * 2u + sizeof(float) * kParamCount);
        const uint32_t magic = kStateMagic;
        const uint32_t version = kStateVersion;
        std::memcpy(state.data(), &magic, sizeof(magic));
        std::memcpy(state.data() + sizeof(magic), &version, sizeof(version));
        for (uint32_t i = 0; i < kParamCount; ++i) {
            const float value = getParameter(i);
            std::memcpy(state.data() + sizeof(uint32_t) * 2u + sizeof(float) * i, &value, sizeof(value));
        }
        return state;
    }

    bool loadState(const std::vector<uint8_t>& state) override {
        if (state.size() < sizeof(uint32_t) * 2)
            return false;
        uint32_t magic = 0;
        uint32_t version = 0;
        std::memcpy(&magic, state.data(), sizeof(magic));
        std::memcpy(&version, state.data() + sizeof(magic), sizeof(version));
        if (magic != kStateMagic)
            return false;
        const uint32_t storedParamCount = version == 1u              ? kLegacyParamCount
                                          : version == 2u            ? kV2ParamCount
                                          : version == kStateVersion ? kParamCount
                                                                     : 0u;
        if (storedParamCount == 0u || state.size() < sizeof(uint32_t) * 2u + sizeof(float) * storedParamCount)
            return false;
        if (version != kStateVersion) {
            const auto defaults = getParameters();
            for (uint32_t i = storedParamCount; i < kParamCount; ++i)
                setParameter(i, defaults[i].defaultValue);
        }
        for (uint32_t i = 0; i < storedParamCount; ++i) {
            float value = 0.0f;
            std::memcpy(&value, state.data() + sizeof(uint32_t) * 2u + sizeof(float) * i, sizeof(value));
            setParameter(i, value);
        }
        if (version == 1u)
            setParameter(kGrain, 0.0f); // V1 stored this value but never applied it to DSP.
        return true;
    }

    bool hasEditor() const override { return true; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {720, 440}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return m_latencySamples; }
    uint32_t getTailSamples() const override { return m_latencySamples * 2u; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

private:
    static constexpr uint32_t kStateMagic = 0x44524654;
    static constexpr uint32_t kStateVersion = 3;
    static constexpr uint32_t kLegacyParamCount = 4;
    static constexpr uint32_t kV2ParamCount = 9;
    static constexpr size_t kTapCount = 2;
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kMaxGrainMilliseconds = 40.0f;

    static float grainMsFromNorm(float norm) { return kMaxGrainMilliseconds - norm * 35.0f; }

    static float sanitizeOutput(float value) { return std::isfinite(value) ? value : 0.0f; }

    // Cubic Hermite interpolation
    static float readBuffer(const std::vector<float>& buf, float readPos, int bufferMask) {
        const int i1 = static_cast<int>(std::floor(readPos)) & bufferMask;
        const int i0 = (i1 - 1) & bufferMask;
        const int i2 = (i1 + 1) & bufferMask;
        const int i3 = (i2 + 1) & bufferMask;
        const float frac = readPos - std::floor(readPos);
        const float y0 = buf[static_cast<size_t>(i0)];
        const float y1 = buf[static_cast<size_t>(i1)];
        const float y2 = buf[static_cast<size_t>(i2)];
        const float y3 = buf[static_cast<size_t>(i3)];
        const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c = -0.5f * y0 + 0.5f * y2;
        return ((a * frac + b) * frac + c) * frac + y1;
    }

    void resetRuntimeState() {
        m_writePos = 0;
        for (size_t i = 0; i < kTapCount; ++i)
            m_tapPhase[i] = static_cast<float>(i) / static_cast<float>(kTapCount);
        m_motionPhase = 0.0f;
        // Snap smoothers to the current targets so load/activate doesn't glide.
        m_pitchSmoothed = m_params[kPitch].load(std::memory_order_relaxed);
        m_grainSmoothed = m_params[kGrain].load(std::memory_order_relaxed);
        m_mixSmoothed = m_params[kMix].load(std::memory_order_relaxed);
        m_fineSmoothed = m_params[kFine].load(std::memory_order_relaxed);
        m_spreadSmoothed = m_params[kSpread].load(std::memory_order_relaxed);
        m_motionSmoothed = m_params[kMotion].load(std::memory_order_relaxed);
        m_motionRateSmoothed = m_params[kMotionRate].load(std::memory_order_relaxed);
        m_outputSmoothed = m_params[kOutput].load(std::memory_order_relaxed);
        m_textureSmoothed = m_params[kTexture].load(std::memory_order_relaxed);
    }

    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_paramsInitialized{false};
    std::array<std::atomic<float>, kParamCount> m_params{};

    std::vector<float> m_buffer;
    std::vector<float> m_bufferR;
    int m_bufferMask = 0;
    uint32_t m_latencySamples = 0;
    int m_writePos = 0;
    std::array<float, kTapCount> m_tapPhase{0.0f, 0.5f};
    float m_motionPhase = 0.0f;

    // Smoothed automatable params. Drift reads Pitch/Grain/Mix per-sample; without
    // smoothing, automating Mix (a dry/wet gain crossfade) or Pitch/Grain zippers.
    // setParameter only stores the atomic target; process() ramps toward it,
    // matching the smoothing contract the other internal plugins follow.
    float m_pitchSmoothed = 0.5f;
    float m_grainSmoothed = 0.0f;
    float m_mixSmoothed = 1.0f;
    float m_fineSmoothed = 0.5f;
    float m_spreadSmoothed = 0.0f;
    float m_motionSmoothed = 0.0f;
    float m_motionRateSmoothed = 0.35f;
    float m_outputSmoothed = 0.5f;
    float m_textureSmoothed = 0.0f;
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
