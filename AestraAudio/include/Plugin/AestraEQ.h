// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// EQ — V1 truthful parametric equalizer with biquad filters.
// 6 fixed-type bands: HPF, Low Shelf, Bell, Bell, High Shelf, LPF.

#pragma once

#include "Plugin/PluginHost.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Plugins {

// ============================================================================
// Biquad Filter — Direct Form II Transposed
// ============================================================================

class BiquadFilter {
public:
    BiquadFilter() { reset(); }

    void reset() {
        m_z1 = 0.0f;
        m_z2 = 0.0f;
    }

    void setCoeffs(float b0, float b1, float b2, float a0, float a1, float a2) {
        if (std::abs(a0) < 1.0e-12f) {
            m_b0 = 1.0f; m_b1 = 0.0f; m_b2 = 0.0f;
            m_a1 = 0.0f; m_a2 = 0.0f;
            return;
        }
        const float invA0 = 1.0f / a0;
        m_b0 = b0 * invA0;
        m_b1 = b1 * invA0;
        m_b2 = b2 * invA0;
        m_a1 = a1 * invA0;
        m_a2 = a2 * invA0;

        if (!std::isfinite(m_b0) || !std::isfinite(m_a1) || !std::isfinite(m_a2)) {
            m_b0 = 1.0f; m_b1 = 0.0f; m_b2 = 0.0f;
            m_a1 = 0.0f; m_a2 = 0.0f;
        }
    }

    float process(float in) {
        if (!std::isfinite(in)) {
            m_z1 = m_z2 = 0.0f;
            return 0.0f;
        }
        const float out = m_b0 * in + m_z1;
        m_z1 = m_b1 * in - m_a1 * out + m_z2;
        m_z2 = m_b2 * in - m_a2 * out;
        if (!std::isfinite(out)) {
            m_z1 = m_z2 = 0.0f;
            return 0.0f;
        }
        return out;
    }

