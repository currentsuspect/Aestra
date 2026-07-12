// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraLFO V1 — tempo-syncable LFO that shapes the audio flowing through it
// (LFOTool-style insert effect): rhythmic volume gating/ducking, auto-pan, or
// filter-cutoff wobble.
//
// V1 deliberately modulates its own audio path instead of other plugins'
// parameters: engine-side parameter modulation routing does not exist yet
// (issue #467); when it lands, this LFO core can be reused as a source.
//
// The LFO free-runs from activate() — it locks to tempo *rate* via setBPM()
// (same push pattern as AestraDelay) but not to transport *phase*; bar-aligned
// restart needs a host transport hook that IPluginInstance does not have yet.
//
// Signal path (per sample):
//   lfo phase -> waveform -> smoothing (slew) -> target:
//     Volume:  gain = 1 - depth * (1 - u)          (u = unipolar LFO, 1 = open)
//     Pan:     equal-power L/R, unity at center
//     Cutoff:  ZDF SVF low-pass swept down up to 6 octaves from 20 kHz

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

class AestraLFO : public IPluginInstance {
public:
    static constexpr uint32_t kStateMagic = 0x4C464F31; // 'LFO1'
    static constexpr float kCutoffTopHz = 20000.0f;
    static constexpr float kCutoffSweepOctaves = 6.0f;

    enum Param : uint32_t {
        kTarget = 0,   // 0=Volume, 1=Pan, 2=Cutoff (stepped)
        kWave,         // 0=Sine, 1=Triangle, 2=Saw Down, 3=Saw Up, 4=Square, 5=S&H
        kSyncMode,     // 0=Free, 1=Sync
        kRateHz,       // 0.02 to 20 Hz (log), free mode
        kNoteDivision, // 0..12 division table (same table as AestraDelay), sync mode
        kDepth,        // 0% to 100%
        kPhase,        // 0 to 360 degrees
        kSmooth,       // 0 to 200 ms output slew (click-free squares)
        kBypass,
        kParamCount,
    };

    enum Target : uint32_t { kTargetVolume = 0, kTargetPan, kTargetCutoff, kTargetCount };
    enum Wave : uint32_t { kWaveSine = 0, kWaveTriangle, kWaveSawDown, kWaveSawUp, kWaveSquare, kWaveSH, kWaveCount };

    // Same division indices/labels as AestraDelay so the two read identically.
    enum NoteDivision : int {
        kDiv1_1 = 0,
        kDiv1_2,
        kDiv1_4,
        kDiv1_8,
        kDiv1_16,
        kDiv1_32,
        kDiv1_2D,
        kDiv1_4D,
        kDiv1_8D,
        kDiv1_16D,
        kDiv1_4T,
        kDiv1_8T,
        kDiv1_16T,
    };

