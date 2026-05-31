// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// EQ — V1 truthful parametric equalizer with cascaded IIR filters.
// 6 primary bands: HPF, Low Shelf, two parametric middle bands, High Shelf, LPF.

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

    void setCoeffs(double b0, double b1, double b2, double a0, double a1, double a2) {
        if (std::abs(a0) < 1.0e-24) {
            m_b0 = 1.0; m_b1 = 0.0; m_b2 = 0.0;
            m_a1 = 0.0; m_a2 = 0.0;
            return;
        }
        const double invA0 = 1.0 / a0;
        m_b0 = b0 * invA0;
        m_b1 = b1 * invA0;
        m_b2 = b2 * invA0;
        m_a1 = a1 * invA0;
        m_a2 = a2 * invA0;

        if (!std::isfinite(m_b0) || !std::isfinite(m_a1) || !std::isfinite(m_a2)) {
            m_b0 = 1.0; m_b1 = 0.0; m_b2 = 0.0;
            m_a1 = 0.0; m_a2 = 0.0;
        }
    }

    float process(float in) {
        if (!std::isfinite(in)) {
            m_z1 = m_z2 = 0.0f;
            return 0.0f;
        }
        const double inSample = static_cast<double>(in);
        const double out = m_b0 * inSample + m_z1;
        m_z1 = m_b1 * inSample - m_a1 * out + m_z2;
        m_z2 = m_b2 * inSample - m_a2 * out;
        if (!std::isfinite(out)) {
            m_z1 = m_z2 = 0.0;
            return 0.0f;
        }
        m_z1 = zapDenormal(m_z1);
        m_z2 = zapDenormal(m_z2);
        return static_cast<float>(zapDenormal(out));
    }

    void process(float* buffer, uint32_t numFrames) {
        const double b0 = m_b0, b1 = m_b1, b2 = m_b2;
        const double a1 = m_a1, a2 = m_a2;
        double z1 = m_z1, z2 = m_z2;
        for (uint32_t i = 0; i < numFrames; ++i) {
            const float in = buffer[i];
            if (!std::isfinite(in)) {
                z1 = z2 = 0.0;
                buffer[i] = 0.0f;
                continue;
            }
            const double inSample = static_cast<double>(in);
            const double out = b0 * inSample + z1;
            z1 = b1 * inSample - a1 * out + z2;
            z2 = b2 * inSample - a2 * out;
            buffer[i] = static_cast<float>(zapDenormal(out));
        }
        m_z1 = zapDenormal(z1);
        m_z2 = zapDenormal(z2);
    }

private:
    static double zapDenormal(double value) {
        return std::abs(value) < 1.0e-24 ? 0.0 : value;
    }

    double m_b0 = 1.0, m_b1 = 0.0, m_b2 = 0.0;
    double m_a1 = 0.0, m_a2 = 0.0;
    double m_z1 = 0.0, m_z2 = 0.0;
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
    double b0, b1, b2, a0, a1, a2;
};

static FilterCoeffs designBiquad(FilterType type, float frequency, float gainDb, float q, float sampleRate) {
    constexpr double pi = 3.14159265358979323846;
    const double w0 = 2.0 * pi * static_cast<double>(frequency) / static_cast<double>(sampleRate);
    const double cos_w0 = std::cos(w0);
    const double sin_w0 = std::sin(w0);
    const double A = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
    const double alpha = sin_w0 / (2.0 * static_cast<double>(q));

    double b0 = 0.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

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
    case FilterType::Tilt:
        // Tilt is implemented as staged low/high shelf filters by AestraEQ.
        b0 = 1;
        b1 = 0;
        b2 = 0;
        a0 = 1;
        a1 = 0;
        a2 = 0;
        break;
    }

    return { b0, b1, b2, a0, a1, a2 };
}

