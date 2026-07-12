// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraSat V1 — oversampled waveshaping saturator (tape / tube / hard clip).
//
// Signal path (per channel, per sample):
//   in -> drive gain -> 4x oversample -> waveshaper -> downsample
//      -> DC blocker -> tone low-pass -> output trim -> mix with latency-aligned dry
//
// The nonlinear stage always runs at 4x via DSP::Oversampler (47/29-tap halfband
// cascade), so latency is the oversampler's reported constant. The dry path is
// delayed by the same amount so the Mix control never comb-filters.

#pragma once

#include "DSP/Oversampler.h"
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

class AestraSat : public IPluginInstance {
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
        // Seed parameter defaults only on the first initialization of a fresh
        // instance. EffectChain::prepare() re-calls initialize() on the live
        // instance during sample-rate/device changes and must preserve the
        // user's current parameter values (and any loaded project state).
        if (!m_paramsInitialized.exchange(true)) {
            for (const auto& param : getParameters()) {
                m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
            }
        }
        for (auto& os : m_oversampler) {
            os.prepare(kOsFactor);
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
            static_cast<uint32_t>(m_params[kMode].load(std::memory_order_relaxed) * static_cast<float>(kModeCount - 1) +
                                  0.5f)));

        float blockInputPeak = 0.0f;
        float blockOutputPeak = 0.0f;

        // Parameters are read and smoothed once per control block; the
        // per-sample loop only does the oversampled shaper, DC blocker, tone
        // one-pole and mix — no atomics or transcendentals in the hot path.
        for (uint32_t blockStart = 0; blockStart < numFrames; blockStart += kCtrlBlock) {
            const uint32_t blockEnd = std::min(blockStart + kCtrlBlock, numFrames);

            m_driveSmoothed += (getParameter(kDrive) - m_driveSmoothed) * m_smoothCoeff;
            m_toneSmoothed += (getParameter(kTone) - m_toneSmoothed) * m_smoothCoeff;
            m_outputSmoothed += (getParameter(kOutput) - m_outputSmoothed) * m_smoothCoeff;
            m_mixSmoothed += (getParameter(kMix) - m_mixSmoothed) * m_smoothCoeff;

            const float driveGain = dbToLinear(driveDbFromNorm(m_driveSmoothed));
            const float outGain = dbToLinear(outputDbFromNorm(m_outputSmoothed));
            const float wet = m_mixSmoothed;
            const float dry = 1.0f - wet;
            const float toneCoeff = toneCoeffFromNorm(m_toneSmoothed);

            for (uint32_t i = blockStart; i < blockEnd; ++i) {
                for (uint32_t ch = 0; ch < channels; ++ch) {
                    const float in = sanitizeSample(readInput(inputs, numInputChannels, ch, i));
                    blockInputPeak = std::max(blockInputPeak, std::abs(in));

                    // Latency-aligned dry tap
                    const float delayedDry = m_dryDelay[ch][m_dryPos];
                    m_dryDelay[ch][m_dryPos] = in;

                    // Nonlinear stage at 4x
                    float osBuf[kOsFactor];
                    m_oversampler[ch].upsample(in * driveGain, osBuf);
                    for (uint32_t p = 0; p < kOsFactor; ++p) {
                        osBuf[p] = shape(osBuf[p], mode);
                    }
                    float sat = m_oversampler[ch].downsample(osBuf);

                    // DC blocker (~10 Hz high-pass) — tube asymmetry adds DC
                    const float dcOut = sat - m_dcX1[ch] + m_dcCoeff * m_dcY1[ch];
                    m_dcX1[ch] = sat;
                    m_dcY1[ch] = dcOut;

                    // Tone: one-pole low-pass on the wet path
                    m_toneState[ch] += (dcOut - m_toneState[ch]) * toneCoeff;

                    const float out = delayedDry * dry + m_toneState[ch] * outGain * wet;
                    blockOutputPeak = std::max(blockOutputPeak, std::abs(out));

                    if (outputs[ch])
                        outputs[ch][i] = out;
                }

                m_dryPos = (m_dryPos + 1u) % kDryDelayLen;

                if (!stereo && numOutputChannels > 1 && outputs[1] && outputs[0]) {
                    outputs[1][i] = outputs[0][i];
                }
                for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                    if (outputs[ch])
                        outputs[ch][i] = 0.0f;
                }
            }
        }

        m_inputLevel.store(blockInputPeak, std::memory_order_relaxed);
        m_outputLevel.store(blockOutputPeak, std::memory_order_relaxed);
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
            return; // NaN survives clamp (comparisons are false) and would poison smoothing
        m_params[id].store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    std::vector<PluginParameter> getParameters() const override {
        return {
            {kDrive, "Drive", "DRV", "dB", 0.25f, 0.0f, 1.0f, true},
            {kMode, "Mode", "MOD", "", 0.0f, 0.0f, 1.0f, true, false, false, kModeCount - 1},
            {kTone, "Tone", "TON", "Hz", 1.0f, 0.0f, 1.0f, true},
            {kOutput, "Output", "OUT", "dB", 0.5f, 0.0f, 1.0f, true},
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
        for (uint32_t i = 0; i < kParamCount; ++i) {
            setParameter(i, blob.params[i]);
        }
        return true;
    }

    bool hasEditor() const override { return true; }
    bool openEditor(void*) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {480, 300}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return DSP::Oversampler::kReportedLatency; }
    uint32_t getTailSamples() const override { return 0; }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

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
    std::atomic<bool> m_paramsInitialized{false};
    std::array<std::atomic<float>, kParamCount> m_params{};

    std::array<DSP::Oversampler, 2> m_oversampler;
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
