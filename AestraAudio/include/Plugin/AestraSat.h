// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraSat V1 — oversampled waveshaping saturator (tape / tube / hard clip).
//
// Signal path (per channel):
//   in -> drive gain -> 4x oversample -> waveshaper -> downsample
//      -> DC blocker -> tone low-pass -> output trim -> mix with latency-aligned dry
//
// The nonlinear stage always runs at 4x via DSP::BlockOversampler4x (the same
// 47/29-tap halfband cascade as DSP::Oversampler, run over a block at a time),
// so latency is the oversampler's reported constant. The dry path is delayed by
// the same amount so the Mix control never comb-filters.

#pragma once

#include "DSP/OversamplerBlock.h"
#include "RealtimeThreadGuard.h"
#include "Plugin/InternalPluginBase.h"
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

class AestraSat : public InternalPluginBase {
public:
    static constexpr uint32_t kStateMagic = 0x53415431; // 'SAT1'
    static constexpr uint32_t kOsFactor = 4;

    enum Param : uint32_t {
        kDrive = 0, // 0 to +36 dB pre-shaper gain
        kMode,      // 0=Tape, 1=Tube, 2=Hard (stepped)
        kTone,      // post low-pass, 1 kHz to 20 kHz (log)
        kOutput,    // -12 to +12 dB trim
        kMix,       // 0% to 100%
        kBypass,
        kParamCount,
    };

    enum Mode : uint32_t { kModeTape = 0, kModeTube, kModeHard, kModeCount };