static FilterCoeffs designFirstOrderCut(FilterType type, float frequency, float sampleRate) {
    constexpr double pi = 3.14159265358979323846;
    const double k = std::tan(pi * static_cast<double>(frequency) / static_cast<double>(sampleRate));
    const double norm = 1.0 / std::max(1.0 + k, 1.0e-24);

    if (type == FilterType::LowCut) {
        return {norm, -norm, 0.0, 1.0, (k - 1.0) * norm, 0.0};
    }
    if (type == FilterType::HighCut) {
        return {k * norm, k * norm, 0.0, 1.0, (k - 1.0) * norm, 0.0};
    }
    return {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
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

struct EQStateBlobV3 {
    uint32_t magic;
    uint32_t version;
    float params[25]; // V3 param count
};

struct EQStateBlobV4 {
    uint32_t magic;
    uint32_t version;
    float params[26]; // V4 param count
};

struct EQStateBlobV5 {
    uint32_t magic;
    uint32_t version;
    float params[27]; // V5 param count
};

struct EQStateBlobV6 {
    uint32_t magic;
    uint32_t version;
    float params[33]; // V6 param count
};

struct EQDynamicBandStateV7 {
    uint8_t enabled;
    uint8_t reserved[3];
    float typeNorm;
    float stereoNorm;
    float frequencyNorm;
    float gainNorm;
    float qOrSlopeNorm;
};

struct EQStateBlobV7 {
    uint32_t magic;
    uint32_t version;
    float params[33]; // V6/V7 public param count
    EQDynamicBandStateV7 dynamicBands[24];
};

struct EQDynamicBandStateV8 {
    uint8_t enabled;
    uint8_t dynamicEnabled;
    uint8_t sidechainLinked;
    uint8_t reserved;
    float typeNorm;
    float stereoNorm;
    float frequencyNorm;
    float gainNorm;
    float qOrSlopeNorm;
    float targetGainNorm;
    float thresholdNorm;
    float kneeNorm;
    float attackNorm;
    float releaseNorm;
    float sidechainTypeNorm;
    float sidechainFrequencyNorm;
    float sidechainQNorm;
};

struct EQStateBlobV8 {
    uint32_t magic;
    uint32_t version;
    float params[33]; // V6/V7/V8 public param count
    EQDynamicBandStateV8 dynamicBands[24];
};

// ============================================================================
// Aestra EQ V1 — 6-band fixed-type parametric equalizer
// ============================================================================

class AestraEQ : public IPluginInstance {
public:
    static constexpr uint32_t kNumBands = 8;         // Legacy state/editor compatibility count
    static constexpr uint32_t kV1BandCount = 6;      // V1 public band count
    static constexpr uint32_t kLegacyBandCount = kV1BandCount;
    static constexpr uint32_t kMaxDynamicBands = 24; // Target advanced EQ slot count
    static constexpr uint32_t kStateMagicV1 = 0x45510001;
    static constexpr uint32_t kStateMagicV2 = 0x45510002;
    static constexpr uint32_t kStateMagicV3 = 0x45510003;
    static constexpr uint32_t kStateMagicV4 = 0x45510004;
    static constexpr uint32_t kStateMagicV5 = 0x45510005;
    static constexpr uint32_t kStateMagicV6 = 0x45510006;
    static constexpr uint32_t kStateMagicV7 = 0x45510007;
    static constexpr uint32_t kStateMagicV8 = 0x45510008;
    static constexpr uint32_t kAnalyzerWindowSize = 1024;
    static constexpr uint32_t kAnalyzerSourceCount = 2;
    static constexpr uint32_t kAnalyzerStereoModeCount = 5;
    static constexpr uint32_t kMaxFilterStages = 8;  // V1 max: 96 dB/oct = 8 stages
    static constexpr uint32_t kBlockSize = 16;       // Smoothing update interval

    enum class AnalyzerSource : uint32_t {
        Pre = 0,
        Post = 1,
    };

    enum class StereoMode : uint32_t {
        Stereo = 0,
        Left = 1,
        Right = 2,
        Mid = 3,
        Side = 4,
    };

    // V1 parameter IDs plus V3 middle-band type extensions.
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
    static constexpr uint32_t kParamBell1Type    = 23;
    static constexpr uint32_t kParamBell2Type    = 24;
    static constexpr uint32_t kParamOutputGain   = 25;
    static constexpr uint32_t kParamPolarityInvert = 26;
    static constexpr uint32_t kParamHPFStereoMode = 27;
    static constexpr uint32_t kParamLShStereoMode = 28;
    static constexpr uint32_t kParamBell1StereoMode = 29;
    static constexpr uint32_t kParamBell2StereoMode = 30;
    static constexpr uint32_t kParamHShStereoMode = 31;
    static constexpr uint32_t kParamLPFStereoMode = 32;
    static constexpr uint32_t kV1ParamCount      = 23;
    static constexpr uint32_t kParamCount        = 33;

    // Legacy parameter count (for internal band mapping)
    static constexpr uint32_t kLegacyBandStride = 5;
    static constexpr uint32_t kLegacyParamCount = 41;

    struct LegacyBandSlot {
        const char* id;
        const char* typeLabel;
        uint32_t enableId;
        uint32_t freqId;
        uint32_t gainId;
        uint32_t qId;
        uint32_t typeId;
        uint32_t stereoModeId;
        FilterType defaultType;
        bool usesSlope;
    };

    struct DynamicBandSlotDefaults {
        bool enabled = false;
        FilterType type = FilterType::Bell;
        StereoMode stereoMode = StereoMode::Stereo;
        float frequencyNorm = 0.5f;
        float gainNorm = 0.5f;
        float qOrSlopeNorm = 0.091f;
        bool usesSlope = false;
        bool dynamicEnabled = false;
        float targetGainNorm = 0.5f;
        float thresholdNorm = 0.5f;
        float kneeNorm = 0.10f;
        float attackNorm = 0.18f;
        float releaseNorm = 0.36f;
        bool sidechainLinked = true;
        FilterType sidechainType = FilterType::BandPass;
        float sidechainFrequencyNorm = 0.5f;
        float sidechainQNorm = 0.091f;
    };

    struct DynamicBandSlotSnapshot {
        uint32_t slotIndex = 0;
        bool legacySlot = false;
        bool enabled = false;
        FilterType type = FilterType::Bell;
        StereoMode stereoMode = StereoMode::Stereo;
        float frequencyNorm = 0.5f;
        float gainNorm = 0.5f;
        float qOrSlopeNorm = 0.091f;
        bool usesSlope = false;
        bool dynamicEnabled = false;
        float targetGainNorm = 0.5f;
        float thresholdNorm = 0.5f;
        float kneeNorm = 0.10f;
        float attackNorm = 0.18f;
        float releaseNorm = 0.36f;
        bool sidechainLinked = true;
        FilterType sidechainType = FilterType::BandPass;
        float sidechainFrequencyNorm = 0.5f;
        float sidechainQNorm = 0.091f;
    };

    static LegacyBandSlot legacyBandSlot(uint32_t band) {
        static constexpr LegacyBandSlot slots[kLegacyBandCount] = {
            {"HP", "Cut", kParamHPFEnable, kParamHPFFreq, 0, kParamHPFSlope, 0, kParamHPFStereoMode,
             FilterType::LowCut, true},
            {"LS", "Shelf", kParamLShEnable, kParamLShFreq, kParamLShGain, kParamLShQ, 0, kParamLShStereoMode,
             FilterType::LowShelf, false},
            {"B1", "Bell", kParamBell1Enable, kParamBell1Freq, kParamBell1Gain, kParamBell1Q, kParamBell1Type,
             kParamBell1StereoMode, FilterType::Bell, false},
            {"B2", "Bell", kParamBell2Enable, kParamBell2Freq, kParamBell2Gain, kParamBell2Q, kParamBell2Type,
             kParamBell2StereoMode, FilterType::Bell, false},
            {"HS", "Shelf", kParamHShEnable, kParamHShFreq, kParamHShGain, kParamHShQ, 0, kParamHShStereoMode,
             FilterType::HighShelf, false},
            {"LP", "Cut", kParamLPFEnable, kParamLPFFreq, 0, kParamLPFSlope, 0, kParamLPFStereoMode,
             FilterType::HighCut, true},
        };
        return slots[std::min(band, kLegacyBandCount - 1)];
    }

    static DynamicBandSlotDefaults dynamicBandSlotDefaults(uint32_t slot) {
        if (slot < kLegacyBandCount) {
            const auto legacy = legacyBandSlot(slot);
            return {
                defaultParameterValue(legacy.enableId) > 0.5f,
                legacy.defaultType,
                StereoMode::Stereo,
                defaultParameterValue(legacy.freqId),
                legacy.gainId != 0 ? defaultParameterValue(legacy.gainId) : 0.5f,
                defaultParameterValue(legacy.qId),
                legacy.usesSlope,
                false,
                0.5f,
                0.5f,
                0.10f,
                0.18f,
                0.36f,
                true,
                FilterType::BandPass,
                defaultParameterValue(legacy.freqId),
                defaultParameterValue(legacy.qId),
            };
        }
        return {
            false,
            FilterType::Bell,
            StereoMode::Stereo,
            0.5f,
            0.5f,
            defaultParameterValue(kParamBell1Q),
            false,
            false,
            0.5f,
            0.5f,
            0.10f,
            0.18f,
            0.36f,
            true,
            FilterType::BandPass,
            0.5f,
            defaultParameterValue(kParamBell1Q),
        };
    }

    static DynamicBandSlotDefaults dynamicBandGraphCreationDefaults(float frequencyNorm, float gainNorm) {
        auto defaults = dynamicBandSlotDefaults(kLegacyBandCount);
        const float clampedFrequency = std::clamp(frequencyNorm, 0.0f, 1.0f);
        const float hz = 20.0f * std::pow(1000.0f, clampedFrequency);
        defaults.enabled = true;
        defaults.frequencyNorm = clampedFrequency;
        defaults.gainNorm = std::clamp(gainNorm, 0.0f, 1.0f);
        defaults.stereoMode = StereoMode::Stereo;
        const bool lowGraphClick = defaults.gainNorm <= 0.18f;
        if (hz <= 24.0f) {
            defaults.type = FilterType::LowCut;
            defaults.gainNorm = 0.5f;
            defaults.qOrSlopeNorm = defaultParameterValue(kParamHPFSlope);
            defaults.usesSlope = true;
        } else if (hz < 50.0f) {
            defaults.type = FilterType::LowShelf;
            defaults.qOrSlopeNorm = defaultParameterValue(kParamLShQ);
            defaults.usesSlope = false;
        } else if (hz < 5000.0f) {
            defaults.type = lowGraphClick ? FilterType::Notch : FilterType::Bell;
            defaults.gainNorm = lowGraphClick ? 0.5f : defaults.gainNorm;
            defaults.qOrSlopeNorm = defaultParameterValue(kParamBell1Q);
            defaults.usesSlope = false;
        } else if (hz < 15000.0f) {
            defaults.type = FilterType::HighShelf;
            defaults.qOrSlopeNorm = defaultParameterValue(kParamHShQ);
            defaults.usesSlope = false;
        } else {
            defaults.type = FilterType::HighCut;
            defaults.gainNorm = 0.5f;
            defaults.qOrSlopeNorm = defaultParameterValue(kParamLPFSlope);
            defaults.usesSlope = true;
        }
        return defaults;
    }

    AestraEQ() {
        initDefaults();
    }

    bool initialize(double sampleRate, uint32_t maxBlockSize) {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        computeSmoothingCoeff();
        for (uint32_t i = 0; i < kParamCount; ++i) {
            m_smoothed[i].store(m_params[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        snapDynamicBandSmoothing();
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

        if (!outputs || numOutputChannels == 0 || numFrames == 0) {
            return;
        }

        if (!m_active.load(std::memory_order_relaxed)) {
            copyOrSilence(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            publishAnalyzerFrame(AnalyzerSource::Pre, inputs, numInputChannels, numFrames);
            publishAnalyzerFrame(AnalyzerSource::Post, outputs, std::min(numInputChannels, numOutputChannels), numFrames);
            return;
        }

        if (m_params[kParamBypass].load(std::memory_order_relaxed) > 0.5f) {
            copyOrSilence(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
            publishAnalyzerFrame(AnalyzerSource::Pre, inputs, numInputChannels, numFrames);
            publishAnalyzerFrame(AnalyzerSource::Post, outputs, std::min(numInputChannels, numOutputChannels), numFrames);
            return;
        }

        publishAnalyzerFrame(AnalyzerSource::Pre, inputs, numInputChannels, numFrames);

        constexpr uint32_t kSupportedProcessChannels = 2;
        const uint32_t channels = std::min(numOutputChannels, kSupportedProcessChannels);
        std::array<bool, kSupportedProcessChannels> channelHasInput{};
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (!outputs[ch]) continue;
            const bool hasInput = inputs && ch < numInputChannels && inputs[ch];
            channelHasInput[ch] = hasInput;
            if (!hasInput) {
                std::memset(outputs[ch], 0, numFrames * sizeof(float));
                continue;
            }
            for (uint32_t i = 0; i < numFrames; ++i) {
                outputs[ch][i] = sanitizeSample(inputs[ch][i]);
            }
        }
        for (uint32_t ch = channels; ch < numOutputChannels; ++ch) {
            if (outputs[ch] && inputs && ch < numInputChannels && inputs[ch]) {
                for (uint32_t i = 0; i < numFrames; ++i) {
                    outputs[ch][i] = sanitizePassThroughSample(inputs[ch][i]);
                }
            } else if (outputs[ch]) {
                std::memset(outputs[ch], 0, numFrames * sizeof(float));
            }
        }

        const float smoothCoeff = m_smoothingCoeff.load(std::memory_order_relaxed);
        uint32_t sampleOffset = 0;

        while (sampleOffset < numFrames) {
            const uint32_t blockEnd = std::min(sampleOffset + kBlockSize, numFrames);
            const uint32_t blockFrames = blockEnd - sampleOffset;

            const int32_t soloBand = activeSoloBand();
            if (soloBand != m_runtimeSoloBand) {
                m_runtimeSoloBand = soloBand;
            }

            const bool dynamicPending =
                updateDynamicEnvelopes(outputs, channelHasInput, channels, sampleOffset, blockFrames, soloBand);
            const bool smoothingPending = smoothParametersAndCheckPending(smoothCoeff);
            if (m_filtersDirty.exchange(false, std::memory_order_acq_rel) || smoothingPending || dynamicPending) {
                rebuildAllCoefficients();
            }

            for (uint32_t band = 0; band < kMaxDynamicBands; ++band) {
                if (soloBand >= 0 && band != static_cast<uint32_t>(soloBand)) continue;
                if (!m_bandEnabled[band].load(std::memory_order_relaxed)) continue;
                processBandBlock(band, outputs, channelHasInput, channels, sampleOffset, blockFrames);
            }

            for (uint32_t ch = 0; ch < channels; ++ch) {
                if (!channelHasInput[ch] || !outputs[ch]) continue;
                applyOutputStage(outputs[ch] + sampleOffset, blockFrames);
            }

            sampleOffset = blockEnd;
        }

        publishAnalyzerFrame(AnalyzerSource::Post, outputs, channels, numFrames);
    }

    // ---- Parameters (normalized 0-1) ----
    uint32_t getParameterCount() const { return kParamCount; }

    float getParameter(uint32_t id) const {
        if (id >= kParamCount) return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    static float defaultParameterValue(uint32_t id) {
        switch (id) {
        case kParamHPFEnable: return 0.0f;
        case kParamHPFFreq: return 0.392f;       // 80 Hz
        case kParamHPFSlope: return 0.333f;      // 24 dB/oct
        case kParamLShEnable: return 0.0f;
        case kParamLShFreq: return 0.370f;       // 200 Hz
        case kParamLShGain: return 0.5f;         // 0 dB
        case kParamLShQ: return 0.061f;          // 0.707
        case kParamBell1Enable: return 0.0f;
        case kParamBell1Freq: return 0.430f;     // 500 Hz
        case kParamBell1Gain: return 0.5f;       // 0 dB
        case kParamBell1Q: return 0.091f;        // 1.0
        case kParamBell2Enable: return 0.0f;
        case kParamBell2Freq: return 0.607f;     // 2000 Hz
        case kParamBell2Gain: return 0.5f;       // 0 dB
        case kParamBell2Q: return 0.091f;        // 1.0
        case kParamHShEnable: return 0.0f;
        case kParamHShFreq: return 0.765f;       // 8000 Hz
        case kParamHShGain: return 0.5f;         // 0 dB
        case kParamHShQ: return 0.061f;          // 0.707
        case kParamLPFEnable: return 0.0f;
        case kParamLPFFreq: return 0.926f;       // 18000 Hz
        case kParamLPFSlope: return 0.166f;      // 12 dB/oct
        case kParamBypass: return 0.0f;
        case kParamBell1Type: return 0.0f;       // Bell
        case kParamBell2Type: return 0.0f;       // Bell
        case kParamOutputGain: return 0.5f;      // 0 dB
        case kParamPolarityInvert: return 0.0f;
        case kParamHPFStereoMode:
        case kParamLShStereoMode:
        case kParamBell1StereoMode:
        case kParamBell2StereoMode:
        case kParamHShStereoMode:
        case kParamLPFStereoMode: return 0.0f;   // Stereo
        default: return 0.0f;
        }
    }

    void setParameter(uint32_t id, float value) {
        if (id >= kParamCount) return;
        m_params[id].store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
        markDirtyForParam(id);
    }

    std::vector<PluginParameter> getParameters() const {
        std::vector<PluginParameter> params;
        params.reserve(kParamCount);

        // Band 1 — High-Pass
        params.push_back({kParamHPFEnable, "High-Pass Enable", "HP On", "", defaultParameterValue(kParamHPFEnable), 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamHPFFreq,   "High-Pass Frequency", "HP Freq", "Hz", defaultParameterValue(kParamHPFFreq), 0.0f, 1.0f, true, false, false, 0});
        params.push_back(
            {kParamHPFSlope, "High-Pass Slope", "HP Slp", "dB/oct", defaultParameterValue(kParamHPFSlope), 0.0f, 1.0f, true, false, false, 6}
        );

        // Band 2 — Low Shelf
        params.push_back({kParamLShEnable, "Low Shelf Enable", "LS On", "", defaultParameterValue(kParamLShEnable), 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamLShFreq,   "Low Shelf Frequency", "LS Freq", "Hz", defaultParameterValue(kParamLShFreq), 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamLShGain,   "Low Shelf Gain", "LS Gain", "dB", defaultParameterValue(kParamLShGain), 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamLShQ,      "Low Shelf Q", "LS Q", "", defaultParameterValue(kParamLShQ), 0.0f, 1.0f, true, false, false, 0});

        // Band 3 — Bell 1
        params.push_back({kParamBell1Enable, "Bell 1 Enable", "B1 On", "", defaultParameterValue(kParamBell1Enable), 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamBell1Freq,   "Bell 1 Frequency", "B1 Freq", "Hz", defaultParameterValue(kParamBell1Freq), 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamBell1Gain,   "Bell 1 Gain", "B1 Gain", "dB", defaultParameterValue(kParamBell1Gain), 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamBell1Q,      "Bell 1 Q", "B1 Q", "", defaultParameterValue(kParamBell1Q), 0.0f, 1.0f, true, false, false, 0});

        // Band 4 — Bell 2
        params.push_back({kParamBell2Enable, "Bell 2 Enable", "B2 On", "", defaultParameterValue(kParamBell2Enable), 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamBell2Freq,   "Bell 2 Frequency", "B2 Freq", "Hz", defaultParameterValue(kParamBell2Freq), 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamBell2Gain,   "Bell 2 Gain", "B2 Gain", "dB", defaultParameterValue(kParamBell2Gain), 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamBell2Q,      "Bell 2 Q", "B2 Q", "", defaultParameterValue(kParamBell2Q), 0.0f, 1.0f, true, false, false, 0});

        // Band 5 — High Shelf
        params.push_back({kParamHShEnable, "High Shelf Enable", "HS On", "", defaultParameterValue(kParamHShEnable), 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamHShFreq,   "High Shelf Frequency", "HS Freq", "Hz", defaultParameterValue(kParamHShFreq), 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamHShGain,   "High Shelf Gain", "HS Gain", "dB", defaultParameterValue(kParamHShGain), 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamHShQ,      "High Shelf Q", "HS Q", "", defaultParameterValue(kParamHShQ), 0.0f, 1.0f, true, false, false, 0});

        // Band 6 — Low-Pass
        params.push_back({kParamLPFEnable, "Low-Pass Enable", "LP On", "", defaultParameterValue(kParamLPFEnable), 0.0f, 1.0f, true, false, false, 1});
        params.push_back({kParamLPFFreq,   "Low-Pass Frequency", "LP Freq", "Hz", defaultParameterValue(kParamLPFFreq), 0.0f, 1.0f, true, false, false, 0});
        params.push_back(
            {kParamLPFSlope, "Low-Pass Slope", "LP Slp", "dB/oct", defaultParameterValue(kParamLPFSlope), 0.0f, 1.0f, true, false, false, 6}
        );

        // Master bypass
        params.push_back({kParamBypass, "Bypass", "BYP", "", defaultParameterValue(kParamBypass), 0.0f, 1.0f, true, true, false, 1});

        // Surgical modes for the two fully parametric bands. Added after the original V1 IDs.
        params.push_back({kParamBell1Type, "Bell 1 Type", "B1 Type", "", defaultParameterValue(kParamBell1Type), 0.0f, 1.0f, true, false, false, 3});
        params.push_back({kParamBell2Type, "Bell 2 Type", "B2 Type", "", defaultParameterValue(kParamBell2Type), 0.0f, 1.0f, true, false, false, 3});

        // Output stage
        params.push_back({kParamOutputGain, "Output Gain", "OUT", "dB", defaultParameterValue(kParamOutputGain), 0.0f, 1.0f, true, false, false, 0});
        params.push_back({kParamPolarityInvert, "Polarity Invert", "POL", "", defaultParameterValue(kParamPolarityInvert), 0.0f, 1.0f, true, false, false, 1});

        params.push_back({kParamHPFStereoMode, "High-Pass Stereo Placement", "HP Mode", "", defaultParameterValue(kParamHPFStereoMode), 0.0f, 1.0f, true, false, false, 4});
        params.push_back({kParamLShStereoMode, "Low Shelf Stereo Placement", "LS Mode", "", defaultParameterValue(kParamLShStereoMode), 0.0f, 1.0f, true, false, false, 4});
        params.push_back({kParamBell1StereoMode, "Bell 1 Stereo Placement", "B1 Mode", "", defaultParameterValue(kParamBell1StereoMode), 0.0f, 1.0f, true, false, false, 4});
        params.push_back({kParamBell2StereoMode, "Bell 2 Stereo Placement", "B2 Mode", "", defaultParameterValue(kParamBell2StereoMode), 0.0f, 1.0f, true, false, false, 4});
        params.push_back({kParamHShStereoMode, "High Shelf Stereo Placement", "HS Mode", "", defaultParameterValue(kParamHShStereoMode), 0.0f, 1.0f, true, false, false, 4});
        params.push_back({kParamLPFStereoMode, "Low-Pass Stereo Placement", "LP Mode", "", defaultParameterValue(kParamLPFStereoMode), 0.0f, 1.0f, true, false, false, 4});

        return params;
    }

    std::string getParameterDisplay(uint32_t id) const {
        if (id >= kParamCount) return "";
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
        case kParamBell1Type:
        case kParamBell2Type: return filterTypeLabel(typeParamToFilterType(val));
        case kParamOutputGain: return formatGain(gainToDb(val));
        case kParamPolarityInvert: return val > 0.5f ? "INV" : "NORMAL";
        case kParamHPFStereoMode:
        case kParamLShStereoMode:
        case kParamBell1StereoMode:
        case kParamBell2StereoMode:
        case kParamHShStereoMode:
        case kParamLPFStereoMode: return stereoModeLabel(stereoModeFromParam(val));
        default: return "";
        }
    }

    // ---- State (V8 primary, V7/V6/V5/V4/V3/V2/V1 migration) ----
    std::vector<uint8_t> saveState() const {
        EQStateBlobV8 blob{};
        blob.magic = kStateMagicV8;
        blob.version = 8;
        for (uint32_t i = 0; i < kParamCount; ++i) {
            blob.params[i] = getParameter(i);
        }
        for (uint32_t slot = 0; slot < kMaxDynamicBands; ++slot) {
            const auto snapshot = getDynamicBandSlotSnapshot(slot);
            auto& state = blob.dynamicBands[slot];
            state.enabled = snapshot.enabled ? 1u : 0u;
            state.typeNorm = dynamicFilterTypeToParam(snapshot.type);
            state.stereoNorm = stereoModeToParam(snapshot.stereoMode);
            state.frequencyNorm = snapshot.frequencyNorm;
            state.gainNorm = snapshot.gainNorm;
            state.qOrSlopeNorm = snapshot.qOrSlopeNorm;
            state.dynamicEnabled = snapshot.dynamicEnabled ? 1u : 0u;
            state.targetGainNorm = snapshot.targetGainNorm;
            state.thresholdNorm = snapshot.thresholdNorm;
            state.kneeNorm = snapshot.kneeNorm;
            state.attackNorm = snapshot.attackNorm;
            state.releaseNorm = snapshot.releaseNorm;
            state.sidechainLinked = snapshot.sidechainLinked ? 1u : 0u;
            state.sidechainTypeNorm = dynamicFilterTypeToParam(snapshot.sidechainType);
            state.sidechainFrequencyNorm = snapshot.sidechainFrequencyNorm;
            state.sidechainQNorm = snapshot.sidechainQNorm;
        }
        const uint8_t* data = reinterpret_cast<const uint8_t*>(&blob);
        return std::vector<uint8_t>(data, data + sizeof(blob));
    }

    bool loadState(const std::vector<uint8_t>& state) {
        if (state.size() >= sizeof(EQStateBlobV8)) {
            const auto* v8 = reinterpret_cast<const EQStateBlobV8*>(state.data());
            if (v8->magic == kStateMagicV8 && v8->version == 8) {
                for (uint32_t i = 0; i < kParamCount; ++i) {
                    setParameter(i, std::clamp(v8->params[i], 0.0f, 1.0f));
                }
                resetDynamicBandSlots();
                for (uint32_t slot = kLegacyBandCount; slot < kMaxDynamicBands; ++slot) {
                    const auto& saved = v8->dynamicBands[slot];
                    if (saved.enabled == 0u) {
                        continue;
                    }
                    auto defaults = dynamicBandSlotDefaults(slot);
                    defaults.enabled = true;
                    defaults.type = dynamicTypeParamToFilterType(std::clamp(saved.typeNorm, 0.0f, 1.0f));
                    defaults.stereoMode = stereoModeFromParam(std::clamp(saved.stereoNorm, 0.0f, 1.0f));
                    defaults.frequencyNorm = std::clamp(saved.frequencyNorm, 0.0f, 1.0f);
                    defaults.gainNorm = std::clamp(saved.gainNorm, 0.0f, 1.0f);
                    defaults.qOrSlopeNorm = std::clamp(saved.qOrSlopeNorm, 0.0f, 1.0f);
                    defaults.dynamicEnabled = saved.dynamicEnabled != 0u;
                    defaults.targetGainNorm = std::clamp(saved.targetGainNorm, 0.0f, 1.0f);
                    defaults.thresholdNorm = std::clamp(saved.thresholdNorm, 0.0f, 1.0f);
                    defaults.kneeNorm = std::clamp(saved.kneeNorm, 0.0f, 1.0f);
                    defaults.attackNorm = std::clamp(saved.attackNorm, 0.0f, 1.0f);
                    defaults.releaseNorm = std::clamp(saved.releaseNorm, 0.0f, 1.0f);
                    defaults.sidechainLinked = saved.sidechainLinked != 0u;
                    defaults.sidechainType =
                        dynamicTypeParamToFilterType(std::clamp(saved.sidechainTypeNorm, 0.0f, 1.0f));
                    defaults.sidechainFrequencyNorm = std::clamp(saved.sidechainFrequencyNorm, 0.0f, 1.0f);
                    defaults.sidechainQNorm = std::clamp(saved.sidechainQNorm, 0.0f, 1.0f);
                    setDynamicBandSlot(slot, defaults);
                }
                m_soloBand.store(-1, std::memory_order_release);
                m_filtersDirty.store(true, std::memory_order_release);
                return true;
            }
        }

        if (state.size() >= sizeof(EQStateBlobV7)) {
            const auto* v7 = reinterpret_cast<const EQStateBlobV7*>(state.data());
            if (v7->magic == kStateMagicV7 && v7->version == 7) {
                for (uint32_t i = 0; i < kParamCount; ++i) {
                    setParameter(i, std::clamp(v7->params[i], 0.0f, 1.0f));
                }
                resetDynamicBandSlots();
                for (uint32_t slot = kLegacyBandCount; slot < kMaxDynamicBands; ++slot) {
                    const auto& saved = v7->dynamicBands[slot];
                    if (saved.enabled == 0u) {
                        continue;
                    }
                    auto defaults = dynamicBandSlotDefaults(slot);
                    defaults.enabled = true;
                    defaults.type = dynamicTypeParamToFilterType(std::clamp(saved.typeNorm, 0.0f, 1.0f));
                    defaults.stereoMode = stereoModeFromParam(std::clamp(saved.stereoNorm, 0.0f, 1.0f));
                    defaults.frequencyNorm = std::clamp(saved.frequencyNorm, 0.0f, 1.0f);
                    defaults.gainNorm = std::clamp(saved.gainNorm, 0.0f, 1.0f);
                    defaults.qOrSlopeNorm = std::clamp(saved.qOrSlopeNorm, 0.0f, 1.0f);
                    setDynamicBandSlot(slot, defaults);
                }
                m_soloBand.store(-1, std::memory_order_release);
                m_filtersDirty.store(true, std::memory_order_release);
                return true;
            }
        }

        if (state.size() >= sizeof(EQStateBlobV6)) {
            const auto* v6 = reinterpret_cast<const EQStateBlobV6*>(state.data());
            if (v6->magic == kStateMagicV6 && v6->version == 6) {
                for (uint32_t i = 0; i < kParamCount; ++i) {
                    setParameter(i, std::clamp(v6->params[i], 0.0f, 1.0f));
                }
                resetDynamicBandSlots();
                m_soloBand.store(-1, std::memory_order_release);
                m_filtersDirty.store(true, std::memory_order_release);
                return true;
            }
        }

        if (state.size() >= sizeof(EQStateBlobV5)) {
            const auto* v5 = reinterpret_cast<const EQStateBlobV5*>(state.data());
            if (v5->magic == kStateMagicV5 && v5->version == 5) {
                for (uint32_t i = 0; i < kParamHPFStereoMode; ++i) {
                    setParameter(i, std::clamp(v5->params[i], 0.0f, 1.0f));
                }
                defaultStereoPlacementParams();
                resetDynamicBandSlots();
                m_soloBand.store(-1, std::memory_order_release);
                m_filtersDirty.store(true, std::memory_order_release);
                return true;
            }
        }

        if (state.size() >= sizeof(EQStateBlobV4)) {
            const auto* v4 = reinterpret_cast<const EQStateBlobV4*>(state.data());
            if (v4->magic == kStateMagicV4 && v4->version == 4) {
                for (uint32_t i = 0; i < kParamPolarityInvert; ++i) {
                    setParameter(i, std::clamp(v4->params[i], 0.0f, 1.0f));
                }
                setParameter(kParamPolarityInvert, 0.0f);
                defaultStereoPlacementParams();
                resetDynamicBandSlots();
                m_soloBand.store(-1, std::memory_order_release);
                m_filtersDirty.store(true, std::memory_order_release);
                return true;
            }
        }

        if (state.size() >= sizeof(EQStateBlobV3)) {
            const auto* v3 = reinterpret_cast<const EQStateBlobV3*>(state.data());
            if (v3->magic == kStateMagicV3 && v3->version == 3) {
                for (uint32_t i = 0; i < kParamOutputGain; ++i) {
                    setParameter(i, std::clamp(v3->params[i], 0.0f, 1.0f));
                }
                setParameter(kParamOutputGain, 0.5f);
                setParameter(kParamPolarityInvert, 0.0f);
                defaultStereoPlacementParams();
                resetDynamicBandSlots();
                m_soloBand.store(-1, std::memory_order_release);
                m_filtersDirty.store(true, std::memory_order_release);
                return true;
            }
        }

        if (state.size() >= sizeof(EQStateBlobV2)) {
            const auto* v2 = reinterpret_cast<const EQStateBlobV2*>(state.data());
            if (v2->magic == kStateMagicV2 && v2->version == 2) {
                for (uint32_t i = 0; i < kV1ParamCount; ++i) {
                    const float value = (i == kParamHPFSlope || i == kParamLPFSlope)
                        ? legacySlopeNormToExtended(v2->params[i])
                        : std::clamp(v2->params[i], 0.0f, 1.0f);
                    setParameter(i, value);
                }
                setParameter(kParamBell1Type, 0.0f);
                setParameter(kParamBell2Type, 0.0f);
                setParameter(kParamOutputGain, 0.5f);
                setParameter(kParamPolarityInvert, 0.0f);
                defaultStereoPlacementParams();
                resetDynamicBandSlots();
                m_soloBand.store(-1, std::memory_order_release);
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
    double getMagnitudeResponseDb(double frequencyHz) const {
        if (m_params[kParamBypass].load(std::memory_order_relaxed) > 0.5f) {
            return 0.0;
        }

        const double sampleRate = std::max(1.0, m_sampleRate);
        const double nyquist = sampleRate * 0.5;
        const double hz = std::clamp(frequencyHz, 1.0, nyquist * 0.98);

        double responseDb = 0.0;
        const int32_t soloBand = activeSoloBand();
        for (uint32_t band = 0; band < kMaxDynamicBands; ++band) {
            if (soloBand >= 0 && band != static_cast<uint32_t>(soloBand)) continue;
            responseDb += getBandMagnitudeResponseDb(band, hz);
        }
        return std::clamp(responseDb, -240.0, 240.0);
    }

    double getBandMagnitudeResponseDb(uint32_t band, double frequencyHz) const {
        if (band >= kMaxDynamicBands || !m_bandEnabled[band].load(std::memory_order_relaxed)) {
            return 0.0;
        }

        const double sampleRate = std::max(1.0, m_sampleRate);
        const double nyquist = sampleRate * 0.5;
        const double hz = std::clamp(frequencyHz, 1.0, nyquist * 0.98);
        const double omega = 2.0 * 3.14159265358979323846 * hz / sampleRate;

        const float freqNorm = slotFrequencyNorm(band);
        const FilterType type = slotFilterType(band);
        const float gainNorm = filterTypeUsesGain(type) ? slotGainNorm(band) : 0.5f;
        const float qNorm = slotQNorm(band);
        const float slopeNorm = slotUsesSlope(band) ? qNorm : 0.0f;

        const float freqHz = std::clamp(
            slotFrequencyHz(band, freqNorm),
            20.0f,
            static_cast<float>(sampleRate * 0.49)
        );
        const float gainDb = filterTypeUsesGain(type) ? gainToDb(gainNorm) : 0.0f;
        const float q = std::clamp(slotQValue(band, qNorm), 0.1f, 10.0f);
        const bool firstOrderCut = slotUsesSlope(band) && slopeUsesFirstOrderStage(slopeNorm);
        const uint32_t stages = slotStageCount(band, type, slopeNorm);

        double bandDb = 0.0;
        for (uint32_t stage = 0; stage < stages; ++stage) {
            const float stageQ = stageQForBand(band, stage, stages, q);
            const auto coeffs = stageCoeffsForFilter(
                type, freqHz, gainDb, stageQ, static_cast<float>(sampleRate), stage, firstOrderCut
            );
            bandDb += biquadMagnitudeDb(coeffs, omega);
        }
        return std::clamp(bandDb, -240.0, 240.0);
    }

    bool getAnalyzerWindow(std::array<float, kAnalyzerWindowSize>& out,
                           uint64_t* outSerial = nullptr,
                           AnalyzerSource source = AnalyzerSource::Post,
                           StereoMode mode = StereoMode::Stereo) const {
        const uint32_t sourceIdx = analyzerSourceIndex(source);
        const uint32_t modeIdx = analyzerStereoModeIndex(mode);
        const uint64_t serial = m_publishedAnalyzerSerial[sourceIdx][modeIdx].load(std::memory_order_acquire);
        if (serial == 0) return false;
        const uint32_t page = m_publishedAnalyzerPage[sourceIdx][modeIdx].load(std::memory_order_acquire);
        const uint64_t pageSerial = m_analyzerPageSerial[sourceIdx][modeIdx][page].load(std::memory_order_acquire);
        if ((pageSerial & 1u) != 0u) return false;
        out = m_analyzerPages[sourceIdx][modeIdx][page];
        const uint64_t pageSerialAfter = m_analyzerPageSerial[sourceIdx][modeIdx][page].load(std::memory_order_acquire);
        const uint64_t serialAfter = m_publishedAnalyzerSerial[sourceIdx][modeIdx].load(std::memory_order_acquire);
        const uint32_t pageAfter = m_publishedAnalyzerPage[sourceIdx][modeIdx].load(std::memory_order_acquire);
        if (pageSerialAfter != pageSerial || serialAfter != serial || pageAfter != page) return false;
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

    uint32_t getDynamicBandSlotCount() const { return kMaxDynamicBands; }

    DynamicBandSlotSnapshot getDynamicBandSlotSnapshot(uint32_t slot) const {
        const uint32_t clampedSlot = std::min(slot, kMaxDynamicBands - 1);
        if (clampedSlot < kLegacyBandCount) {
            const auto legacy = legacyBandSlot(clampedSlot);
            const float typeNorm = legacy.typeId != 0 ? getParameter(legacy.typeId) : 0.0f;
            auto snapshot = DynamicBandSlotSnapshot{};
            snapshot.slotIndex = clampedSlot;
            snapshot.legacySlot = true;
            snapshot.enabled = getParameter(legacy.enableId) > 0.5f;
            snapshot.type = bandFilterType(clampedSlot, typeNorm);
            snapshot.stereoMode = stereoModeFromParam(getParameter(legacy.stereoModeId));
            snapshot.frequencyNorm = getParameter(legacy.freqId);
            snapshot.gainNorm = legacy.gainId != 0 ? getParameter(legacy.gainId) : 0.5f;
            snapshot.qOrSlopeNorm = getParameter(legacy.qId);
            snapshot.usesSlope = legacy.usesSlope;
            snapshot.sidechainFrequencyNorm = snapshot.frequencyNorm;
            snapshot.sidechainQNorm = snapshot.qOrSlopeNorm;
            return snapshot;
        }

        const auto defaults = dynamicBandSlotSnapshotFromAtomics(clampedSlot);
        auto snapshot = DynamicBandSlotSnapshot{};
        snapshot.slotIndex = clampedSlot;
        snapshot.legacySlot = false;
        snapshot.enabled = defaults.enabled;
        snapshot.type = defaults.type;
        snapshot.stereoMode = defaults.stereoMode;
        snapshot.frequencyNorm = defaults.frequencyNorm;
        snapshot.gainNorm = defaults.gainNorm;
        snapshot.qOrSlopeNorm = defaults.qOrSlopeNorm;
        snapshot.usesSlope = defaults.usesSlope;
        snapshot.dynamicEnabled = defaults.dynamicEnabled;
        snapshot.targetGainNorm = defaults.targetGainNorm;
        snapshot.thresholdNorm = defaults.thresholdNorm;
        snapshot.kneeNorm = defaults.kneeNorm;
        snapshot.attackNorm = defaults.attackNorm;
        snapshot.releaseNorm = defaults.releaseNorm;
        snapshot.sidechainLinked = defaults.sidechainLinked;
        snapshot.sidechainType = defaults.sidechainType;
        snapshot.sidechainFrequencyNorm = defaults.sidechainFrequencyNorm;
        snapshot.sidechainQNorm = defaults.sidechainQNorm;
        return snapshot;
    }

    float getDynamicBandEnvelopeAmount(uint32_t slot) const {
        if (slot >= kMaxDynamicBands) {
            return 0.0f;
        }
        return std::clamp(m_dynamicEnvelopeMeters[slot].load(std::memory_order_relaxed), 0.0f, 1.0f);
    }

    bool setDynamicBandSlot(uint32_t slot, const DynamicBandSlotDefaults& defaults) {
        if (slot < kLegacyBandCount || slot >= kMaxDynamicBands) {
            return false;
        }

        const bool wasEnabled = m_bandEnabled[slot].load(std::memory_order_relaxed);
        const FilterType previousType =
            dynamicTypeParamToFilterType(m_dynamicBandTypes[slot].load(std::memory_order_relaxed));
        const StereoMode previousMode =
            stereoModeFromParam(m_dynamicBandStereoModes[slot].load(std::memory_order_relaxed));

        DynamicBandSlotDefaults clamped = defaults;
        clamped.frequencyNorm = std::clamp(clamped.frequencyNorm, 0.0f, 1.0f);
        clamped.gainNorm = std::clamp(clamped.gainNorm, 0.0f, 1.0f);
        clamped.qOrSlopeNorm = std::clamp(clamped.qOrSlopeNorm, 0.0f, 1.0f);
        clamped.targetGainNorm = std::clamp(clamped.targetGainNorm, 0.0f, 1.0f);
        clamped.thresholdNorm = std::clamp(clamped.thresholdNorm, 0.0f, 1.0f);
        clamped.kneeNorm = std::clamp(clamped.kneeNorm, 0.0f, 1.0f);
        clamped.attackNorm = std::clamp(clamped.attackNorm, 0.0f, 1.0f);
        clamped.releaseNorm = std::clamp(clamped.releaseNorm, 0.0f, 1.0f);
        clamped.sidechainFrequencyNorm = std::clamp(clamped.sidechainFrequencyNorm, 0.0f, 1.0f);
        clamped.sidechainQNorm = std::clamp(clamped.sidechainQNorm, 0.0f, 1.0f);
        clamped.usesSlope = clamped.type == FilterType::LowCut || clamped.type == FilterType::HighCut;
        if (clamped.sidechainLinked) {
            clamped.sidechainFrequencyNorm = clamped.frequencyNorm;
            clamped.sidechainQNorm = clamped.qOrSlopeNorm;
        }
        m_dynamicBandSlots[slot] = clamped;
        m_dynamicBandTypes[slot].store(dynamicFilterTypeToParam(clamped.type), std::memory_order_relaxed);
        m_dynamicBandStereoModes[slot].store(stereoModeToParam(clamped.stereoMode), std::memory_order_relaxed);
        m_dynamicBandFreqs[slot].store(clamped.frequencyNorm, std::memory_order_relaxed);
        m_dynamicBandGains[slot].store(clamped.gainNorm, std::memory_order_relaxed);
        m_dynamicBandQs[slot].store(clamped.qOrSlopeNorm, std::memory_order_relaxed);
        m_dynamicBandDynamicEnabled[slot].store(clamped.dynamicEnabled, std::memory_order_relaxed);
        m_dynamicBandTargetGains[slot].store(clamped.targetGainNorm, std::memory_order_relaxed);
        m_dynamicBandThresholds[slot].store(clamped.thresholdNorm, std::memory_order_relaxed);
        m_dynamicBandKnees[slot].store(clamped.kneeNorm, std::memory_order_relaxed);
        m_dynamicBandAttacks[slot].store(clamped.attackNorm, std::memory_order_relaxed);
        m_dynamicBandReleases[slot].store(clamped.releaseNorm, std::memory_order_relaxed);
        m_dynamicBandSidechainLinked[slot].store(clamped.sidechainLinked, std::memory_order_relaxed);
        m_dynamicBandSidechainTypes[slot].store(dynamicFilterTypeToParam(clamped.sidechainType),
                                                std::memory_order_relaxed);
        m_dynamicBandSidechainFreqs[slot].store(clamped.sidechainFrequencyNorm, std::memory_order_relaxed);
        m_dynamicBandSidechainQs[slot].store(clamped.sidechainQNorm, std::memory_order_relaxed);
        m_bandEnabled[slot].store(clamped.enabled, std::memory_order_relaxed);
        if (!wasEnabled || !clamped.enabled || previousType != clamped.type || previousMode != clamped.stereoMode) {
            snapDynamicBandSmoothing(slot);
        }
        if (!clamped.enabled && m_soloBand.load(std::memory_order_acquire) == static_cast<int32_t>(slot)) {
            m_soloBand.store(-1, std::memory_order_release);
        }
        m_filtersDirty.store(true, std::memory_order_release);
        return true;
    }

    void setSoloBandSlot(int32_t slot) {
        const int32_t next = (slot < 0 || slot >= static_cast<int32_t>(kMaxDynamicBands)) ? -1 : slot;
        const int32_t previous = m_soloBand.exchange(next, std::memory_order_acq_rel);
        if (previous != next) {
            m_filtersDirty.store(true, std::memory_order_release);
        }
    }

    int32_t getSoloBandSlot() const {
        return m_soloBand.load(std::memory_order_acquire);
    }

    bool isBandSoloed(uint32_t slot) const {
        return slot < kMaxDynamicBands && getSoloBandSlot() == static_cast<int32_t>(slot);
    }

    bool clearDynamicBandSlot(uint32_t slot) {
        if (slot < kLegacyBandCount || slot >= kMaxDynamicBands) {
            return false;
        }

        m_dynamicBandSlots[slot] = dynamicBandSlotDefaults(slot);
        const auto defaults = m_dynamicBandSlots[slot];
        m_dynamicBandTypes[slot].store(dynamicFilterTypeToParam(defaults.type), std::memory_order_relaxed);
        m_dynamicBandStereoModes[slot].store(stereoModeToParam(defaults.stereoMode), std::memory_order_relaxed);
        m_dynamicBandFreqs[slot].store(defaults.frequencyNorm, std::memory_order_relaxed);
        m_dynamicBandGains[slot].store(defaults.gainNorm, std::memory_order_relaxed);
        m_dynamicBandQs[slot].store(defaults.qOrSlopeNorm, std::memory_order_relaxed);
        m_dynamicBandDynamicEnabled[slot].store(defaults.dynamicEnabled, std::memory_order_relaxed);
        m_dynamicBandTargetGains[slot].store(defaults.targetGainNorm, std::memory_order_relaxed);
        m_dynamicBandThresholds[slot].store(defaults.thresholdNorm, std::memory_order_relaxed);
        m_dynamicBandKnees[slot].store(defaults.kneeNorm, std::memory_order_relaxed);
        m_dynamicBandAttacks[slot].store(defaults.attackNorm, std::memory_order_relaxed);
        m_dynamicBandReleases[slot].store(defaults.releaseNorm, std::memory_order_relaxed);
        m_dynamicBandSidechainLinked[slot].store(defaults.sidechainLinked, std::memory_order_relaxed);
        m_dynamicBandSidechainTypes[slot].store(dynamicFilterTypeToParam(defaults.sidechainType),
                                                std::memory_order_relaxed);
        m_dynamicBandSidechainFreqs[slot].store(defaults.sidechainFrequencyNorm, std::memory_order_relaxed);
        m_dynamicBandSidechainQs[slot].store(defaults.sidechainQNorm, std::memory_order_relaxed);
        m_dynamicEnvelopeMeters[slot].store(0.0f, std::memory_order_relaxed);
        snapDynamicBandSmoothing(slot);
        m_bandEnabled[slot].store(false, std::memory_order_relaxed);
        m_bandStages[slot].store(0u, std::memory_order_relaxed);
        if (m_soloBand.load(std::memory_order_acquire) == static_cast<int32_t>(slot)) {
            m_soloBand.store(-1, std::memory_order_release);
        }
        m_filtersDirty.store(true, std::memory_order_release);
        return true;
    }

    bool isDynamicBandSlotEnabled(uint32_t slot) const {
        return slot < kMaxDynamicBands && m_bandEnabled[slot].load(std::memory_order_relaxed);
    }

    uint32_t getDynamicBandSlotStageCount(uint32_t slot) const {
        if (slot >= kMaxDynamicBands) return 0;
        return m_bandStages[slot].load(std::memory_order_relaxed);
    }

    static bool isLegacyDynamicBandSlot(uint32_t slot) {
        return slot < kLegacyBandCount;
    }

    bool isDynamicBandSlotAllocatable(uint32_t slot) const {
        return slot >= kLegacyBandCount && slot < kMaxDynamicBands && !isDynamicBandSlotEnabled(slot);
    }

    int32_t findNextAvailableDynamicBandSlot(uint32_t searchAfterSlot = kLegacyBandCount - 1) const {
        if (kMaxDynamicBands <= kLegacyBandCount) {
            return -1;
        }

        constexpr uint32_t firstDynamicSlot = kLegacyBandCount;
        constexpr uint32_t dynamicSlotCount = kMaxDynamicBands - kLegacyBandCount;
        uint32_t start = searchAfterSlot + 1u;
        if (start < firstDynamicSlot || start >= kMaxDynamicBands) {
            start = firstDynamicSlot;
        }

        for (uint32_t offset = 0; offset < dynamicSlotCount; ++offset) {
            const uint32_t slot = firstDynamicSlot + ((start - firstDynamicSlot + offset) % dynamicSlotCount);
            if (isDynamicBandSlotAllocatable(slot)) {
                return static_cast<int32_t>(slot);
            }
        }
        return -1;
    }

    int32_t createDynamicBandSlot(const DynamicBandSlotDefaults& defaults,
                                  uint32_t searchAfterSlot = kLegacyBandCount - 1) {
        const int32_t slot = findNextAvailableDynamicBandSlot(searchAfterSlot);
        if (slot < 0) {
            return -1;
        }
        if (!setDynamicBandSlot(static_cast<uint32_t>(slot), defaults)) {
            return -1;
        }
        return slot;
    }

    int32_t createDynamicBandAtGraphPoint(float frequencyNorm,
                                          float gainNorm,
                                          uint32_t searchAfterSlot = kLegacyBandCount - 1) {
        return createDynamicBandSlot(dynamicBandGraphCreationDefaults(frequencyNorm, gainNorm), searchAfterSlot);
    }

    void resetToEmptyState() {
        for (uint32_t i = 0; i < kParamCount; ++i) {
            setParameter(i, defaultParameterValue(i));
        }
        for (uint32_t band = 0; band < kLegacyBandCount; ++band) {
            setParameter(legacyBandSlot(band).enableId, 0.0f);
        }
        resetDynamicBandSlots();
        m_soloBand.store(-1, std::memory_order_release);
        m_filtersDirty.store(true, std::memory_order_release);
    }

private:
    // ---- Safety ----
    static float sanitizeSample(float sample) {
        if (!std::isfinite(sample)) return 0.0f;
        if (std::abs(sample) < 1.0e-24f) return 0.0f;
        return std::clamp(sample, -16.0f, 16.0f);
    }

    static float sanitizePassThroughSample(float sample) {
        if (!std::isfinite(sample)) return 0.0f;
        if (std::abs(sample) < 1.0e-24f) return 0.0f;
        return sample;
    }

    static void copyOrSilence(const float* const* inputs, float** outputs,
                               uint32_t numInputChannels, uint32_t numOutputChannels,
                               uint32_t numFrames) {
        if (!outputs) return;
        for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
            if (outputs[ch] && inputs && ch < numInputChannels && inputs[ch]) {
                for (uint32_t i = 0; i < numFrames; ++i) {
                    outputs[ch][i] = sanitizePassThroughSample(inputs[ch][i]);
                }
            } else if (outputs[ch]) {
                std::memset(outputs[ch], 0, numFrames * sizeof(float));
            }
        }
    }

    // ---- Default initialization ----
    void initDefaults() {
        for (uint32_t i = 0; i < kParamCount; ++i) {
            m_params[i].store(defaultParameterValue(i), std::memory_order_relaxed);
        }

        for (auto& s : m_smoothed) s.store(0.0f, std::memory_order_relaxed);
        for (uint32_t slot = 0; slot < kMaxDynamicBands; ++slot) {
            m_dynamicBandSlots[slot] = dynamicBandSlotDefaults(slot);
            m_dynamicBandTypes[slot].store(dynamicFilterTypeToParam(m_dynamicBandSlots[slot].type),
                                           std::memory_order_relaxed);
            m_dynamicBandStereoModes[slot].store(stereoModeToParam(m_dynamicBandSlots[slot].stereoMode),
                                                 std::memory_order_relaxed);
            m_dynamicBandFreqs[slot].store(m_dynamicBandSlots[slot].frequencyNorm, std::memory_order_relaxed);
            m_dynamicBandGains[slot].store(m_dynamicBandSlots[slot].gainNorm, std::memory_order_relaxed);
            m_dynamicBandQs[slot].store(m_dynamicBandSlots[slot].qOrSlopeNorm, std::memory_order_relaxed);
            m_dynamicBandDynamicEnabled[slot].store(m_dynamicBandSlots[slot].dynamicEnabled,
                                                    std::memory_order_relaxed);
            m_dynamicBandTargetGains[slot].store(m_dynamicBandSlots[slot].targetGainNorm,
                                                 std::memory_order_relaxed);
            m_dynamicBandThresholds[slot].store(m_dynamicBandSlots[slot].thresholdNorm,
                                                std::memory_order_relaxed);
            m_dynamicBandKnees[slot].store(m_dynamicBandSlots[slot].kneeNorm, std::memory_order_relaxed);
            m_dynamicBandAttacks[slot].store(m_dynamicBandSlots[slot].attackNorm, std::memory_order_relaxed);
            m_dynamicBandReleases[slot].store(m_dynamicBandSlots[slot].releaseNorm, std::memory_order_relaxed);
            m_dynamicBandSidechainLinked[slot].store(m_dynamicBandSlots[slot].sidechainLinked,
                                                     std::memory_order_relaxed);
            m_dynamicBandSidechainTypes[slot].store(dynamicFilterTypeToParam(m_dynamicBandSlots[slot].sidechainType),
                                                    std::memory_order_relaxed);
            m_dynamicBandSidechainFreqs[slot].store(m_dynamicBandSlots[slot].sidechainFrequencyNorm,
                                                    std::memory_order_relaxed);
            m_dynamicBandSidechainQs[slot].store(m_dynamicBandSlots[slot].sidechainQNorm,
                                                 std::memory_order_relaxed);
            snapDynamicBandSmoothing(slot);
            m_dynamicEnvelopeAmounts[slot] = 0.0f;
            m_dynamicEnvelopeMeters[slot].store(0.0f, std::memory_order_relaxed);
            m_bandEnabled[slot].store(slot < kLegacyBandCount && m_dynamicBandSlots[slot].enabled,
                                      std::memory_order_relaxed);
            m_bandStages[slot].store(0u, std::memory_order_relaxed);
            m_runtimeBandEnabled[slot] = false;
            m_runtimeBandTypes[slot] = m_dynamicBandSlots[slot].type;
            m_runtimeBandModes[slot] = m_dynamicBandSlots[slot].stereoMode;
            m_runtimeBandStages[slot] = 0u;
        }
    }

    void computeSmoothingCoeff() {
        const float coeff = 1.0f - std::exp(-1.0f / std::max(1.0f, static_cast<float>(m_sampleRate) * 0.005f));
        m_smoothingCoeff.store(coeff, std::memory_order_relaxed);
    }

    // ---- Band type/enable mapping ----
    static bool bandUsesSlope(uint32_t band) {
        return legacyBandSlot(band).usesSlope;
    }

    static FilterType defaultBandType(uint32_t band) {
        return legacyBandSlot(band).defaultType;
    }

    static FilterType typeParamToFilterType(float norm) {
        const uint32_t idx = static_cast<uint32_t>(std::round(std::clamp(norm, 0.0f, 1.0f) * 3.0f));
        static constexpr FilterType types[] = {
            FilterType::Bell,
            FilterType::Notch,
            FilterType::BandPass,
            FilterType::Tilt
        };
        return types[std::min(idx, 3u)];
    }

    static float filterTypeToTypeParam(FilterType type) {
        switch (type) {
        case FilterType::Bell: return 0.0f;
        case FilterType::Notch: return 1.0f / 3.0f;
        case FilterType::BandPass: return 2.0f / 3.0f;
        case FilterType::Tilt: return 1.0f;
        default: return 0.0f;
        }
    }

    static FilterType dynamicTypeParamToFilterType(float norm) {
        const uint32_t idx = static_cast<uint32_t>(std::round(std::clamp(norm, 0.0f, 1.0f) * 7.0f));
        static constexpr FilterType types[] = {
            FilterType::LowCut,
            FilterType::LowShelf,
            FilterType::Bell,
            FilterType::Notch,
            FilterType::BandPass,
            FilterType::Tilt,
            FilterType::HighShelf,
            FilterType::HighCut,
        };
        return types[std::min(idx, 7u)];
    }

    static float dynamicFilterTypeToParam(FilterType type) {
        switch (type) {
        case FilterType::LowCut: return 0.0f;
        case FilterType::LowShelf: return 1.0f / 7.0f;
        case FilterType::Bell: return 2.0f / 7.0f;
        case FilterType::Notch: return 3.0f / 7.0f;
        case FilterType::BandPass: return 4.0f / 7.0f;
        case FilterType::Tilt: return 5.0f / 7.0f;
        case FilterType::HighShelf: return 6.0f / 7.0f;
        case FilterType::HighCut: return 1.0f;
        default: return 2.0f / 7.0f;
        }
    }

    static const char* filterTypeLabel(FilterType type) {
        switch (type) {
        case FilterType::Bell: return "Bell";
        case FilterType::Notch: return "Notch";
        case FilterType::BandPass: return "Band Pass";
        case FilterType::Tilt: return "Tilt";
        case FilterType::LowCut: return "High-Pass";
        case FilterType::HighCut: return "Low-Pass";
        case FilterType::LowShelf: return "Low Shelf";
        case FilterType::HighShelf: return "High Shelf";
        default: return "";
        }
    }

    static StereoMode stereoModeFromParam(float norm) {
        const uint32_t idx = static_cast<uint32_t>(std::round(std::clamp(norm, 0.0f, 1.0f) * 4.0f));
        static constexpr StereoMode modes[] = {
            StereoMode::Stereo,
            StereoMode::Left,
            StereoMode::Right,
            StereoMode::Mid,
            StereoMode::Side
        };
        return modes[std::min(idx, 4u)];
    }

    static float stereoModeToParam(StereoMode mode) {
        switch (mode) {
        case StereoMode::Stereo: return 0.0f;
        case StereoMode::Left: return 0.25f;
        case StereoMode::Right: return 0.5f;
        case StereoMode::Mid: return 0.75f;
        case StereoMode::Side: return 1.0f;
        default: return 0.0f;
        }
    }

    static const char* stereoModeLabel(StereoMode mode) {
        switch (mode) {
        case StereoMode::Stereo: return "Stereo";
        case StereoMode::Left: return "Left";
        case StereoMode::Right: return "Right";
        case StereoMode::Mid: return "Mid";
        case StereoMode::Side: return "Side";
        default: return "Stereo";
        }
    }

    static bool filterTypeUsesGain(FilterType type) {
        return type == FilterType::Bell ||
               type == FilterType::LowShelf ||
               type == FilterType::HighShelf ||
               type == FilterType::Tilt;
    }

    static bool bandUsesGain(uint32_t band) {
        return filterTypeUsesGain(defaultBandType(band));
    }

    static uint32_t bandV1EnableId(uint32_t band) {
        return legacyBandSlot(band).enableId;
    }
    static uint32_t bandV1FreqId(uint32_t band) {
        return legacyBandSlot(band).freqId;
    }
    static uint32_t bandV1GainId(uint32_t band) {
        return legacyBandSlot(band).gainId;
    }
    static uint32_t bandV1QId(uint32_t band) {
        return legacyBandSlot(band).qId;
    }
    static uint32_t bandV1TypeId(uint32_t band) {
        const auto slot = legacyBandSlot(band);
        return slot.typeId != 0 ? slot.typeId : slot.enableId;
    }
    static uint32_t bandV1StereoModeId(uint32_t band) {
        return legacyBandSlot(band).stereoModeId;
    }

    static uint32_t filterStageBase(uint32_t channelSlot, uint32_t band) {
        return (channelSlot * kMaxDynamicBands + band) * kMaxFilterStages;
    }

    static FilterType bandFilterType(uint32_t band, float typeNorm) {
        if (band == 2 || band == 3) {
            return typeParamToFilterType(typeNorm);
        }
        return defaultBandType(band);
    }

    DynamicBandSlotDefaults dynamicBandSlotSnapshotFromAtomics(uint32_t slot) const {
        if (slot < kLegacyBandCount || slot >= kMaxDynamicBands) {
            return dynamicBandSlotDefaults(slot);
        }
        auto defaults = m_dynamicBandSlots[slot];
        const FilterType type =
            dynamicTypeParamToFilterType(m_dynamicBandTypes[slot].load(std::memory_order_relaxed));
        defaults.enabled = m_bandEnabled[slot].load(std::memory_order_relaxed);
        defaults.type = type;
        defaults.stereoMode = stereoModeFromParam(m_dynamicBandStereoModes[slot].load(std::memory_order_relaxed));
        defaults.frequencyNorm = m_dynamicBandFreqs[slot].load(std::memory_order_relaxed);
        defaults.gainNorm = m_dynamicBandGains[slot].load(std::memory_order_relaxed);
        defaults.qOrSlopeNorm = m_dynamicBandQs[slot].load(std::memory_order_relaxed);
        defaults.dynamicEnabled = m_dynamicBandDynamicEnabled[slot].load(std::memory_order_relaxed);
        defaults.targetGainNorm = m_dynamicBandTargetGains[slot].load(std::memory_order_relaxed);
        defaults.thresholdNorm = m_dynamicBandThresholds[slot].load(std::memory_order_relaxed);
        defaults.kneeNorm = m_dynamicBandKnees[slot].load(std::memory_order_relaxed);
        defaults.attackNorm = m_dynamicBandAttacks[slot].load(std::memory_order_relaxed);
        defaults.releaseNorm = m_dynamicBandReleases[slot].load(std::memory_order_relaxed);
        defaults.sidechainLinked = m_dynamicBandSidechainLinked[slot].load(std::memory_order_relaxed);
        defaults.sidechainType =
            dynamicTypeParamToFilterType(m_dynamicBandSidechainTypes[slot].load(std::memory_order_relaxed));
        defaults.sidechainFrequencyNorm = m_dynamicBandSidechainFreqs[slot].load(std::memory_order_relaxed);
        defaults.sidechainQNorm = m_dynamicBandSidechainQs[slot].load(std::memory_order_relaxed);
        defaults.usesSlope = type == FilterType::LowCut || type == FilterType::HighCut;
        if (defaults.sidechainLinked) {
            defaults.sidechainFrequencyNorm = defaults.frequencyNorm;
            defaults.sidechainQNorm = defaults.qOrSlopeNorm;
        }
        return defaults;
    }

    FilterType slotFilterType(uint32_t slot) const {
        if (slot < kLegacyBandCount) {
            return bandFilterType(slot, m_smoothed[bandV1TypeId(slot)].load(std::memory_order_relaxed));
        }
        return dynamicTypeParamToFilterType(m_dynamicBandTypes[slot].load(std::memory_order_relaxed));
    }

    StereoMode slotStereoMode(uint32_t slot) const {
        if (slot < kLegacyBandCount) {
            return stereoModeFromParam(m_smoothed[bandV1StereoModeId(slot)].load(std::memory_order_relaxed));
        }
        return stereoModeFromParam(m_dynamicBandStereoModes[slot].load(std::memory_order_relaxed));
    }

    float slotFrequencyNorm(uint32_t slot) const {
        if (slot < kLegacyBandCount) {
            return m_smoothed[bandV1FreqId(slot)].load(std::memory_order_relaxed);
        }
        return m_dynamicBandFreqs[slot].load(std::memory_order_relaxed);
    }

    float slotGainNorm(uint32_t slot) const {
        if (slot < kLegacyBandCount) {
            const FilterType type = slotFilterType(slot);
            return filterTypeUsesGain(type) ? m_smoothed[bandV1GainId(slot)].load(std::memory_order_relaxed) : 0.5f;
        }
        return m_dynamicBandGains[slot].load(std::memory_order_relaxed);
    }

    float slotQNorm(uint32_t slot) const {
        if (slot < kLegacyBandCount) {
            return m_smoothed[bandV1QId(slot)].load(std::memory_order_relaxed);
        }
        return m_dynamicBandQs[slot].load(std::memory_order_relaxed);
    }

    float slotFrequencyNormForProcessing(uint32_t slot) const {
        if (slot < kLegacyBandCount) {
            return slotFrequencyNorm(slot);
        }
        return m_smoothedDynamicBandFreqs[slot].load(std::memory_order_relaxed);
    }

    float slotGainNormForProcessing(uint32_t slot) const {
        if (slot < kLegacyBandCount) {
            return slotGainNorm(slot);
        }
        return m_smoothedDynamicBandGains[slot].load(std::memory_order_relaxed);
    }

    float slotQNormForProcessing(uint32_t slot) const {
        if (slot < kLegacyBandCount) {
            return slotQNorm(slot);
        }
        return m_smoothedDynamicBandQs[slot].load(std::memory_order_relaxed);
    }

    void snapDynamicBandSmoothing(uint32_t slot) {
        if (slot >= kMaxDynamicBands) return;
        m_smoothedDynamicBandFreqs[slot].store(m_dynamicBandFreqs[slot].load(std::memory_order_relaxed),
                                               std::memory_order_relaxed);
        m_smoothedDynamicBandGains[slot].store(m_dynamicBandGains[slot].load(std::memory_order_relaxed),
                                               std::memory_order_relaxed);
        m_smoothedDynamicBandQs[slot].store(m_dynamicBandQs[slot].load(std::memory_order_relaxed),
                                            std::memory_order_relaxed);
    }

    void snapDynamicBandSmoothing() {
        for (uint32_t slot = 0; slot < kMaxDynamicBands; ++slot) {
            snapDynamicBandSmoothing(slot);
        }
    }

    bool slotUsesSlope(uint32_t slot) const {
        if (slot < kLegacyBandCount) {
            return bandUsesSlope(slot);
        }
        const FilterType type = slotFilterType(slot);
        return type == FilterType::LowCut || type == FilterType::HighCut;
    }

    void snapBandTypes() {
        for (uint32_t i = 0; i < kV1BandCount; ++i) {
            m_bandEnabled[i].store(getParameter(bandV1EnableId(i)) > 0.5f, std::memory_order_relaxed);
        }
    }

    void resetDynamicBandSlots() {
        for (uint32_t i = kV1BandCount; i < kMaxDynamicBands; ++i) {
            m_dynamicBandSlots[i] = dynamicBandSlotDefaults(i);
            m_dynamicBandTypes[i].store(dynamicFilterTypeToParam(m_dynamicBandSlots[i].type), std::memory_order_relaxed);
            m_dynamicBandStereoModes[i].store(stereoModeToParam(m_dynamicBandSlots[i].stereoMode),
                                              std::memory_order_relaxed);
            m_dynamicBandFreqs[i].store(m_dynamicBandSlots[i].frequencyNorm, std::memory_order_relaxed);
            m_dynamicBandGains[i].store(m_dynamicBandSlots[i].gainNorm, std::memory_order_relaxed);
            m_dynamicBandQs[i].store(m_dynamicBandSlots[i].qOrSlopeNorm, std::memory_order_relaxed);
            m_dynamicBandDynamicEnabled[i].store(m_dynamicBandSlots[i].dynamicEnabled, std::memory_order_relaxed);
            m_dynamicBandTargetGains[i].store(m_dynamicBandSlots[i].targetGainNorm, std::memory_order_relaxed);
            m_dynamicBandThresholds[i].store(m_dynamicBandSlots[i].thresholdNorm, std::memory_order_relaxed);
            m_dynamicBandKnees[i].store(m_dynamicBandSlots[i].kneeNorm, std::memory_order_relaxed);
            m_dynamicBandAttacks[i].store(m_dynamicBandSlots[i].attackNorm, std::memory_order_relaxed);
            m_dynamicBandReleases[i].store(m_dynamicBandSlots[i].releaseNorm, std::memory_order_relaxed);
            m_dynamicBandSidechainLinked[i].store(m_dynamicBandSlots[i].sidechainLinked, std::memory_order_relaxed);
            m_dynamicBandSidechainTypes[i].store(dynamicFilterTypeToParam(m_dynamicBandSlots[i].sidechainType),
                                                 std::memory_order_relaxed);
            m_dynamicBandSidechainFreqs[i].store(m_dynamicBandSlots[i].sidechainFrequencyNorm,
                                                 std::memory_order_relaxed);
            m_dynamicBandSidechainQs[i].store(m_dynamicBandSlots[i].sidechainQNorm, std::memory_order_relaxed);
            snapDynamicBandSmoothing(i);
            m_bandEnabled[i].store(false, std::memory_order_relaxed);
            m_bandStages[i].store(0u, std::memory_order_relaxed);
            m_dynamicEnvelopeAmounts[i] = 0.0f;
            m_dynamicEnvelopeMeters[i].store(0.0f, std::memory_order_relaxed);
            m_runtimeBandEnabled[i] = false;
            m_runtimeBandTypes[i] = FilterType::Bell;
            m_runtimeBandModes[i] = StereoMode::Stereo;
            m_runtimeBandSoloAudition[i] = false;
            m_runtimeBandStages[i] = 0u;
        }
        const int32_t solo = m_soloBand.load(std::memory_order_acquire);
        if (solo >= static_cast<int32_t>(kV1BandCount)) {
            m_soloBand.store(-1, std::memory_order_release);
        }
    }

    void markDirtyForParam(uint32_t id) {
        if (id == kParamBypass) return;
        if (parameterAffectsFilters(id)) {
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
        if (id == kParamBell1Type) return 2;
        if (id == kParamBell2Type) return 3;
        if (id >= kParamHPFStereoMode && id <= kParamLPFStereoMode) return id - kParamHPFStereoMode;
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

    static float dynamicBandFreqToHz(float norm) {
        return 20.0f * std::pow(1000.0f, std::clamp(norm, 0.0f, 1.0f)); // 20–20000 Hz
    }

    static float gainToDb(float norm) {
        return -18.0f + std::clamp(norm, 0.0f, 1.0f) * 36.0f;
    }

    static float outputGainToLinear(float norm) {
        return std::pow(10.0f, gainToDb(norm) / 20.0f);
    }

    static float qToLinear(float norm) {
        return 0.1f + std::clamp(norm, 0.0f, 1.0f) * 9.9f;
    }

    static uint32_t slopeDbPerOct(float norm) {
        static constexpr uint32_t slopes[] = {6, 12, 24, 36, 48, 72, 96};
        const float clamped = std::clamp(norm, 0.0f, 1.0f);
        const uint32_t idx = static_cast<uint32_t>(std::round(clamped * 6.0f));
        return slopes[std::min(idx, 6u)];
    }

    static uint32_t slopeStageCount(float norm) {
        const uint32_t db = slopeDbPerOct(norm);
        return db == 6 ? 1u : db / 12u;
    }

    static bool slopeUsesFirstOrderStage(float norm) {
        return slopeDbPerOct(norm) == 6;
    }

    static FilterCoeffs stageCoeffsForFilter(FilterType type,
                                             float frequency,
                                             float gainDb,
                                             float q,
                                             float sampleRate,
                                             uint32_t stage,
                                             bool firstOrderCut) {
        if (firstOrderCut) {
            return designFirstOrderCut(type, frequency, sampleRate);
        }
        if (type == FilterType::Tilt) {
            const FilterType shelfType = stage == 0 ? FilterType::LowShelf : FilterType::HighShelf;
            const float shelfGainDb = stage == 0 ? -gainDb * 0.5f : gainDb * 0.5f;
            return designBiquad(shelfType, frequency, shelfGainDb, q, sampleRate);
        }
        return designBiquad(type, frequency, gainDb, q, sampleRate);
    }

    static float butterworthStageQ(uint32_t stage, uint32_t stages) {
        if (stages <= 1) return 0.70710678f;
        constexpr double pi = 3.14159265358979323846;
        const uint32_t clampedStage = std::min(stage, stages - 1);
        const double order = static_cast<double>(stages) * 2.0;
        const double angle = (2.0 * static_cast<double>(clampedStage) + 1.0) * pi / (2.0 * order);
        const double q = 1.0 / (2.0 * std::cos(angle));
        return static_cast<float>(std::clamp(q, 0.1, 10.0));
    }

    float stageQForBand(uint32_t band, uint32_t stage, uint32_t stages, float q) const {
        if (slotUsesSlope(band) && stages > 1) {
            return butterworthStageQ(stage, stages);
        }
        return q;
    }

    static float legacySlopeNormToExtended(float norm) {
        const float clamped = std::clamp(norm, 0.0f, 1.0f);
        const uint32_t oldIdx = static_cast<uint32_t>(std::round(clamped * 3.0f));
        const uint32_t extendedIdx = std::min(oldIdx, 3u) + 1u;
        return static_cast<float>(extendedIdx) / 6.0f;
    }

    static bool parameterAffectsFilters(uint32_t id) {
        return id <= kParamLPFSlope || id == kParamBell1Type || id == kParamBell2Type ||
               (id >= kParamHPFStereoMode && id <= kParamLPFStereoMode);
    }

    void defaultStereoPlacementParams() {
        for (uint32_t band = 0; band < kV1BandCount; ++band) {
            setParameter(bandV1StereoModeId(band), 0.0f);
        }
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

    float slotFrequencyHz(uint32_t slot, float norm) const {
        if (slot < kLegacyBandCount) {
            return bandFrequencyHz(slot, norm);
        }
        return dynamicBandFreqToHz(norm);
    }

    float bandQValue(uint32_t band, float norm) const {
        if (bandUsesSlope(band)) return 0.70710678f; // Butterworth
        return qToLinear(norm);
    }

    float slotQValue(uint32_t slot, float norm) const {
        if (slotUsesSlope(slot)) return 0.70710678f;
        return qToLinear(norm);
    }

    uint32_t bandStageCount(uint32_t band, FilterType type, float slopeNorm) const {
        if (type == FilterType::Tilt) return 2;
        if (!bandUsesSlope(band)) return 1;
        return slopeStageCount(slopeNorm);
    }

    uint32_t slotStageCount(uint32_t slot, FilterType type, float slopeNorm) const {
        if (type == FilterType::Tilt) return 2;
        if (!slotUsesSlope(slot)) return 1;
        return slopeStageCount(slopeNorm);
    }

    static double biquadMagnitudeDb(const FilterCoeffs& c, double omega) {
        const double c1 = std::cos(omega);
        const double s1 = std::sin(omega);
        const double c2 = std::cos(2.0 * omega);
        const double s2 = std::sin(2.0 * omega);
        const double nr = c.b0 + c.b1 * c1 + c.b2 * c2;
        const double ni = -c.b1 * s1 - c.b2 * s2;
        const double dr = c.a0 + c.a1 * c1 + c.a2 * c2;
        const double di = -c.a1 * s1 - c.a2 * s2;
        const double nm = std::hypot(nr, ni);
        const double dm = std::max(std::hypot(dr, di), 1.0e-24);
        return 20.0 * std::log10(std::max(nm / dm, 1.0e-24));
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
        if (std::abs(db) < 0.05f) {
            db = 0.0f;
        }
        char buf[16];
        if (db == 0.0f) {
            std::snprintf(buf, sizeof(buf), "0.0dB");
        } else if (db > 0.0f) {
            std::snprintf(buf, sizeof(buf), "+%.1fdB", db);
        } else {
            std::snprintf(buf, sizeof(buf), "%.1fdB", db);
        }
        return buf;
    }

    static std::string formatQ(float q) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", q);
        return buf;
    }

    static float dynamicThresholdDb(float norm) {
        return -72.0f + std::clamp(norm, 0.0f, 1.0f) * 72.0f;
    }

    static float dynamicKneeDb(float norm) {
        return 0.5f + std::clamp(norm, 0.0f, 1.0f) * 23.5f;
    }

    static float dynamicTimeMs(float norm, float minMs, float maxMs) {
        const float clamped = std::clamp(norm, 0.0f, 1.0f);
        return minMs * std::pow(maxMs / minMs, clamped);
    }

    static float dynamicDetectorAmount(float levelDb, float thresholdDb, float kneeDb) {
        const float halfKnee = std::max(kneeDb * 0.5f, 0.0f);
        if (halfKnee <= 0.0f) {
            return levelDb > thresholdDb ? 1.0f : 0.0f;
        }
        if (levelDb <= thresholdDb - halfKnee) return 0.0f;
        if (levelDb >= thresholdDb + halfKnee) return 1.0f;
        const float x = (levelDb - (thresholdDb - halfKnee)) / std::max(kneeDb, 1.0e-6f);
        return x * x * (3.0f - 2.0f * x);
    }

    static FilterType detectorFilterTypeFor(FilterType type) {
        switch (type) {
        case FilterType::LowShelf:
        case FilterType::HighCut:
            return FilterType::HighCut;
        case FilterType::HighShelf:
        case FilterType::LowCut:
            return FilterType::LowCut;
        case FilterType::Bell:
        case FilterType::Notch:
        case FilterType::BandPass:
        case FilterType::Tilt:
        default:
            return FilterType::BandPass;
        }
    }

    void setDynamicDetectorCoefficients(uint32_t band,
                                        FilterType detectorType,
                                        float frequencyNorm,
                                        float qNorm,
                                        bool enabled) {
        if (band >= kMaxDynamicBands) return;
        if (!enabled) {
            for (uint32_t ch = 0; ch < 2; ++ch) {
                m_dynamicDetectorFilters[ch * kMaxDynamicBands + band].setCoeffs(1.0f, 0.0f, 0.0f, 1.0f,
                                                                                  0.0f, 0.0f);
                m_dynamicDetectorFilters[ch * kMaxDynamicBands + band].reset();
            }
            return;
        }

        float freq = slotFrequencyHz(band, frequencyNorm);
        freq = std::clamp(freq, 20.0f, static_cast<float>(m_sampleRate) * 0.49f);
        const float q = std::clamp(qToLinear(qNorm), 0.1f, 10.0f);
        auto coeffs = designBiquad(detectorType, freq, 0.0f, q, static_cast<float>(m_sampleRate));
        if (!std::isfinite(coeffs.b0) || !std::isfinite(coeffs.a0) || std::abs(coeffs.a0) < 1.0e-12f) {
            coeffs = {1, 0, 0, 1, 0, 0};
        }
        for (uint32_t ch = 0; ch < 2; ++ch) {
            m_dynamicDetectorFilters[ch * kMaxDynamicBands + band].setCoeffs(coeffs.b0, coeffs.b1, coeffs.b2,
                                                                              coeffs.a0, coeffs.a1, coeffs.a2);
        }
    }

    float processDetectorSample(uint32_t band, uint32_t channelSlot, float sample) {
        if (band >= kMaxDynamicBands || channelSlot >= 2) return 0.0f;
        return sanitizeSample(m_dynamicDetectorFilters[channelSlot * kMaxDynamicBands + band].process(sample));
    }

    float measureDynamicDetectorLevelDb(uint32_t band,
                                        float** outputs,
                                        const std::array<bool, 2>& channelHasInput,
                                        uint32_t channels,
                                        uint32_t sampleOffset,
                                        uint32_t blockFrames) {
        if (!outputs || blockFrames == 0 || channels == 0) {
            return -120.0f;
        }

        const StereoMode mode = slotStereoMode(band);
        double sumSquares = 0.0;
        uint32_t sampleCount = 0;
        switch (mode) {
        case StereoMode::Left:
            if (channelHasInput[0] && outputs[0]) {
                for (uint32_t i = 0; i < blockFrames; ++i) {
                    const float sample = processDetectorSample(band, 0, sanitizeSample(outputs[0][sampleOffset + i]));
                    sumSquares += static_cast<double>(sample) * sample;
                }
                sampleCount += blockFrames;
            }
            break;
        case StereoMode::Right:
            if (channels > 1 && channelHasInput[1] && outputs[1]) {
                for (uint32_t i = 0; i < blockFrames; ++i) {
                    const float sample = processDetectorSample(band, 1, sanitizeSample(outputs[1][sampleOffset + i]));
                    sumSquares += static_cast<double>(sample) * sample;
                }
                sampleCount += blockFrames;
            }
            break;
        case StereoMode::Mid:
        case StereoMode::Side:
            if (channels > 1 && channelHasInput[0] && channelHasInput[1] && outputs[0] && outputs[1]) {
                const float sign = mode == StereoMode::Mid ? 1.0f : -1.0f;
                for (uint32_t i = 0; i < blockFrames; ++i) {
                    const uint32_t frame = sampleOffset + i;
                    const float encoded = 0.5f * (sanitizeSample(outputs[0][frame]) +
                                                  sign * sanitizeSample(outputs[1][frame]));
                    const float sample = processDetectorSample(band, mode == StereoMode::Mid ? 0u : 1u, encoded);
                    sumSquares += static_cast<double>(sample) * sample;
                }
                sampleCount += blockFrames;
            }
            break;
        case StereoMode::Stereo:
        default:
            for (uint32_t ch = 0; ch < channels; ++ch) {
                if (!channelHasInput[ch] || !outputs[ch]) continue;
                for (uint32_t i = 0; i < blockFrames; ++i) {
                    const float sample =
                        processDetectorSample(band, ch, sanitizeSample(outputs[ch][sampleOffset + i]));
                    sumSquares += static_cast<double>(sample) * sample;
                }
                sampleCount += blockFrames;
            }
            break;
        }

        if (sampleCount == 0) {
            return -120.0f;
        }
        const double rms = std::sqrt(sumSquares / static_cast<double>(sampleCount));
        return static_cast<float>(20.0 * std::log10(std::max(rms, 1.0e-8)));
    }

    bool updateDynamicEnvelopes(float** outputs,
                                const std::array<bool, 2>& channelHasInput,
                                uint32_t channels,
                                uint32_t sampleOffset,
                                uint32_t blockFrames,
                                int32_t soloBand) {
        bool changed = false;
        for (uint32_t band = kLegacyBandCount; band < kMaxDynamicBands; ++band) {
            const bool active = m_bandEnabled[band].load(std::memory_order_relaxed) &&
                                m_dynamicBandDynamicEnabled[band].load(std::memory_order_relaxed) &&
                                filterTypeUsesGain(slotFilterType(band)) &&
                                (soloBand < 0 || band == static_cast<uint32_t>(soloBand));
            float targetAmount = 0.0f;
            if (active) {
                const float levelDb =
                    measureDynamicDetectorLevelDb(band, outputs, channelHasInput, channels, sampleOffset, blockFrames);
                targetAmount = dynamicDetectorAmount(levelDb,
                                                     dynamicThresholdDb(m_dynamicBandThresholds[band].load(
                                                         std::memory_order_relaxed)),
                                                     dynamicKneeDb(m_dynamicBandKnees[band].load(
                                                         std::memory_order_relaxed)));
            }

            const float current = m_dynamicEnvelopeAmounts[band];
            const float norm = targetAmount > current
                                   ? m_dynamicBandAttacks[band].load(std::memory_order_relaxed)
                                   : m_dynamicBandReleases[band].load(std::memory_order_relaxed);
            const float timeMs =
                targetAmount > current ? dynamicTimeMs(norm, 1.0f, 120.0f) : dynamicTimeMs(norm, 10.0f, 1200.0f);
            const float coeff = 1.0f - std::exp(-static_cast<float>(blockFrames) /
                                                std::max(static_cast<float>(m_sampleRate) * timeMs * 0.001f, 1.0f));
            const float next = std::clamp(current + (targetAmount - current) * coeff, 0.0f, 1.0f);
            m_dynamicEnvelopeAmounts[band] = next;
            m_dynamicEnvelopeMeters[band].store(next, std::memory_order_relaxed);
            changed = changed || std::abs(next - current) > 1.0e-4f;
        }
        return changed;
    }

    // ---- Smoothing ----
    // Combines smoothing and pending-change check into a single pass.
    bool smoothParametersAndCheckPending(float coeff) {
        bool changed = false;
        for (uint32_t i = 0; i < kParamCount; ++i) {
            if (i == kParamBypass) continue;
            const float target = m_params[i].load(std::memory_order_relaxed);
            if (i == kParamBell1Type || i == kParamBell2Type || i == kParamPolarityInvert ||
                (i >= kParamHPFStereoMode && i <= kParamLPFStereoMode)) {
                const float current = m_smoothed[i].load(std::memory_order_relaxed);
                m_smoothed[i].store(target, std::memory_order_relaxed);
                changed = changed || (parameterAffectsFilters(i) && std::abs(target - current) > 1.0e-4f);
                continue;
            }
            float current = m_smoothed[i].load(std::memory_order_relaxed);
            current += (target - current) * coeff;
            m_smoothed[i].store(current, std::memory_order_relaxed);
            changed = changed || (parameterAffectsFilters(i) && std::abs(target - current) > 1.0e-4f);
        }
        for (uint32_t slot = kLegacyBandCount; slot < kMaxDynamicBands; ++slot) {
            if (!m_bandEnabled[slot].load(std::memory_order_relaxed)) {
                snapDynamicBandSmoothing(slot);
                continue;
            }
            changed = smoothDynamicBandValue(m_dynamicBandFreqs[slot], m_smoothedDynamicBandFreqs[slot], coeff) ||
                      changed;
            changed = smoothDynamicBandValue(m_dynamicBandGains[slot], m_smoothedDynamicBandGains[slot], coeff) ||
                      changed;
            changed = smoothDynamicBandValue(m_dynamicBandQs[slot], m_smoothedDynamicBandQs[slot], coeff) || changed;
        }
        return changed;
    }

    static bool smoothDynamicBandValue(const std::atomic<float>& targetValue,
                                       std::atomic<float>& smoothedValue,
                                       float coeff) {
        const float target = targetValue.load(std::memory_order_relaxed);
        float current = smoothedValue.load(std::memory_order_relaxed);
        current += (target - current) * coeff;
        smoothedValue.store(current, std::memory_order_relaxed);
        return std::abs(target - current) > 1.0e-4f;
    }

    void applyOutputStage(float* buffer, uint32_t numFrames) const {
        if (!buffer || numFrames == 0) return;
        float gain = outputGainToLinear(m_smoothed[kParamOutputGain].load(std::memory_order_relaxed));
        if (m_smoothed[kParamPolarityInvert].load(std::memory_order_relaxed) > 0.5f) {
            gain = -gain;
        }
        for (uint32_t i = 0; i < numFrames; ++i) {
            buffer[i] = sanitizeSample(buffer[i] * gain);
        }
    }

    void processBandBuffer(uint32_t band, uint32_t channelSlot, float* buffer, uint32_t numFrames) {
        if (!buffer || band >= kMaxDynamicBands || channelSlot >= 2) return;
        const uint32_t stages = m_bandStages[band].load(std::memory_order_relaxed);
        const uint32_t stageBase = filterStageBase(channelSlot, band);
        for (uint32_t stage = 0; stage < stages; ++stage) {
            m_filters[stageBase + stage].process(buffer, numFrames);
        }
    }

    void processBandBlock(uint32_t band,
                          float** outputs,
                          const std::array<bool, 2>& channelHasInput,
                          uint32_t channels,
                          uint32_t sampleOffset,
                          uint32_t blockFrames) {
        if (!outputs || band >= kMaxDynamicBands || blockFrames == 0) return;
        const StereoMode mode = slotStereoMode(band);

        switch (mode) {
        case StereoMode::Left:
            if (channels > 0 && channelHasInput[0] && outputs[0]) {
                processBandBuffer(band, 0, outputs[0] + sampleOffset, blockFrames);
            }
            return;
        case StereoMode::Right:
            if (channels > 1 && channelHasInput[1] && outputs[1]) {
                processBandBuffer(band, 1, outputs[1] + sampleOffset, blockFrames);
            }
            return;
        case StereoMode::Mid:
        case StereoMode::Side:
            if (channels > 1 && channelHasInput[0] && channelHasInput[1] && outputs[0] && outputs[1]) {
                std::array<float, kBlockSize> work{};
                const float sign = mode == StereoMode::Mid ? 1.0f : -1.0f;
                for (uint32_t i = 0; i < blockFrames; ++i) {
                    const float l = outputs[0][sampleOffset + i];
                    const float r = outputs[1][sampleOffset + i];
                    work[i] = 0.5f * (l + sign * r);
                }
                processBandBuffer(band, mode == StereoMode::Mid ? 0u : 1u, work.data(), blockFrames);
                for (uint32_t i = 0; i < blockFrames; ++i) {
                    const uint32_t frame = sampleOffset + i;
                    if (mode == StereoMode::Mid) {
                        const float side = 0.5f * (outputs[0][frame] - outputs[1][frame]);
                        outputs[0][frame] = sanitizeSample(work[i] + side);
                        outputs[1][frame] = sanitizeSample(work[i] - side);
                    } else {
                        const float mid = 0.5f * (outputs[0][frame] + outputs[1][frame]);
                        outputs[0][frame] = sanitizeSample(mid + work[i]);
                        outputs[1][frame] = sanitizeSample(mid - work[i]);
                    }
                }
                return;
            }
            if (channels > 0 && channelHasInput[0] && outputs[0]) {
                processBandBuffer(band, 0, outputs[0] + sampleOffset, blockFrames);
            }
            return;
        case StereoMode::Stereo:
        default:
            for (uint32_t ch = 0; ch < channels; ++ch) {
                if (!channelHasInput[ch] || !outputs[ch]) continue;
                processBandBuffer(band, ch, outputs[ch] + sampleOffset, blockFrames);
            }
            return;
        }
    }

    // ---- Coefficient rebuild ----
    void rebuildAllCoefficients() {
        for (uint32_t band = 0; band < kMaxDynamicBands; ++band) {
            const bool enabled = m_bandEnabled[band].load(std::memory_order_relaxed);
            const FilterType type = slotFilterType(band);

            if (!enabled) {
                if (m_runtimeBandEnabled[band]) {
                    resetBandFilterStates(band);
                }
                setDynamicDetectorCoefficients(band, FilterType::BandPass, 0.5f, 0.091f, false);
                for (uint32_t ch = 0; ch < 2; ++ch) {
                    const uint32_t stageBase = filterStageBase(ch, band);
                    for (uint32_t stage = 0; stage < kMaxFilterStages; ++stage) {
                        m_filters[stageBase + stage].setCoeffs(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
                    }
                }
                m_bandStages[band].store(0u, std::memory_order_relaxed);
                m_dynamicEnvelopeAmounts[band] = 0.0f;
                m_runtimeBandEnabled[band] = false;
                m_runtimeBandTypes[band] = type;
                m_runtimeBandModes[band] = slotStereoMode(band);
                m_runtimeBandSoloAudition[band] = false;
                m_runtimeBandStages[band] = 0u;
                continue;
            }

            const float freqNorm = slotFrequencyNormForProcessing(band);
            const float gainNorm = slotGainNormForProcessing(band);
            const float qNorm = slotQNormForProcessing(band);
            const float slopeNorm = slotUsesSlope(band) ? qNorm : 0.0f;

            float freq = slotFrequencyHz(band, freqNorm);
            freq = std::clamp(freq, 20.0f, static_cast<float>(m_sampleRate) * 0.49f);
            const float gainDb = filterTypeUsesGain(type) ? gainToDb(gainNorm) : 0.0f;
            float q = slotQValue(band, qNorm);
            q = std::clamp(q, 0.1f, 10.0f);
            const bool soloAudition = m_soloBand.load(std::memory_order_acquire) == static_cast<int32_t>(band);
            const uint32_t stages = soloAudition ? 1u : slotStageCount(band, type, slopeNorm);
            const FilterType processType = soloAudition ? FilterType::BandPass : type;
            float processGainDb = soloAudition ? 0.0f : gainDb;
            if (!soloAudition && band >= kLegacyBandCount && filterTypeUsesGain(type) &&
                m_dynamicBandDynamicEnabled[band].load(std::memory_order_relaxed)) {
                const float targetGainDb =
                    gainToDb(m_dynamicBandTargetGains[band].load(std::memory_order_relaxed));
                const float amount = std::clamp(m_dynamicEnvelopeAmounts[band], 0.0f, 1.0f);
                processGainDb = gainDb + (targetGainDb - gainDb) * amount;
            }
            const float processQ = soloAudition ? std::clamp(q, 0.65f, 8.0f) : q;
            const bool dynamicDetectorEnabled = band >= kLegacyBandCount &&
                                                m_dynamicBandDynamicEnabled[band].load(std::memory_order_relaxed) &&
                                                filterTypeUsesGain(type);
            const bool sidechainLinked =
                band < kLegacyBandCount || m_dynamicBandSidechainLinked[band].load(std::memory_order_relaxed);
            const FilterType detectorType =
                sidechainLinked
                    ? detectorFilterTypeFor(type)
                    : detectorFilterTypeFor(dynamicTypeParamToFilterType(
                          m_dynamicBandSidechainTypes[band].load(std::memory_order_relaxed)));
            const float detectorFreqNorm =
                sidechainLinked ? freqNorm : m_dynamicBandSidechainFreqs[band].load(std::memory_order_relaxed);
            const float detectorQNorm =
                sidechainLinked ? qNorm : m_dynamicBandSidechainQs[band].load(std::memory_order_relaxed);
            setDynamicDetectorCoefficients(band, detectorType, detectorFreqNorm, detectorQNorm,
                                           dynamicDetectorEnabled && !soloAudition);

            if (!m_runtimeBandEnabled[band] ||
                m_runtimeBandTypes[band] != type ||
                m_runtimeBandStages[band] != stages ||
                m_runtimeBandModes[band] != slotStereoMode(band) ||
                m_runtimeBandSoloAudition[band] != soloAudition) {
                resetBandFilterStates(band);
            }
            m_bandStages[band].store(stages, std::memory_order_relaxed);
            const StereoMode mode = slotStereoMode(band);

            const bool firstOrderCut = !soloAudition && slotUsesSlope(band) && slopeUsesFirstOrderStage(slopeNorm);

            for (uint32_t ch = 0; ch < 2; ++ch) {
                const uint32_t stageBase = filterStageBase(ch, band);
                for (uint32_t stage = 0; stage < kMaxFilterStages; ++stage) {
                    if (stage < stages) {
                        const float stageQ = soloAudition ? processQ : stageQForBand(band, stage, stages, processQ);
                        auto coeffs = stageCoeffsForFilter(
                            processType, freq, processGainDb, stageQ, static_cast<float>(m_sampleRate), stage, firstOrderCut
                        );
                        if (!std::isfinite(coeffs.b0) || !std::isfinite(coeffs.a0) ||
                            std::abs(coeffs.a0) < 1.0e-12f) {
                            coeffs = {1, 0, 0, 1, 0, 0};
                        }
                        m_filters[stageBase + stage].setCoeffs(
                            coeffs.b0, coeffs.b1, coeffs.b2, coeffs.a0, coeffs.a1, coeffs.a2
                        );
                    } else {
                        m_filters[stageBase + stage].setCoeffs(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
                        m_filters[stageBase + stage].reset();
                    }
                }
            }
            m_runtimeBandEnabled[band] = true;
            m_runtimeBandTypes[band] = type;
            m_runtimeBandStages[band] = stages;
            m_runtimeBandModes[band] = mode;
            m_runtimeBandSoloAudition[band] = soloAudition;
        }
    }

    void resetBandFilterStates(uint32_t band) {
        if (band >= kMaxDynamicBands) return;
        for (uint32_t ch = 0; ch < 2; ++ch) {
            const uint32_t stageBase = filterStageBase(ch, band);
            for (uint32_t stage = 0; stage < kMaxFilterStages; ++stage) {
                m_filters[stageBase + stage].reset();
            }
        }
    }

    void resetAllFilterStates() {
        for (uint32_t band = 0; band < kMaxDynamicBands; ++band) {
            resetBandFilterStates(band);
        }
    }

    int32_t activeSoloBand() const {
        const int32_t solo = m_soloBand.load(std::memory_order_acquire);
        if (solo < 0 || solo >= static_cast<int32_t>(kMaxDynamicBands)) {
            return -1;
        }
        if (!m_bandEnabled[static_cast<uint32_t>(solo)].load(std::memory_order_relaxed)) {
            return -1;
        }
        return solo;
    }

    void readSmoothedBandParams(uint32_t band, float& freq, float& gain, float& q, float& slope) const {
        freq = m_smoothed[bandV1FreqId(band)].load(std::memory_order_relaxed);
        const FilterType type = bandFilterType(band, m_smoothed[bandV1TypeId(band)].load(std::memory_order_relaxed));
        if (filterTypeUsesGain(type)) {
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
                    const float qOrSlope = v1->params[v1Base + 4];
                    setParameter(bandV1QId(band),
                                 bandUsesSlope(band) ? legacySlopeNormToExtended(qOrSlope)
                                                     : std::clamp(qOrSlope, 0.0f, 1.0f));
                }
            } else {
                // Type mismatch — load defaults for this band
                const auto params = getParameters();
                for (const auto& p : params) {
                    if (p.id == bandV1FreqId(band)) {
                        setParameter(bandV1FreqId(band), p.defaultValue);
                        break;
                    }
                }
                if (bandUsesGain(band)) {
                    for (const auto& p : params) {
                        if (p.id == bandV1GainId(band)) {
                            setParameter(bandV1GainId(band), p.defaultValue);
                            break;
                        }
                    }
                }
                for (const auto& p : params) {
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
        setParameter(kParamBell1Type, 0.0f);
        setParameter(kParamBell2Type, 0.0f);
        setParameter(kParamOutputGain, 0.5f);
        setParameter(kParamPolarityInvert, 0.0f);
        defaultStereoPlacementParams();
        resetDynamicBandSlots();

        m_filtersDirty.store(true, std::memory_order_release);
        return true;
    }

    // ---- Analyzer ----
    static constexpr uint32_t analyzerSourceIndex(AnalyzerSource source) {
        return source == AnalyzerSource::Pre ? 0u : 1u;
    }

    static constexpr uint32_t analyzerStereoModeIndex(StereoMode mode) {
        switch (mode) {
        case StereoMode::Left: return 1u;
        case StereoMode::Right: return 2u;
        case StereoMode::Mid: return 3u;
        case StereoMode::Side: return 4u;
        case StereoMode::Stereo:
        default: return 0u;
        }
    }

    void publishAnalyzerFrame(AnalyzerSource source, const float* const* buffers, uint32_t numChannels, uint32_t numFrames) {
        if (numChannels == 0 || !buffers) return;

        const uint32_t sourceIdx = analyzerSourceIndex(source);

        for (uint32_t i = 0; i < numFrames; ++i) {
            const float left = buffers[0] ? sanitizeSample(buffers[0][i]) : 0.0f;
            const float right = (numChannels > 1 && buffers[1]) ? sanitizeSample(buffers[1][i]) : left;
            const std::array<float, kAnalyzerStereoModeCount> samples = {
                (left + right) * 0.5f,
                left,
                right,
                (left + right) * 0.5f,
                (left - right) * 0.5f,
            };

            for (uint32_t mode = 0; mode < kAnalyzerStereoModeCount; ++mode) {
                if (m_analyzerWritePos[sourceIdx][mode] == 0) {
                    m_analyzerPageSerial[sourceIdx][mode][m_analyzerWritePage[sourceIdx][mode]].fetch_add(
                        1, std::memory_order_acq_rel);
                }
                m_analyzerPages[sourceIdx][mode][m_analyzerWritePage[sourceIdx][mode]]
                               [m_analyzerWritePos[sourceIdx][mode]++] = samples[mode];
                if (m_analyzerWritePos[sourceIdx][mode] >= kAnalyzerWindowSize) {
                    m_analyzerPageSerial[sourceIdx][mode][m_analyzerWritePage[sourceIdx][mode]].fetch_add(
                        1, std::memory_order_release);
                    m_publishedAnalyzerPage[sourceIdx][mode].store(m_analyzerWritePage[sourceIdx][mode],
                                                                   std::memory_order_release);
                    m_publishedAnalyzerSerial[sourceIdx][mode].fetch_add(1, std::memory_order_release);
                    m_analyzerWritePage[sourceIdx][mode] = 1u - m_analyzerWritePage[sourceIdx][mode];
                    m_analyzerWritePos[sourceIdx][mode] = 0;
                }
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

    std::array<std::atomic<float>, kParamCount> m_params{};
    std::array<std::atomic<float>, kParamCount> m_smoothed{};
    std::array<std::atomic<bool>, kMaxDynamicBands> m_bandEnabled{};
    std::array<std::atomic<uint32_t>, kMaxDynamicBands> m_bandStages{};
    std::array<DynamicBandSlotDefaults, kMaxDynamicBands> m_dynamicBandSlots{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandTypes{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandStereoModes{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandFreqs{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandGains{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandQs{};
    std::array<std::atomic<bool>, kMaxDynamicBands> m_dynamicBandDynamicEnabled{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandTargetGains{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandThresholds{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandKnees{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandAttacks{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandReleases{};
    std::array<std::atomic<bool>, kMaxDynamicBands> m_dynamicBandSidechainLinked{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandSidechainTypes{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandSidechainFreqs{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicBandSidechainQs{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_smoothedDynamicBandFreqs{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_smoothedDynamicBandGains{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_smoothedDynamicBandQs{};
    std::array<float, kMaxDynamicBands> m_dynamicEnvelopeAmounts{};
    std::array<std::atomic<float>, kMaxDynamicBands> m_dynamicEnvelopeMeters{};
    std::array<bool, kMaxDynamicBands> m_runtimeBandEnabled{};
    std::array<FilterType, kMaxDynamicBands> m_runtimeBandTypes{};
    std::array<StereoMode, kMaxDynamicBands> m_runtimeBandModes{};
    std::array<bool, kMaxDynamicBands> m_runtimeBandSoloAudition{};
    std::array<uint32_t, kMaxDynamicBands> m_runtimeBandStages{};
    std::atomic<int32_t> m_soloBand{-1};
    int32_t m_runtimeSoloBand = -1;

    // 2 channels * kMaxDynamicBands preallocated slots * kMaxFilterStages
    std::array<BiquadFilter, 2 * kMaxDynamicBands * kMaxFilterStages> m_filters;
    std::array<BiquadFilter, 2 * kMaxDynamicBands> m_dynamicDetectorFilters;

    std::array<std::array<std::array<std::array<float, kAnalyzerWindowSize>, 2>, kAnalyzerStereoModeCount>,
               kAnalyzerSourceCount>
        m_analyzerPages{};
    std::array<std::array<std::atomic<uint32_t>, kAnalyzerStereoModeCount>, kAnalyzerSourceCount>
        m_publishedAnalyzerPage{};
    std::array<std::array<std::atomic<uint64_t>, kAnalyzerStereoModeCount>, kAnalyzerSourceCount>
        m_publishedAnalyzerSerial{};
    std::array<std::array<std::array<std::atomic<uint64_t>, 2>, kAnalyzerStereoModeCount>, kAnalyzerSourceCount>
        m_analyzerPageSerial{};
    std::array<std::array<uint32_t, kAnalyzerStereoModeCount>, kAnalyzerSourceCount> m_analyzerWritePage{};
    std::array<std::array<uint32_t, kAnalyzerStereoModeCount>, kAnalyzerSourceCount> m_analyzerWritePos{};
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