    AestraLFO() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override {
        (void)maxBlockSize;
        m_sampleRate = std::max(1.0, sampleRate);
        for (const auto& param : getParameters()) {
            m_params[param.id].store(param.defaultValue, std::memory_order_relaxed);
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

    void setBPM(float bpm) { m_bpm.store(std::clamp(bpm, 20.0f, 999.0f), std::memory_order_relaxed); }
    float getBPM() const { return m_bpm.load(std::memory_order_relaxed); }

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
        const auto target = targetFromNorm(m_params[kTarget].load(std::memory_order_relaxed));
        const auto wave = waveFromNorm(m_params[kWave].load(std::memory_order_relaxed));
        const bool sync = m_params[kSyncMode].load(std::memory_order_relaxed) > 0.5f;
        const float bpm = m_bpm.load(std::memory_order_relaxed);
        const float maxCutoff = 0.45f * sr;

        // Control-block architecture: parameters, the phase increment (pow /
        // divide) and the slew coefficient (exp) are computed once per block.
        // The waveform, S&H and slew stay per sample (that is the LFO). Volume
        // is a cheap linear function of the LFO so it is applied exactly per
        // sample; the Pan (cos/sin) and Cutoff (exp2/tan) transforms are
        // evaluated once per block from the block-boundary LFO value and their
        // coefficients are linearly interpolated across the block, so no
        // transcendental runs in the per-sample path.
        for (uint32_t blockStart = 0; blockStart < numFrames; blockStart += kCtrlBlock) {
            const uint32_t blockEnd = std::min(blockStart + kCtrlBlock, numFrames);
            const uint32_t blockLen = blockEnd - blockStart;

            m_depthSmoothed += (getParameter(kDepth) - m_depthSmoothed) * m_smoothCoeff;
            m_phaseOffSmoothed += (getParameter(kPhase) - m_phaseOffSmoothed) * m_smoothCoeff;
            const float depth = m_depthSmoothed;

            float periodSec;
            if (sync) {
                const int div = noteDivisionIndexFromParam(getParameter(kNoteDivision));
                periodSec = noteDivisionMultiplier(div) * 60.0f / bpm;
            } else {
                periodSec = 1.0f / rateHzFromNorm(getParameter(kRateHz));
            }
            const float phaseInc = 1.0f / (std::max(1.0e-3f, periodSec) * sr);

            const float smoothMs = smoothMsFromNorm(getParameter(kSmooth));
            const bool hardSlew = smoothMs < 0.01f;
            const float slewCoeff = hardSlew ? 1.0f : 1.0f - std::exp(-1.0f / (smoothMs * 0.001f * sr));

            // Pan/Cutoff coefficient targets from the block-boundary LFO value;
            // ramp the applied coefficients toward them across the block.
            float dPanGL = 0.0f, dPanGR = 0.0f, dca1 = 0.0f, dca2 = 0.0f, dca3 = 0.0f;
            if (target == kTargetPan) {
                const float pan = depth * m_lfoSlewed; // -1 .. +1
                const float a = (pan + 1.0f) * 0.25f * 3.14159265358979323846f;
                const float tgtGL = std::cos(a) * 1.41421356f;
                const float tgtGR = std::sin(a) * 1.41421356f;
                if (m_coeffsDirty) {
                    m_panGL = tgtGL;
                    m_panGR = tgtGR;
                } else {
                    dPanGL = (tgtGL - m_panGL) / static_cast<float>(blockLen);
                    dPanGR = (tgtGR - m_panGR) / static_cast<float>(blockLen);
                }
            } else if (target == kTargetCutoff) {
                const float u = 0.5f + 0.5f * m_lfoSlewed;
                const float fc =
                    std::clamp(kCutoffTopHz * std::exp2(-kCutoffSweepOctaves * depth * (1.0f - u)), 20.0f, maxCutoff);
                const float g = std::tan(3.14159265358979323846f * fc / sr);
                const float k = 1.41421356f; // Butterworth
                const float ta1 = 1.0f / (1.0f + g * (g + k));
                const float ta2 = g * ta1;
                const float ta3 = g * ta2;
                if (m_coeffsDirty) {
                    m_ca1 = ta1;
                    m_ca2 = ta2;
                    m_ca3 = ta3;
                } else {
                    dca1 = (ta1 - m_ca1) / static_cast<float>(blockLen);
                    dca2 = (ta2 - m_ca2) / static_cast<float>(blockLen);
                    dca3 = (ta3 - m_ca3) / static_cast<float>(blockLen);
                }
            }
            m_coeffsDirty = false;

            for (uint32_t i = blockStart; i < blockEnd; ++i) {
                // ── Advance the LFO phase ──
                m_phase += phaseInc;
                if (m_phase >= 1.0f)
                    m_phase -= std::floor(m_phase);

                float p = m_phase + m_phaseOffSmoothed;
                p -= std::floor(p);

                // S&H draws a fresh value each cycle (wrap of the offset phase).
                if (p < m_prevPhase) {
                    m_lcgState = m_lcgState * 1664525u + 1013904223u;
                    m_shValue = static_cast<float>(m_lcgState >> 8) * (1.0f / 8388608.0f) - 1.0f;
                }
                m_prevPhase = p;

                // ── Waveform, bipolar [-1, 1], "open" (+1) at phase 0 ──
                float b;
                switch (wave) {
                case kWaveSine:
                    b = std::cos(2.0f * 3.14159265358979323846f * p);
                    break;
                case kWaveTriangle:
                    b = 4.0f * std::abs(p - 0.5f) - 1.0f;
                    break;
                case kWaveSawDown:
                    b = 1.0f - 2.0f * p;
                    break;
                case kWaveSawUp:
                    b = 2.0f * p - 1.0f;
                    break;
                case kWaveSquare:
                    b = (p < 0.5f) ? 1.0f : -1.0f;
                    break;
                default:
                    b = m_shValue;
                    break;
                }

                // ── Output slew (one-pole toward the raw wave) ──
                if (hardSlew) {
                    m_lfoSlewed = b;
                } else {
                    m_lfoSlewed += (b - m_lfoSlewed) * slewCoeff;
                }

                const float inL = sanitizeSample(readInput(inputs, numInputChannels, 0, i));
                const float inR = stereo ? sanitizeSample(readInput(inputs, numInputChannels, 1, i)) : inL;

                float outL = inL;
                float outR = inR;
                switch (target) {
                case kTargetVolume: {
                    const float u = 0.5f + 0.5f * m_lfoSlewed; // unipolar, 1 = open
                    const float gain = 1.0f - depth * (1.0f - u);
                    outL = inL * gain;
                    outR = inR * gain;
                    break;
                }
                case kTargetPan: {
                    m_panGL += dPanGL;
                    m_panGR += dPanGR;
                    outL = inL * m_panGL;
                    outR = inR * m_panGR;
                    break;
                }
                default: { // kTargetCutoff
                    m_ca1 += dca1;
                    m_ca2 += dca2;
                    m_ca3 += dca3;
                    for (uint32_t ch = 0; ch < 2; ++ch) {
                        const float x = (ch == 0) ? inL : inR;
                        float& ic1 = m_ic1[ch];
                        float& ic2 = m_ic2[ch];
                        const float v3 = x - ic2;
                        const float v1 = m_ca1 * ic1 + m_ca2 * v3;
                        const float v2 = ic2 + m_ca2 * ic1 + m_ca3 * v3;
                        ic1 = 2.0f * v1 - ic1;
                        ic2 = 2.0f * v2 - ic2;
                        if (ch == 0)
                            outL = v2;
                        else
                            outR = v2;
                    }
                    break;
                }
                }

                if (outputs[0])
                    outputs[0][i] = flushDenormal(outL);
                if (numOutputChannels > 1 && outputs[1])
                    outputs[1][i] = flushDenormal(stereo ? outR : outL);
                for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                    if (outputs[ch])
                        outputs[ch][i] = 0.0f;
                }
            }
        }

        m_lfoLevel.store(u01(m_lfoSlewed), std::memory_order_relaxed);
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
            {kTarget, "Target", "TGT", "", 0.0f, 0.0f, 1.0f, true, false, false, kTargetCount - 1},
            {kWave, "Wave", "WAVE", "", 0.0f, 0.0f, 1.0f, true, false, false, kWaveCount - 1},
            {kSyncMode, "Sync", "SYNC", "", 1.0f, 0.0f, 1.0f, true, false, false, 1},
            {kRateHz, "Rate", "RATE", "Hz", 0.566f, 0.0f, 1.0f, true},                                // ~1 Hz
            {kNoteDivision, "Division", "DIV", "", 2.0f / 12.0f, 0.0f, 1.0f, true, false, false, 12}, // 1/4
            {kDepth, "Depth", "DPT", "%", 0.5f, 0.0f, 1.0f, true},
            {kPhase, "Phase", "PHS", "deg", 0.0f, 0.0f, 1.0f, true},
            {kSmooth, "Smooth", "SMTH", "ms", 0.05f, 0.0f, 1.0f, true}, // 10 ms
            {kBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1},
        };
    }

    std::string getParameterDisplay(uint32_t id) const override {
        if (id >= kParamCount)
            return "";
        const float v = getParameter(id);
        char buf[32];
        switch (id) {
        case kTarget:
            switch (targetFromNorm(v)) {
            case kTargetVolume:
                return "Volume";
            case kTargetPan:
                return "Pan";
            default:
                return "Cutoff";
            }
        case kWave:
            switch (waveFromNorm(v)) {
            case kWaveSine:
                return "Sine";
            case kWaveTriangle:
                return "Triangle";
            case kWaveSawDown:
                return "Saw Down";
            case kWaveSawUp:
                return "Saw Up";
            case kWaveSquare:
                return "Square";
            default:
                return "S&H";
            }
        case kSyncMode:
            return v > 0.5f ? "Sync" : "Free";
        case kRateHz:
            std::snprintf(buf, sizeof(buf), "%.2f Hz", rateHzFromNorm(v));
            return buf;
        case kNoteDivision:
            return noteDivisionLabel(noteDivisionIndexFromParam(v));
        case kDepth:
            return std::to_string(static_cast<int>(std::round(v * 100.0f))) + "%";
        case kPhase:
            std::snprintf(buf, sizeof(buf), "%.0f deg", v * 360.0f);
            return buf;
        case kSmooth:
            std::snprintf(buf, sizeof(buf), "%.0f ms", smoothMsFromNorm(v));
            return buf;
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
    std::pair<int, int> getEditorSize() const override { return {560, 320}; }
    bool resizeEditor(int, int) override { return false; }

    const PluginInfo& getInfo() const override { return m_info; }
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override {
        // Sample-rate-relative so the reported tail is constant in time, not in
        // samples. Volume/Pan are multiplicative (silent in -> silent out); the
        // Cutoff target's Butterworth SVF rings ~ln(1000)/(pi·20 Hz) ≈ 0.11 s at
        // the low end, so ~120 ms covers the worst case.
        return static_cast<uint32_t>(m_sampleRate * 0.12);
    }
    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

    // Metering (for the editor's LFO position indicator), 0..1.
    float getLfoLevel() const { return m_lfoLevel.load(std::memory_order_relaxed); }

    // ── Parameter mapping (public: shared with editor/tests) ──
    static float rateHzFromNorm(float value) {
        // 0.02 .. 20 Hz, log
        return 0.02f * std::pow(1000.0f, std::clamp(value, 0.0f, 1.0f));
    }

    static float smoothMsFromNorm(float value) { return std::clamp(value, 0.0f, 1.0f) * 200.0f; }

    static Target targetFromNorm(float value) {
        return static_cast<Target>(std::min<uint32_t>(
            kTargetCount - 1,
            static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * static_cast<float>(kTargetCount - 1) + 0.5f)));
    }

    static float normFromTarget(Target target) {
        return static_cast<float>(target) / static_cast<float>(kTargetCount - 1);
    }

    static Wave waveFromNorm(float value) {
        return static_cast<Wave>(std::min<uint32_t>(
            kWaveCount - 1,
            static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * static_cast<float>(kWaveCount - 1) + 0.5f)));
    }

    static float normFromWave(Wave wave) { return static_cast<float>(wave) / static_cast<float>(kWaveCount - 1); }

    static int noteDivisionIndexFromParam(float value) {
        return std::clamp(static_cast<int>(std::round(std::clamp(value, 0.0f, 1.0f) * 12.0f)), 0, 12);
    }

    static float noteDivisionParamFromIndex(int index) { return static_cast<float>(std::clamp(index, 0, 12)) / 12.0f; }

    /// Division length in beats (quarter notes), matching AestraDelay's table.
    static float noteDivisionMultiplier(int index) {
        switch (std::clamp(index, 0, 12)) {
        case kDiv1_1:
            return 4.0f;
        case kDiv1_2:
            return 2.0f;
        case kDiv1_4:
            return 1.0f;
        case kDiv1_8:
            return 0.5f;
        case kDiv1_16:
            return 0.25f;
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
            return 1.0f;
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
        case kDiv1_8:
            return "1/8";
        case kDiv1_16:
            return "1/16";
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
        default:
            return "1/16T";
        }
    }