    AestraSat() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        (void)maxBlockSize;
        m_sampleRate = std::max(1.0, sampleRate);
        seedDefaultsOnce();
        for (auto& os : m_oversampler) {
            os.prepare();
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
                 uint32_t numFrames, const MidiBuffer* midiInput = nullptr, MidiBuffer* midiOutput = nullptr) AESTRA_RT_NONBLOCKING override {
        (void)midiInput;
        (void)midiOutput;

        if (!m_active.load(std::memory_order_relaxed) || isBypassed()) {
            // The plugin always reports the oversampler latency, so internal
            // bypass must delay by the same amount (through the same ring,
            // which keeps advancing) or this slot lands ~30 samples early
            // relative to PDC-compensated tracks.
            copyDelayed(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            m_wasBypassed = true;
            return;
        }
        if (m_wasBypassed) {
            // Flush stale wet-path state from before the bypass so reactivation
            // does not emit it; the dry ring stays hot for latency alignment.
            for (auto& os : m_oversampler) {
                os.reset();
            }
            m_dcX1[0] = m_dcX1[1] = 0.0f;
            m_dcY1[0] = m_dcY1[1] = 0.0f;
            m_toneState[0] = m_toneState[1] = 0.0f;
            snapSmoothedParams();
            m_wasBypassed = false;
        }

        const uint32_t channels = std::min<uint32_t>(2, numOutputChannels);
        const bool stereo = channels >= 2;
        const auto mode = static_cast<Mode>(std::min<uint32_t>(
            kModeCount - 1,
            static_cast<uint32_t>(paramValue(kMode) * static_cast<float>(kModeCount - 1) +
                                  0.5f)));

        float blockInputPeak = 0.0f;
        float blockOutputPeak = 0.0f;

        // Work in chunks of kChunk frames (a whole number of control blocks).
        // Parameters are read and smoothed once per control block; per chunk the
        // oversampler runs over the whole block, so the shaper and the filters
        // around it work on contiguous arrays instead of one sample at a time.
        for (uint32_t chunkStart = 0; chunkStart < numFrames; chunkStart += kChunk) {
            const uint32_t n = std::min(kChunk, numFrames - chunkStart);

            ControlBlock ctl[kChunk / kCtrlBlock];
            const uint32_t numCtl = (n + kCtrlBlock - 1u) / kCtrlBlock;
            for (uint32_t c = 0; c < numCtl; ++c) {
                m_driveSmoothed += (getParameter(kDrive) - m_driveSmoothed) * m_smoothCoeff;
                m_toneSmoothed += (getParameter(kTone) - m_toneSmoothed) * m_smoothCoeff;
                m_outputSmoothed += (getParameter(kOutput) - m_outputSmoothed) * m_smoothCoeff;
                m_mixSmoothed += (getParameter(kMix) - m_mixSmoothed) * m_smoothCoeff;

                ctl[c].driveGain = dbToLinear(driveDbFromNorm(m_driveSmoothed));
                ctl[c].outGain = dbToLinear(outputDbFromNorm(m_outputSmoothed));
                ctl[c].wet = m_mixSmoothed;
                ctl[c].dry = 1.0f - m_mixSmoothed;
                ctl[c].toneCoeff = toneCoeffFromNorm(m_toneSmoothed);
            }

            for (uint32_t ch = 0; ch < channels; ++ch) {
                float dryIn[kChunk];
                float driven[kChunk];
                for (uint32_t i = 0; i < n; ++i) {
                    const float in = sanitizeSample(readInput(inputs, numInputChannels, ch, chunkStart + i));
                    blockInputPeak = std::max(blockInputPeak, std::abs(in));
                    dryIn[i] = in;
                    driven[i] = in * ctl[i / kCtrlBlock].driveGain;
                }

                // Nonlinear stage at 4x
                float osBuf[kOsFactor * kChunk];
                m_oversampler[ch].upsample(driven, osBuf, n);
                shapeBlock(osBuf, kOsFactor * n, mode);
                float sat[kChunk];
                m_oversampler[ch].downsample(osBuf, sat, n);

                uint32_t dryPos = m_dryPos;
                for (uint32_t i = 0; i < n; ++i) {
                    const ControlBlock& cb = ctl[i / kCtrlBlock];

                    // Latency-aligned dry tap
                    const float delayedDry = m_dryDelay[ch][dryPos];
                    m_dryDelay[ch][dryPos] = dryIn[i];
                    dryPos = (dryPos + 1u) % kDryDelayLen;

                    // DC blocker (~10 Hz high-pass) — tube asymmetry adds DC
                    const float dcOut = sat[i] - m_dcX1[ch] + m_dcCoeff * m_dcY1[ch];
                    m_dcX1[ch] = sat[i];
                    m_dcY1[ch] = dcOut;

                    // Tone: one-pole low-pass on the wet path
                    m_toneState[ch] += (dcOut - m_toneState[ch]) * cb.toneCoeff;

                    const float out = delayedDry * cb.dry + m_toneState[ch] * cb.outGain * cb.wet;
                    blockOutputPeak = std::max(blockOutputPeak, std::abs(out));

                    if (outputs[ch])
                        outputs[ch][chunkStart + i] = out;
                }
            }
            m_dryPos = (m_dryPos + n) % kDryDelayLen;

            if (!stereo && numOutputChannels > 1 && outputs[1] && outputs[0]) {
                std::memcpy(outputs[1] + chunkStart, outputs[0] + chunkStart, n * sizeof(float));
            }
            for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                if (outputs[ch])
                    std::memset(outputs[ch] + chunkStart, 0, n * sizeof(float));
            }
        }

        m_inputLevel.store(blockInputPeak, std::memory_order_relaxed);
        m_outputLevel.store(blockOutputPeak, std::memory_order_relaxed);
    }

    // Declarative parameter table: this is the whole plugin-side surface now.
    // Storage, the non-finite and range guards, getParameters() and the state
    // blob come from InternalPluginBase.
    inline static constexpr ParamSpec kSpecs[] = {
        {kDrive, "Drive", "DRV", "dB", 0.25f, 0.0f, 1.0f, true},
        {kMode, "Mode", "MOD", "", 0.0f, 0.0f, 1.0f, true, false, false, kModeCount - 1},
        {kTone, "Tone", "TON", "Hz", 1.0f, 0.0f, 1.0f, true},
        {kOutput, "Output", "OUT", "dB", 0.5f, 0.0f, 1.0f, true},
        {kMix, "Mix", "MIX", "%", 1.0f, 0.0f, 1.0f, true},
        {kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1},
    };