    void process(float* buffer, uint32_t numFrames) {
        for (uint32_t i = 0; i < numFrames; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

private:
    float m_b0 = 1.0f, m_b1 = 0.0f, m_b2 = 0.0f;
    float m_a1 = 0.0f, m_a2 = 0.0f;
    float m_z1 = 0.0f, m_z2 = 0.0f;
};

// ============================================================================
// Filter Type Enum (internal, not all exposed in V1)
// ============================================================================

enum class FilterType : uint32_t {
    Bell = 0,
    LowCut = 1,
    HighCut = 2,
    LowShelf = 3,
    HighShelf = 4,
    Notch = 5,
    BandPass = 6,
    Tilt = 7
};

struct FilterCoeffs {
    float b0, b1, b2, a0, a1, a2;
};

static FilterCoeffs designBiquad(
    FilterType type,
    float frequency,
    float gainDb,
    float q,
    float sampleRate
) {
    constexpr float pi = 3.14159265358979323846f;
    const float w0 = 2.0f * pi * frequency / sampleRate;
    const float cos_w0 = std::cos(w0);
    const float sin_w0 = std::sin(w0);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float alpha = sin_w0 / (2.0f * q);

    float b0 = 0, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type) {
    case FilterType::Bell: {
        b0 = 1 + alpha * A;
        b1 = -2 * cos_w0;
        b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;
        a1 = -2 * cos_w0;
        a2 = 1 - alpha / A;
        break;
    }
    case FilterType::LowCut: {
        b0 =  (1 + cos_w0) / 2;
        b1 = -(1 + cos_w0);
        b2 =  (1 + cos_w0) / 2;
        a0 =   1 + alpha;
        a1 =  -2 * cos_w0;
        a2 =   1 - alpha;
        break;
    }
    case FilterType::HighCut: {
        b0 =  (1 - cos_w0) / 2;
        b1 =   1 - cos_w0;
        b2 =  (1 - cos_w0) / 2;
        a0 =   1 + alpha;
        a1 =  -2 * cos_w0;
        a2 =   1 - alpha;
        break;
    }
    case FilterType::LowShelf: {
        b0 = A * ((A + 1) - (A - 1) * cos_w0 + 2 * std::sqrt(A) * alpha);
        b1 = 2 * A * ((A - 1) - (A + 1) * cos_w0);
        b2 = A * ((A + 1) - (A - 1) * cos_w0 - 2 * std::sqrt(A) * alpha);
        a0 = (A + 1) + (A - 1) * cos_w0 + 2 * std::sqrt(A) * alpha;
        a1 = -2 * ((A - 1) + (A + 1) * cos_w0);
        a2 = (A + 1) + (A - 1) * cos_w0 - 2 * std::sqrt(A) * alpha;
        break;
    }
    case FilterType::HighShelf: {
        b0 = A * ((A + 1) + (A - 1) * cos_w0 + 2 * std::sqrt(A) * alpha);
        b1 = -2 * A * ((A - 1) + (A + 1) * cos_w0);
        b2 = A * ((A + 1) + (A - 1) * cos_w0 - 2 * std::sqrt(A) * alpha);
        a0 = (A + 1) - (A - 1) * cos_w0 + 2 * std::sqrt(A) * alpha;
        a1 = 2 * ((A - 1) - (A + 1) * cos_w0);
        a2 = (A + 1) - (A - 1) * cos_w0 - 2 * std::sqrt(A) * alpha;
        break;
    }
    case FilterType::Notch: {
        b0 = 1;
        b1 = -2 * cos_w0;
        b2 = 1;
        a0 = 1 + alpha;
        a1 = -2 * cos_w0;
        a2 = 1 - alpha;
        break;
    }
    case FilterType::BandPass: {
        b0 = alpha;
        b1 = 0;
        b2 = -alpha;
        a0 = 1 + alpha;
        a1 = -2 * cos_w0;
        a2 = 1 - alpha;
        break;
    }
    case FilterType::Tilt: {
        const float gainLow = std::pow(10.0f, gainDb / 20.0f);
        const float gainHigh = std::pow(10.0f, -gainDb / 20.0f);
        const float A_tilt = std::sqrt(gainLow);
        b0 = A_tilt * ((A_tilt + 1) + (A_tilt - 1) * cos_w0 + 2 * std::sqrt(A_tilt) * alpha);
        b1 = -2 * A_tilt * ((A_tilt - 1) + (A_tilt + 1) * cos_w0);
        b2 = A_tilt * ((A_tilt + 1) + (A_tilt - 1) * cos_w0 - 2 * std::sqrt(A_tilt) * alpha);
        a0 = (A_tilt + 1) - (A_tilt - 1) * cos_w0 + 2 * std::sqrt(A_tilt) * alpha;
        a1 = 2 * ((A_tilt - 1) - (A_tilt + 1) * cos_w0);
        a2 = (A_tilt + 1) - (A_tilt - 1) * cos_w0 - 2 * std::sqrt(A_tilt) * alpha;
        break;
    }
    }

    return { b0, b1, b2, a0, a1, a2 };
}

// ============================================================================
// EQ Band (internal)
// ============================================================================

struct EQBand {
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> type{0};
    std::atomic<float> frequency{0.5f};
    std::atomic<float> gain{0.5f};
    std::atomic<float> q{0.5f};
};

// ============================================================================
// State Blobs
// ============================================================================

struct EQStateBlobV1 {
    uint32_t magic;
    uint32_t version;
    float params[41];
    uint8_t enabled[8];
    uint8_t types[8];
};

struct EQStateBlobV2 {
    uint32_t magic;
    uint32_t version;
    float params[23]; // kV1ParamCount
};

// ============================================================================
// Aestra EQ V1 — 6-band fixed-type parametric equalizer
// ============================================================================

class AestraEQ : public IPluginInstance {
public:
    static constexpr uint32_t kNumBands = 8;         // Internal band count (editor compat)
    static constexpr uint32_t kV1BandCount = 6;      // V1 public band count
    static constexpr uint32_t kStateMagicV1 = 0x45510001;
    static constexpr uint32_t kStateMagicV2 = 0x45510002;
    static constexpr uint32_t kAnalyzerWindowSize = 1024;
    static constexpr uint32_t kMaxFilterStages = 4;  // V1 max: 48 dB/oct = 4 stages
    static constexpr uint32_t kBlockSize = 16;       // Smoothing update interval

    // V1 parameter IDs (22 params)
    static constexpr uint32_t kParamHPFEnable    = 0;
    static constexpr uint32_t kParamHPFFreq      = 1;
    static constexpr uint32_t kParamHPFSlope     = 2;
    static constexpr uint32_t kParamLShEnable    = 3;
    static constexpr uint32_t kParamLShFreq      = 4;
    static constexpr uint32_t kParamLShGain      = 5;
    static constexpr uint32_t kParamLShQ         = 6;
    static constexpr uint32_t kParamBell1Enable  = 7;
    static constexpr uint32_t kParamBell1Freq    = 8;
    static constexpr uint32_t kParamBell1Gain    = 9;
    static constexpr uint32_t kParamBell1Q       = 10;
    static constexpr uint32_t kParamBell2Enable  = 11;
    static constexpr uint32_t kParamBell2Freq    = 12;
    static constexpr uint32_t kParamBell2Gain    = 13;
    static constexpr uint32_t kParamBell2Q       = 14;
    static constexpr uint32_t kParamHShEnable    = 15;
    static constexpr uint32_t kParamHShFreq      = 16;
    static constexpr uint32_t kParamHShGain      = 17;
    static constexpr uint32_t kParamHShQ         = 18;
    static constexpr uint32_t kParamLPFEnable    = 19;
    static constexpr uint32_t kParamLPFFreq      = 20;
    static constexpr uint32_t kParamLPFSlope     = 21;
    static constexpr uint32_t kParamBypass       = 22;
    static constexpr uint32_t kV1ParamCount      = 23;