private:
    static constexpr uint32_t kCtrlBlock = 16; // control-rate interval for param/coefficient updates

    static float u01(float bipolar) { return std::clamp(0.5f + 0.5f * bipolar, 0.0f, 1.0f); }

    void resetRuntimeState() {
        m_phase = 0.0f;
        m_prevPhase = 0.0f;
        m_lfoSlewed = 1.0f; // "open" so volume target starts transparent
        m_lcgState = 0x12345678u;
        m_shValue = 0.0f;
        m_ic1[0] = m_ic1[1] = 0.0f;
        m_ic2[0] = m_ic2[1] = 0.0f;
        m_coeffsDirty = true;
        m_panGL = 1.0f;
        m_panGR = 1.0f;
        m_ca1 = m_ca2 = m_ca3 = 0.0f;
        const float sr = static_cast<float>(m_sampleRate);
        // Smoothing runs once per control block, so the coefficient is scaled
        // to the block interval to keep the 2 ms time constant.
        m_smoothCoeff = 1.0f - std::exp(-static_cast<float>(kCtrlBlock) / (0.002f * sr));
        m_lfoLevel.store(1.0f, std::memory_order_relaxed);
    }

    void snapSmoothedParams() {
        m_depthSmoothed = getParameter(kDepth);
        m_phaseOffSmoothed = getParameter(kPhase);
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

    static float flushDenormal(float value) { return std::abs(value) < 1.0e-20f ? 0.0f : value; }

    // ── State ──
    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    std::atomic<bool> m_active{false};
    std::atomic<float> m_bpm{120.0f};
    std::array<std::atomic<float>, kParamCount> m_params{};

    float m_phase = 0.0f;
    float m_prevPhase = 0.0f;
    float m_lfoSlewed = 1.0f;
    uint32_t m_lcgState = 0x12345678u;
    float m_shValue = 0.0f;

    // ZDF SVF integrator state (cutoff target), per channel
    float m_ic1[2] = {0.0f, 0.0f};
    float m_ic2[2] = {0.0f, 0.0f};

    float m_smoothCoeff = 0.0f;
    float m_depthSmoothed = 0.5f;
    float m_phaseOffSmoothed = 0.0f;

    // Block-interpolated transform coefficients (Pan gains / Cutoff SVF).
    bool m_coeffsDirty = true;
    float m_panGL = 1.0f;
    float m_panGR = 1.0f;
    float m_ca1 = 0.0f;
    float m_ca2 = 0.0f;
    float m_ca3 = 0.0f;

    std::atomic<float> m_lfoLevel{1.0f};
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