    const ParamSpec* paramSpecs() const override { return kSpecs; }
    uint32_t paramSpecCount() const override { return kParamCount; }
    AESTRA_VALIDATE_PARAM_SPECS(kSpecs, kParamCount);
    uint32_t stateMagic() const override { return kStateMagic; }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount)
            return "";
        const float v = getParameter(id);
        char buf[32];
        switch (id) {
        case kDrive:
            std::snprintf(buf, sizeof(buf), "%.1f dB", driveDbFromNorm(v));
            return buf;
        case kMode:
            switch (modeFromNorm(v)) {
            case kModeTape:
                return "Tape";
            case kModeTube:
                return "Tube";
            default:
                return "Hard";
            }
        case kTone: {
            const float hz = toneHzFromNorm(v);
            if (hz >= 10000.0f) {
                std::snprintf(buf, sizeof(buf), "%.1f kHz", hz / 1000.0f);
            } else if (hz >= 1000.0f) {
                std::snprintf(buf, sizeof(buf), "%.2f kHz", hz / 1000.0f);
            } else {
                std::snprintf(buf, sizeof(buf), "%.0f Hz", hz);
            }
            return buf;
        }
        case kOutput:
            std::snprintf(buf, sizeof(buf), "%+.1f dB", outputDbFromNorm(v));
            return buf;
        case kMix:
            return std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        case kBypass:
            return v > 0.5f ? "ON" : "OFF";
        default:
            return "";
        }
    }

    bool hasEditor() const override { return true; }
    std::pair<int, int> getEditorSize() const override { return {480, 300}; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return DSP::Oversampler::kReportedLatency; }
    uint32_t getTailSamples() const override { return 0; }

    void setInfo(const PluginInfo& info) { m_info = info; }

    // Metering
    float getInputLevel() const { return m_inputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return m_outputLevel.load(std::memory_order_relaxed); }

    // ── Parameter mapping (public: shared with editor/tests) ──
    static float driveDbFromNorm(float value) { return std::clamp(value, 0.0f, 1.0f) * 36.0f; }

    static float outputDbFromNorm(float value) { return -12.0f + std::clamp(value, 0.0f, 1.0f) * 24.0f; }

    static float toneHzFromNorm(float value) {
        // 1 kHz .. 20 kHz, log
        return 1000.0f * std::pow(20.0f, std::clamp(value, 0.0f, 1.0f));
    }

    static Mode modeFromNorm(float value) {
        return static_cast<Mode>(std::min<uint32_t>(
            kModeCount - 1,
            static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * static_cast<float>(kModeCount - 1) + 0.5f)));
    }

    static float normFromMode(Mode mode) { return static_cast<float>(mode) / static_cast<float>(kModeCount - 1); }