    // Legacy parameter count (for internal band mapping)
    static constexpr uint32_t kLegacyBandStride = 5;
    static constexpr uint32_t kLegacyParamCount = 41;

    AestraEQ() {
        initDefaults();
    }

    bool initialize(double sampleRate, uint32_t maxBlockSize) {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        computeSmoothingCoeff();
        for (uint32_t i = 0; i < kV1ParamCount; ++i) {
            m_smoothed[i].store(m_params[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        snapBandTypes();
        rebuildAllCoefficients();
        return true;
    }

    void shutdown() {}

    void activate() { m_active.store(true, std::memory_order_relaxed); }
    void deactivate() { m_active.store(false, std::memory_order_relaxed); }
    bool isActive() const { return m_active.load(std::memory_order_relaxed); }

    void process(const float* const* inputs, float** outputs,
                 uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* midiInput = nullptr,
                 MidiBuffer* midiOutput = nullptr) {
        (void)midiInput;
        (void)midiOutput;

        if (!m_active.load(std::memory_order_relaxed)) {
            copyOrSilence(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            publishAnalyzerFrame(outputs, std::min(numInputChannels, numOutputChannels), numFrames);
            return;
        }

        if (m_params[kParamBypass].load(std::memory_order_relaxed) > 0.5f) {
            copyOrSilence(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            publishAnalyzerFrame(outputs, std::min(numInputChannels, numOutputChannels), numFrames);
            return;
        }

        const uint32_t channels = std::min(numInputChannels, numOutputChannels);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (!inputs[ch] || !outputs[ch]) continue;
            std::memcpy(outputs[ch], inputs[ch], numFrames * sizeof(float));
        }

        const float smoothCoeff = m_smoothingCoeff.load(std::memory_order_relaxed);
        uint32_t sampleOffset = 0;

        while (sampleOffset < numFrames) {
            const uint32_t blockEnd = std::min(sampleOffset + kBlockSize, numFrames);
            const uint32_t blockFrames = blockEnd - sampleOffset;

            smoothParameters(smoothCoeff);
            rebuildAllCoefficients();

            for (uint32_t ch = 0; ch < channels; ++ch) {
                if (!inputs[ch] || !outputs[ch]) continue;
                float* buf = outputs[ch] + sampleOffset;
                for (uint32_t band = 0; band < kV1BandCount; ++band) {
                    if (!m_bandEnabled[band].load(std::memory_order_relaxed)) continue;
                    const uint32_t stages = m_bandStages[band].load(std::memory_order_relaxed);
                    const uint32_t stageBase = (ch * kV1BandCount + band) * kMaxFilterStages;
                    for (uint32_t stage = 0; stage < stages; ++stage) {
                        m_filters[stageBase + stage].process(buf, blockFrames);
                    }
                }
            }

            sampleOffset = blockEnd;
        }

        for (uint32_t ch = channels; ch < numOutputChannels; ++ch) {
            if (outputs[ch]) std::memset(outputs[ch], 0, numFrames * sizeof(float));
        }

        publishAnalyzerFrame(outputs, channels, numFrames);
    }

    // ---- Parameters (normalized 0-1) ----
    uint32_t getParameterCount() const { return kV1ParamCount; }

    float getParameter(uint32_t id) const {
        if (id >= kV1ParamCount) return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    void setParameter(uint32_t id, float value) {
        if (id >= kV1ParamCount) return;
        m_params[id].store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
        markDirtyForParam(id);
    }

    std::vector<PluginParameter> getParameters() const {
        std::vector<PluginParameter> params;
        params.reserve(kV1ParamCount);

        // Band 1 — High-Pass
        params.push_back({kParamHPFEnable, "High-Pass Enable", "HP On", "", 0.0f, 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamHPFFreq,   "High-Pass Frequency", "HP Freq", "Hz", 0.392f, 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamHPFSlope,  "High-Pass Slope", "HP Slp", "dB/oct", 0.333f, 0.0f, 1.0f, true, false, false, 3});

        // Band 2 — Low Shelf
        params.push_back({kParamLShEnable, "Low Shelf Enable", "LS On", "", 0.0f, 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamLShFreq,   "Low Shelf Frequency", "LS Freq", "Hz", 0.370f, 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamLShGain,   "Low Shelf Gain", "LS Gain", "dB", 0.5f, 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamLShQ,      "Low Shelf Q", "LS Q", "", 0.061f, 0.0f, 1.0f, true, false, false, 0});

        // Band 3 — Bell 1
        params.push_back({kParamBell1Enable, "Bell 1 Enable", "B1 On", "", 1.0f, 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamBell1Freq,   "Bell 1 Frequency", "B1 Freq", "Hz", 0.430f, 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamBell1Gain,   "Bell 1 Gain", "B1 Gain", "dB", 0.5f, 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamBell1Q,      "Bell 1 Q", "B1 Q", "", 0.091f, 0.0f, 1.0f, true, false, false, 0});

        // Band 4 — Bell 2
        params.push_back({kParamBell2Enable, "Bell 2 Enable", "B2 On", "", 1.0f, 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamBell2Freq,   "Bell 2 Frequency", "B2 Freq", "Hz", 0.607f, 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamBell2Gain,   "Bell 2 Gain", "B2 Gain", "dB", 0.5f, 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamBell2Q,      "Bell 2 Q", "B2 Q", "", 0.091f, 0.0f, 1.0f, true, false, false, 0});

        // Band 5 — High Shelf
        params.push_back({kParamHShEnable, "High Shelf Enable", "HS On", "", 0.0f, 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamHShFreq,   "High Shelf Frequency", "HS Freq", "Hz", 0.765f, 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamHShGain,   "High Shelf Gain", "HS Gain", "dB", 0.5f, 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamHShQ,      "High Shelf Q", "HS Q", "", 0.061f, 0.0f, 1.0f, true, false, false, 0});

        // Band 6 — Low-Pass
        params.push_back({kParamLPFEnable, "Low-Pass Enable", "LP On", "", 0.0f, 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamLPFFreq,   "Low-Pass Frequency", "LP Freq", "Hz", 0.926f, 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamLPFSlope,  "Low-Pass Slope", "LP Slp", "dB/oct", 0.0f, 0.0f, 1.0f, true, false, false, 3});

        // Master bypass
        params.push_back({kParamBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1});

        return params;
    }

    std::string getParameterDisplay(uint32_t id) const {
        if (id >= kV1ParamCount) return "";
        const float val = getParameter(id);

        if (id == kParamBypass) return val > 0.5f ? "ON" : "OFF";

        switch (id) {
        case kParamHPFEnable: case kParamLShEnable: case kParamBell1Enable:
        case kParamBell2Enable: case kParamHShEnable: case kParamLPFEnable:
            return val > 0.5f ? "ON" : "OFF";
        case kParamHPFFreq:  return formatFreq(hpfFreqToHz(val));
        case kParamLShFreq:  return formatFreq(lshFreqToHz(val));
        case kParamBell1Freq: return formatFreq(bell1FreqToHz(val));
        case kParamBell2Freq: return formatFreq(bell2FreqToHz(val));
        case kParamHShFreq:  return formatFreq(hshFreqToHz(val));
        case kParamLPFFreq:  return formatFreq(lpfFreqToHz(val));
        case kParamHPFSlope: return std::to_string(slopeDbPerOct(val)) + " dB/oct";
        case kParamLPFSlope: return std::to_string(slopeDbPerOct(val)) + " dB/oct";
        case kParamLShGain:  return formatGain(gainToDb(val));
        case kParamLShQ:     return formatQ(qToLinear(val));
        case kParamBell1Gain: return formatGain(gainToDb(val));
        case kParamBell1Q:   return formatQ(qToLinear(val));
        case kParamBell2Gain: return formatGain(gainToDb(val));
        case kParamBell2Q:   return formatQ(qToLinear(val));
        case kParamHShGain:  return formatGain(gainToDb(val));
        case kParamHShQ:     return formatQ(qToLinear(val));
        default: return "";
        }
    }

    // ---- State (V2 primary, V1 migration) ----
    std::vector<uint8_t> saveState() const {
        EQStateBlobV2 blob{};
        blob.magic = kStateMagicV2;
        blob.version = 2;
        for (uint32_t i = 0; i < kV1ParamCount; ++i) {
            blob.params[i] = getParameter(i);
        }
        const uint8_t* data = reinterpret_cast<const uint8_t*>(&blob);
        return std::vector<uint8_t>(data, data + sizeof(blob));
    }

    bool loadState(const std::vector<uint8_t>& state) {
        if (state.size() >= sizeof(EQStateBlobV2)) {
            const auto* v2 = reinterpret_cast<const EQStateBlobV2*>(state.data());
            if (v2->magic == kStateMagicV2 && v2->version == 2) {
                for (uint32_t i = 0; i < kV1ParamCount; ++i) {
                    setParameter(i, std::clamp(v2->params[i], 0.0f, 1.0f));
                }
                m_filtersDirty.store(true, std::memory_order_release);
                return true;
            }
        }

        if (state.size() >= sizeof(EQStateBlobV1)) {
            const auto* v1 = reinterpret_cast<const EQStateBlobV1*>(state.data());
            if (v1->magic == kStateMagicV1) {
                return migrateV1State(v1);
            }
        }

        return false;
    }

    // ---- Editor ----
    bool hasEditor() const { return true; }
    bool openEditor(void*) { return false; }
    void closeEditor() {}
    bool isEditorOpen() const { return false; }
    std::pair<int, int> getEditorSize() const { return {800, 600}; }
    bool resizeEditor(int, int) { return false; }

    const PluginInfo& getInfo() const { return m_info; }
    uint32_t getLatencySamples() const { return 0; }
    uint32_t getTailSamples() const { return 64; }
    double getAnalyzerSampleRate() const { return m_sampleRate; }
    bool getAnalyzerWindow(std::array<float, kAnalyzerWindowSize>& out, uint64_t* outSerial = nullptr) const {
        const uint64_t serial = m_publishedAnalyzerSerial.load(std::memory_order_acquire);
        if (serial == 0) return false;
        const uint32_t page = m_publishedAnalyzerPage.load(std::memory_order_acquire);
        out = m_analyzerPages[page];
        if (outSerial) *outSerial = serial;
        return true;
    }

    WatchdogStats getWatchdogStats() const { return {}; }
    void resetWatchdog() {}
    bool isBypassedByWatchdog() const { return false; }
    bool isCrashed() const { return false; }

    void setInfo(const PluginInfo& info) { m_info = info; }

    // Legacy accessors for editor compatibility
    uint32_t getLegacyParameterCount() const { return kLegacyParamCount; }

    float getLegacyParameter(uint32_t id) const {
        if (id >= kLegacyParamCount) return 0.0f;
        const uint32_t band = id / kLegacyBandStride;
        const uint32_t sub = id % kLegacyBandStride;
        if (band < kV1BandCount) {
            switch (sub) {
            case 0: return getParameter(bandV1EnableId(band));
            case 1: return 0.0f;
            case 2: return getParameter(bandV1FreqId(band));
            case 3: return getParameter(bandV1GainId(band));
            case 4: return getParameter(bandV1QId(band));
            }
        }
        return 0.5f;
    }

    void setLegacyParameter(uint32_t id, float value) {
        const uint32_t band = id / kLegacyBandStride;
        const uint32_t sub = id % kLegacyBandStride;
        if (band < kV1BandCount) {
            switch (sub) {
            case 0: setParameter(bandV1EnableId(band), value); break;
            case 2: setParameter(bandV1FreqId(band), value); break;
            case 3: setParameter(bandV1GainId(band), value); break;
            case 4: setParameter(bandV1QId(band), value); break;
            default: break;
            }
        }
    }

private:
    // ---- Safety ----
    static float sanitizeSample(float sample) {
        if (!std::isfinite(sample)) return 0.0f;
        return std::clamp(sample, -16.0f, 16.0f);
    }

    static void copyOrSilence(const float* const* inputs, float** outputs,
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

    // ---- Default initialization ----
    void initDefaults() {
        for (auto& p : m_params) p.store(0.0f, std::memory_order_relaxed);
        m_params[kParamHPFEnable].store(0.0f, std::memory_order_relaxed);
        m_params[kParamHPFFreq].store(0.392f, std::memory_order_relaxed);   // 80 Hz
        m_params[kParamHPFSlope].store(0.333f, std::memory_order_relaxed);  // 24 dB/oct
        m_params[kParamLShEnable].store(0.0f, std::memory_order_relaxed);
        m_params[kParamLShFreq].store(0.370f, std::memory_order_relaxed);   // 200 Hz
        m_params[kParamLShGain].store(0.5f, std::memory_order_relaxed);     // 0 dB
        m_params[kParamLShQ].store(0.061f, std::memory_order_relaxed);      // 0.707
        m_params[kParamBell1Enable].store(1.0f, std::memory_order_relaxed);
        m_params[kParamBell1Freq].store(0.430f, std::memory_order_relaxed);  // 500 Hz
        m_params[kParamBell1Gain].store(0.5f, std::memory_order_relaxed);    // 0 dB
        m_params[kParamBell1Q].store(0.091f, std::memory_order_relaxed);     // 1.0
        m_params[kParamBell2Enable].store(1.0f, std::memory_order_relaxed);
        m_params[kParamBell2Freq].store(0.607f, std::memory_order_relaxed);  // 2000 Hz
        m_params[kParamBell2Gain].store(0.5f, std::memory_order_relaxed);    // 0 dB
        m_params[kParamBell2Q].store(0.091f, std::memory_order_relaxed);     // 1.0
        m_params[kParamHShEnable].store(0.0f, std::memory_order_relaxed);
        m_params[kParamHShFreq].store(0.765f, std::memory_order_relaxed);   // 8000 Hz
        m_params[kParamHShGain].store(0.5f, std::memory_order_relaxed);     // 0 dB
        m_params[kParamHShQ].store(0.061f, std::memory_order_relaxed);      // 0.707
        m_params[kParamLPFEnable].store(0.0f, std::memory_order_relaxed);
        m_params[kParamLPFFreq].store(0.926f, std::memory_order_relaxed);   // 18000 Hz
        m_params[kParamLPFSlope].store(0.0f, std::memory_order_relaxed);    // 12 dB/oct
        m_params[kParamBypass].store(0.0f, std::memory_order_relaxed);

        for (auto& s : m_smoothed) s.store(0.0f, std::memory_order_relaxed);
    }

    void computeSmoothingCoeff() {
        const float coeff = 1.0f - std::exp(-1.0f / std::max(1.0f, static_cast<float>(m_sampleRate) * 0.005f));
        m_smoothingCoeff.store(coeff, std::memory_order_relaxed);
    }

    // ---- Band type/enable mapping ----
    static constexpr FilterType kBandTypes[kV1BandCount] = {
        FilterType::LowCut, FilterType::LowShelf, FilterType::Bell,
        FilterType::Bell, FilterType::HighShelf, FilterType::HighCut
    };

    static bool bandUsesSlope(uint32_t band) {
        return band == 0 || band == 5;
    }

    static bool bandUsesGain(uint32_t band) {
        return band >= 1 && band <= 4;
    }

    static uint32_t bandV1EnableId(uint32_t band) {
        static constexpr uint32_t ids[] = {
            kParamHPFEnable, kParamLShEnable, kParamBell1Enable,
            kParamBell2Enable, kParamHShEnable, kParamLPFEnable
        };
        return ids[band];
    }

    static uint32_t bandV1FreqId(uint32_t band) {
        static constexpr uint32_t ids[] = {
            kParamHPFFreq, kParamLShFreq, kParamBell1Freq,
            kParamBell2Freq, kParamHShFreq, kParamLPFFreq
        };
        return ids[band];
    }

    static uint32_t bandV1GainId(uint32_t band) {
        static constexpr uint32_t ids[] = {
            0, kParamLShGain, kParamBell1Gain, kParamBell2Gain, kParamHShGain, 0
        };
        return ids[band];
    }

    static uint32_t bandV1QId(uint32_t band) {
        static constexpr uint32_t ids[] = {
            kParamHPFSlope, kParamLShQ, kParamBell1Q, kParamBell2Q, kParamHShQ, kParamLPFSlope
        };
        return ids[band];
    }

    void snapBandTypes() {
        for (uint32_t i = 0; i < kV1BandCount; ++i) {
            m_bandEnabled[i].store(getParameter(bandV1EnableId(i)) > 0.5f, std::memory_order_relaxed);
        }
    }

    void markDirtyForParam(uint32_t id) {
        if (id == kParamBypass) return;
        if (id <= kParamLPFSlope) {
            const uint32_t band = bandIndexForParam(id);
            if (band < kV1BandCount) {
                m_bandEnabled[band].store(getParameter(bandV1EnableId(band)) > 0.5f, std::memory_order_relaxed);
            }
            m_filtersDirty.store(true, std::memory_order_release);
        }
    }

    static uint32_t bandIndexForParam(uint32_t id) {
        if (id <= kParamHPFSlope) return 0;
        if (id <= kParamLShQ) return 1;
        if (id <= kParamBell1Q) return 2;
        if (id <= kParamBell2Q) return 3;
        if (id <= kParamHShQ) return 4;
        if (id <= kParamLPFSlope) return 5;
        return kV1BandCount;
    }

    // ---- Mapping functions ----
    static float hpfFreqToHz(float norm) {
        return 20.0f * std::pow(25.0f, std::clamp(norm, 0.0f, 1.0f)); // 20–500 Hz
    }
    static float lshFreqToHz(float norm) {
        return 40.0f * std::pow(25.0f, std::clamp(norm, 0.0f, 1.0f)); // 40–1000 Hz
    }
    static float bell1FreqToHz(float norm) {
        return 80.0f * std::pow(100.0f, std::clamp(norm, 0.0f, 1.0f)); // 80–8000 Hz
    }
    static float bell2FreqToHz(float norm) {
        return 200.0f * std::pow(80.0f, std::clamp(norm, 0.0f, 1.0f)); // 200–16000 Hz
    }
    static float hshFreqToHz(float norm) {
        return 2000.0f * std::pow(10.0f, std::clamp(norm, 0.0f, 1.0f)); // 2000–20000 Hz
    }
    static float lpfFreqToHz(float norm) {
        return 1000.0f * std::pow(20.0f, std::clamp(norm, 0.0f, 1.0f)); // 1000–20000 Hz
    }

    static float gainToDb(float norm) {
        return -18.0f + std::clamp(norm, 0.0f, 1.0f) * 36.0f;
    }

    static float qToLinear(float norm) {
        return 0.1f + std::clamp(norm, 0.0f, 1.0f) * 9.9f;
    }

    static uint32_t slopeDbPerOct(float norm) {
        static constexpr uint32_t slopes[] = {12, 24, 36, 48};
        const float clamped = std::clamp(norm, 0.0f, 1.0f);
        const uint32_t idx = static_cast<uint32_t>(std::round(clamped * 3.0f));
        return slopes[std::min(idx, 3u)];
    }

    static uint32_t slopeStageCount(float norm) {
        const uint32_t db = slopeDbPerOct(norm);
        return db / 12;
    }

    float bandFrequencyHz(uint32_t band, float norm) const {
        switch (band) {
        case 0: return hpfFreqToHz(norm);
        case 1: return lshFreqToHz(norm);
        case 2: return bell1FreqToHz(norm);
        case 3: return bell2FreqToHz(norm);
        case 4: return hshFreqToHz(norm);
        case 5: return lpfFreqToHz(norm);
        default: return 1000.0f;
        }
    }

    float bandQValue(uint32_t band, float norm) const {
        if (bandUsesSlope(band)) return 0.70710678f; // Butterworth
        return qToLinear(norm);
    }

    uint32_t bandStageCount(uint32_t band, float slopeNorm) const {
        if (!bandUsesSlope(band)) return 1;
        return slopeStageCount(slopeNorm);
    }

    // ---- Display helpers ----
    static std::string formatFreq(float hz) {
        if (hz >= 1000.0f) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1fkHz", hz / 1000.0f);
            return buf;
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%dHz", static_cast<int>(hz + 0.5f));
        return buf;
    }

    static std::string formatGain(float db) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%+.1fdB", db);
        return buf;
    }

    static std::string formatQ(float q) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", q);
        return buf;
    }

    // ---- Smoothing ----
    void smoothParameters(float coeff) {
        for (uint32_t i = 0; i < kV1ParamCount; ++i) {
            if (i == kParamBypass) continue;
            const float target = m_params[i].load(std::memory_order_relaxed);
            float current = m_smoothed[i].load(std::memory_order_relaxed);
            current += (target - current) * coeff;
            m_smoothed[i].store(current, std::memory_order_relaxed);
        }
    }

    // ---- Coefficient rebuild ----
    void rebuildAllCoefficients() {
        for (uint32_t band = 0; band < kV1BandCount; ++band) {
            const bool enabled = m_bandEnabled[band].load(std::memory_order_relaxed);
            const FilterType type = kBandTypes[band];

            m_bandStages[band].store(enabled ? bandStageCount(band, m_smoothed[bandV1QId(band)].load(std::memory_order_relaxed)) : 1u, std::memory_order_relaxed);

            if (!enabled) {
                for (uint32_t ch = 0; ch < 2; ++ch) {
                    const uint32_t stageBase = (ch * kV1BandCount + band) * kMaxFilterStages;
                    for (uint32_t stage = 0; stage < kMaxFilterStages; ++stage) {
                        m_filters[stageBase + stage].setCoeffs(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
                    }
                }
                continue;
            }

            float freqNorm, gainNorm, qNorm, slopeNorm;
            readSmoothedBandParams(band, freqNorm, gainNorm, qNorm, slopeNorm);

            float freq = bandFrequencyHz(band, freqNorm);
            freq = std::clamp(freq, 20.0f, static_cast<float>(m_sampleRate) * 0.49f);
            const float gainDb = bandUsesGain(band) ? gainToDb(gainNorm) : 0.0f;
            float q = bandQValue(band, qNorm);
            q = std::clamp(q, 0.1f, 10.0f);
            const uint32_t stages = bandStageCount(band, slopeNorm);

            auto coeffs = designBiquad(type, freq, gainDb, q, static_cast<float>(m_sampleRate));
            if (!std::isfinite(coeffs.b0) || !std::isfinite(coeffs.a0) || std::abs(coeffs.a0) < 1.0e-12f) {
                coeffs = {1, 0, 0, 1, 0, 0};
            }

            for (uint32_t ch = 0; ch < 2; ++ch) {
                const uint32_t stageBase = (ch * kV1BandCount + band) * kMaxFilterStages;
                for (uint32_t stage = 0; stage < kMaxFilterStages; ++stage) {
                    if (stage < stages) {
                        m_filters[stageBase + stage].setCoeffs(
                            coeffs.b0, coeffs.b1, coeffs.b2, coeffs.a0, coeffs.a1, coeffs.a2
                        );
                    } else {
                        m_filters[stageBase + stage].setCoeffs(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
                    }
                }
            }
        }
    }

    void readSmoothedBandParams(uint32_t band, float& freq, float& gain, float& q, float& slope) const {
        freq = m_smoothed[bandV1FreqId(band)].load(std::memory_order_relaxed);
        if (bandUsesGain(band)) {
            gain = m_smoothed[bandV1GainId(band)].load(std::memory_order_relaxed);
        } else {
            gain = 0.5f;
        }
        if (bandUsesSlope(band)) {
            slope = m_smoothed[bandV1QId(band)].load(std::memory_order_relaxed);
            q = 0.5f;
        } else {
            q = m_smoothed[bandV1QId(band)].load(std::memory_order_relaxed);
            slope = 0.0f;
        }
    }

    // ---- State migration ----
    bool migrateV1State(const EQStateBlobV1* v1) {
        static constexpr FilterType v1BandTargetTypes[] = {
            FilterType::LowCut, FilterType::LowShelf, FilterType::Bell,
            FilterType::Bell, FilterType::HighShelf, FilterType::HighCut
        };

        for (uint32_t band = 0; band < kV1BandCount; ++band) {
            const uint32_t v1Base = band * kLegacyBandStride;
            const bool v1Enabled = v1->enabled[band] != 0;
            const uint32_t v1Type = v1->types[band];

            setParameter(bandV1EnableId(band), v1Enabled ? 1.0f : 0.0f);

            if (v1Type == static_cast<uint32_t>(v1BandTargetTypes[band])) {
                if (v1Base + 4 < kLegacyParamCount) {
                    setParameter(bandV1FreqId(band), std::clamp(v1->params[v1Base + 2], 0.0f, 1.0f));
                    if (bandUsesGain(band)) {
                        setParameter(bandV1GainId(band), std::clamp(v1->params[v1Base + 3], 0.0f, 1.0f));
                    }
                    setParameter(bandV1QId(band), std::clamp(v1->params[v1Base + 4], 0.0f, 1.0f));
                }
            } else {
                // Type mismatch — load defaults for this band
                for (const auto& p : getParameters()) {
                    if (p.id == bandV1FreqId(band)) {
                        setParameter(bandV1FreqId(band), p.defaultValue);
                        break;
                    }
                }
                if (bandUsesGain(band)) {
                    for (const auto& p : getParameters()) {
                        if (p.id == bandV1GainId(band)) {
                            setParameter(bandV1GainId(band), p.defaultValue);
                            break;
                        }
                    }
                }
                for (const auto& p : getParameters()) {
                    if (p.id == bandV1QId(band)) {
                        setParameter(bandV1QId(band), p.defaultValue);
                        break;
                    }
                }
            }
        }

        if (kLegacyParamCount > 40) {
            setParameter(kParamBypass, v1->params[40] > 0.5f ? 1.0f : 0.0f);
        }

        m_filtersDirty.store(true, std::memory_order_release);
        return true;
    }

    // ---- Analyzer ----
    void publishAnalyzerFrame(float** outputs, uint32_t numChannels, uint32_t numFrames) {
        if (numChannels == 0 || !outputs) return;

        for (uint32_t i = 0; i < numFrames; ++i) {
            float mono = 0.0f;
            uint32_t count = 0;
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                if (outputs[ch]) {
                    mono += outputs[ch][i];
                    ++count;
                }
            }
            mono = (count > 1) ? mono / static_cast<float>(count) : mono;

            m_analyzerPages[m_analyzerWritePage][m_analyzerWritePos++] = mono;
            if (m_analyzerWritePos >= kAnalyzerWindowSize) {
                m_publishedAnalyzerPage.store(m_analyzerWritePage, std::memory_order_release);
                m_publishedAnalyzerSerial.fetch_add(1, std::memory_order_release);
                m_analyzerWritePage = 1u - m_analyzerWritePage;
                m_analyzerWritePos = 0;
            }
        }
    }

    // ---- Member state ----
    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    uint32_t m_maxBlockSize = 512;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_filtersDirty{true};
    std::atomic<float> m_smoothingCoeff{0.001f};

    std::array<std::atomic<float>, kV1ParamCount> m_params{};
    std::array<std::atomic<float>, kV1ParamCount> m_smoothed{};
    std::array<std::atomic<bool>, kV1BandCount> m_bandEnabled{};
    std::array<std::atomic<uint32_t>, kV1BandCount> m_bandStages{};

    // 2 channels * kV1BandCount bands * kMaxFilterStages
    std::array<BiquadFilter, 2 * kV1BandCount * kMaxFilterStages> m_filters;

    std::array<std::array<float, kAnalyzerWindowSize>, 2> m_analyzerPages{};
    std::atomic<uint32_t> m_publishedAnalyzerPage{0};
    std::atomic<uint64_t> m_publishedAnalyzerSerial{0};
    uint32_t m_analyzerWritePage = 0;
    uint32_t m_analyzerWritePos = 0;
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