private:
    // Read-before-write circular buffer of length N delays by exactly N —
    // sized to match the oversampler round-trip latency.
    static constexpr uint32_t kDryDelayLen = DSP::Oversampler::kReportedLatency;
    static constexpr uint32_t kCtrlBlock = 16; // control-rate interval for param/coefficient updates
    // Frames handled per oversampler call: a whole number of control blocks, and
    // the most BlockOversampler4x takes at once.
    static constexpr uint32_t kChunk = DSP::BlockOversampler4x::kMaxFrames;
    static_assert(kChunk % kCtrlBlock == 0, "a chunk must hold whole control blocks");

    /// Smoothed parameters for one control block, resolved once so the
    /// per-sample loop does no transcendentals and no atomics.
    struct ControlBlock {
        float driveGain = 1.0f;
        float outGain = 1.0f;
        float wet = 1.0f;
        float dry = 0.0f;
        float toneCoeff = 0.0f;
    };

    /// Bypass copy delayed by the reported latency, through the same dry ring
    /// the active path uses (so the ring stays hot across bypass toggles).
    /// Channels >= 2 pass through like copyOrClear.
    void copyDelayed(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                     uint32_t numFrames) {
        for (uint32_t i = 0; i < numFrames; ++i) {
            for (uint32_t ch = 0; ch < 2; ++ch) {
                const float in = sanitizeSample(readInput(inputs, numInputChannels, ch, i));
                const float delayed = m_dryDelay[ch][m_dryPos];
                m_dryDelay[ch][m_dryPos] = in;
                if (ch < numOutputChannels && outputs[ch])
                    outputs[ch][i] = delayed;
            }
            m_dryPos = (m_dryPos + 1u) % kDryDelayLen;
        }
        for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
            if (outputs[ch] && ch < numInputChannels && inputs[ch]) {
                std::memcpy(outputs[ch], inputs[ch], numFrames * sizeof(float));
            } else if (outputs[ch]) {
                std::memset(outputs[ch], 0, numFrames * sizeof(float));
            }
        }
    }

    static float shape(float x, Mode mode) {
        switch (mode) {
        case kModeTape:
            return std::tanh(x);
        case kModeTube:
            // Asymmetric: the negative lobe saturates earlier (limit -1/1.5)
            // which generates even harmonics. Both branches have unity slope
            // at 0, so the curve is C1-continuous.
            return x >= 0.0f ? std::tanh(x) : std::tanh(1.5f * x) * (1.0f / 1.5f);
        default:
            return std::clamp(x, -1.0f, 1.0f);
        }
    }

    /// The mode switch sits outside the loop so each loop is a single shaper.
    static void shapeBlock(float* buf, uint32_t count, Mode mode) {
        switch (mode) {
        case kModeTape:
            for (uint32_t i = 0; i < count; ++i)
                buf[i] = shape(buf[i], kModeTape);
            break;
        case kModeTube:
            for (uint32_t i = 0; i < count; ++i)
                buf[i] = shape(buf[i], kModeTube);
            break;
        default:
            for (uint32_t i = 0; i < count; ++i)
                buf[i] = shape(buf[i], kModeHard);
            break;
        }
    }

    float toneCoeffFromNorm(float norm) const {
        const float fc = toneHzFromNorm(norm);
        const float x = 2.0f * 3.14159265358979323846f * fc / static_cast<float>(m_sampleRate);
        return 1.0f - std::exp(-x);
    }

    void resetRuntimeState() {
        for (auto& os : m_oversampler) {
            os.reset();
        }
        for (auto& d : m_dryDelay) {
            d.fill(0.0f);
        }
        m_dryPos = 0;
        m_dcX1[0] = m_dcX1[1] = 0.0f;
        m_dcY1[0] = m_dcY1[1] = 0.0f;
        m_toneState[0] = m_toneState[1] = 0.0f;
        m_wasBypassed = false;
        const float sr = static_cast<float>(m_sampleRate);
        // Smoothing runs once per control block, so the coefficient is scaled
        // to the block interval to keep the 2 ms time constant.
        m_smoothCoeff = 1.0f - std::exp(-static_cast<float>(kCtrlBlock) / (0.002f * sr));
        m_dcCoeff = 1.0f - 2.0f * 3.14159265358979323846f * 10.0f / sr;
        m_inputLevel.store(0.0f, std::memory_order_relaxed);
        m_outputLevel.store(0.0f, std::memory_order_relaxed);
    }

    void snapSmoothedParams() {
        m_driveSmoothed = getParameter(kDrive);
        m_toneSmoothed = getParameter(kTone);
        m_outputSmoothed = getParameter(kOutput);
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

    std::array<DSP::BlockOversampler4x, 2> m_oversampler;
    std::array<std::array<float, kDryDelayLen>, 2> m_dryDelay{};
    uint32_t m_dryPos = 0;
    bool m_wasBypassed = false;

    float m_dcX1[2] = {0.0f, 0.0f};
    float m_dcY1[2] = {0.0f, 0.0f};
    float m_dcCoeff = 0.9987f;
    float m_toneState[2] = {0.0f, 0.0f};

    float m_smoothCoeff = 0.0f;
    float m_driveSmoothed = 0.25f;
    float m_toneSmoothed = 1.0f;
    float m_outputSmoothed = 0.5f;
    float m_mixSmoothed = 1.0f;

    std::atomic<float> m_inputLevel{0.0f};
    std::atomic<float> m_outputLevel{0.0f};
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
