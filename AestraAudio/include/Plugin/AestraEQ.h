// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// EQ — Parametric equalizer with biquad filters.
// Arsenal effect plugin for Aestra DAW.

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
// Biquad Filter (Cookbook formulae, Robert Bristow-Johnson)
// ============================================================================

class BiquadFilter {
public:
    BiquadFilter() { reset(); }

    void reset() {
        m_x1 = m_x2 = m_y1 = m_y2 = 0.0f;
    }

    void setCoeffs(float b0, float b1, float b2, float a0, float a1, float a2) {
        // Normalize by a0 and validate
        const float invA0 = 1.0f / a0;
        m_b0 = b0 * invA0;
        m_b1 = b1 * invA0;
        m_b2 = b2 * invA0;
        m_a1 = a1 * invA0;
        m_a2 = a2 * invA0;

        // Validate coefficients — reset if unstable
        if (std::isnan(m_b0) || std::isinf(m_b0) ||
            std::isnan(m_a1) || std::isinf(m_a1) ||
            std::isnan(m_a2) || std::isinf(m_a2)) {
            // Unstable coefficients — set to passthrough
            m_b0 = 1; m_b1 = 0; m_b2 = 0;
            m_a1 = 0; m_a2 = 0;
        }
    }

    float process(float in) {
        // Guard against NaN propagation
        if (std::isnan(in) || std::isinf(in)) {
            m_x1 = m_x2 = m_y1 = m_y2 = 0.0f;
            return 0.0f;
        }
        const float out = m_b0 * in + m_b1 * m_x1 + m_b2 * m_x2
                        - m_a1 * m_y1 - m_a2 * m_y2;
        if (std::isnan(out) || std::isinf(out)) {
            // Filter instability detected — reset state
            m_x1 = m_x2 = m_y1 = m_y2 = 0.0f;
            return 0.0f;
        }
        m_x2 = m_x1; m_x1 = in;
        m_y2 = m_y1; m_y1 = out;
        return out;
    }

    void process(float* buffer, uint32_t numFrames) {
        for (uint32_t i = 0; i < numFrames; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

private:
    float m_b0 = 1, m_b1 = 0, m_b2 = 0;
    float m_a1 = 0, m_a2 = 0;
    float m_x1 = 0, m_x2 = 0;
    float m_y1 = 0, m_y2 = 0;
};

// ============================================================================
// Filter Design Functions
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
    const float A = std::pow(10.0f, gainDb / 40.0f); // sqrt of linear gain
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
        // High-pass filter: passes frequencies ABOVE cutoff
        b0 =  (1 + cos_w0) / 2;
        b1 = -(1 + cos_w0);
        b2 =  (1 + cos_w0) / 2;
        a0 =   1 + alpha;
        a1 =  -2 * cos_w0;
        a2 =   1 - alpha;
        break;
    }
    case FilterType::HighCut: {
        // Low-pass filter: passes frequencies BELOW cutoff
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
        // Tilt shelf: opposite slopes for low and high
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
// EQ Band
// ============================================================================

struct EQBand {
    std::atomic<bool> enabled{true};
    std::atomic<uint32_t> type{0};     // FilterType enum
    std::atomic<float> frequency{0.5f}; // normalized 0-1 → 20Hz-20kHz
    std::atomic<float> gain{0.5f};      // normalized 0-1 → -18dB to +18dB
    std::atomic<float> q{0.5f};         // normalized 0-1 → 0.1-10.0
};

struct EQStateBlob {
    uint32_t magic;
    uint32_t version;
    float params[41]; // kParamCount
    uint8_t enabled[8];
    uint8_t types[8];
};

// ============================================================================
// Aestra EQ — Parametric Equalizer (8 bands)
// ============================================================================

class AestraEQ : public IPluginInstance {
public:
    static constexpr uint32_t kNumBands = 8;
    static constexpr uint32_t kStateMagic = 0x45510001; // 'EQ' + version 1
    static constexpr uint32_t kAnalyzerWindowSize = 1024;
    static constexpr uint32_t kMaxFilterStages = 8;
    static constexpr std::array<float, kNumBands> kDefaultFreqs = {
        0.04f, 0.12f, 0.22f, 0.36f, 0.50f, 0.66f, 0.80f, 0.92f
    };

    // Parameter IDs: per-band params (5 per band) + master bypass
    static constexpr uint32_t kParamBandStart = 0;
    static constexpr uint32_t kParamBypass = kParamBandStart + kNumBands * 5;
    static constexpr uint32_t kParamCount = kParamBypass + 1;

    AestraEQ() = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;

        // Initialize bands with default values
        // Start with a neutral, consistent graph: all visible bands begin as
        // bell filters distributed across the spectrum. That keeps the default
        // interaction intuitive before type-specific shaping is introduced.
        for (uint32_t i = 0; i < kNumBands; ++i) {
            m_bands[i].frequency.store(kDefaultFreqs[i], std::memory_order_relaxed);
            m_bands[i].q.store(0.5f, std::memory_order_relaxed);
            m_bands[i].gain.store(0.5f, std::memory_order_relaxed);
            m_bands[i].type.store(static_cast<uint32_t>(FilterType::Bell), std::memory_order_relaxed);
            m_bands[i].enabled.store(true, std::memory_order_relaxed);
        }
        updateAllFilters();
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
            // Pass-through or silence
            for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
                if (outputs[ch] && ch < numInputChannels && inputs[ch]) {
                    std::memcpy(outputs[ch], inputs[ch], numFrames * sizeof(float));
                } else if (outputs[ch]) {
                    std::memset(outputs[ch], 0, numFrames * sizeof(float));
                }
            }
            publishAnalyzerFrame(outputs, std::min(numInputChannels, numOutputChannels), numFrames);
            return;
        }

        // Check bypass
        if (m_params[kParamBypass].load(std::memory_order_relaxed) > 0.5f) {
            for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
                if (outputs[ch] && ch < numInputChannels && inputs[ch]) {
                    std::memcpy(outputs[ch], inputs[ch], numFrames * sizeof(float));
                } else if (outputs[ch]) {
                    std::memset(outputs[ch], 0, numFrames * sizeof(float));
                }
            }
            publishAnalyzerFrame(outputs, std::min(numInputChannels, numOutputChannels), numFrames);
            return;
        }

        // Check if filter coefficients need updating (atomic snapshot)
        if (m_filtersDirty.load(std::memory_order_acquire)) {
            updateAllFilters();
            m_filtersDirty.store(false, std::memory_order_release);
        }

        // Process each channel
        const uint32_t channels = std::min(numInputChannels, numOutputChannels);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (!inputs[ch] || !outputs[ch]) continue;

            // Copy input to output
            std::memcpy(outputs[ch], inputs[ch], numFrames * sizeof(float));

            // Process through each enabled band
            for (uint32_t band = 0; band < kNumBands; ++band) {
                if (m_bands[band].enabled.load(std::memory_order_relaxed)) {
                    const FilterType type = static_cast<FilterType>(m_bands[band].type.load(std::memory_order_relaxed));
                    const uint32_t stages = filterStageCount(type, m_bands[band].q.load(std::memory_order_relaxed));
                    const uint32_t stageBase = (ch * kNumBands + band) * kMaxFilterStages;
                    for (uint32_t stage = 0; stage < stages; ++stage) {
                        m_filters[stageBase + stage].process(outputs[ch], numFrames);
                    }
                }
            }
        }

        // Handle extra output channels (silence or copy from first)
        for (uint32_t ch = channels; ch < numOutputChannels; ++ch) {
            if (outputs[ch]) std::memset(outputs[ch], 0, numFrames * sizeof(float));
        }

        publishAnalyzerFrame(outputs, channels, numFrames);
    }

    // ---- Parameters (normalized 0-1) ----
    uint32_t getParameterCount() const { return kParamCount; }

    float getParameter(uint32_t id) const {
        if (id >= kParamCount) return 0.0f;
        return m_params[id].load(std::memory_order_relaxed);
    }

    void setParameter(uint32_t id, float value) {
        if (id >= kParamCount) return;
        m_params[id].store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);

        // Update the corresponding band
        if (id < kNumBands * 5) {
            const uint32_t band = id / 5;
            const uint32_t sub = id % 5;
            switch (sub) {
            case 0: m_bands[band].enabled.store(value > 0.5f, std::memory_order_relaxed); break;
            case 1: m_bands[band].type.store(static_cast<uint32_t>(std::round(value * 7.0f)), std::memory_order_relaxed); break;
            case 2: m_bands[band].frequency.store(value, std::memory_order_relaxed); break;
            case 3: m_bands[band].gain.store(value, std::memory_order_relaxed); break;
            case 4: m_bands[band].q.store(value, std::memory_order_relaxed); break;
            }
            m_filtersDirty.store(true, std::memory_order_release);
        }
    }

    std::vector<PluginParameter> getParameters() const {
        std::vector<PluginParameter> params;
        params.reserve(kParamCount);

        for (uint32_t band = 0; band < kNumBands; ++band) {
            // Enabled
            params.push_back({
                band * 5, "Band " + std::to_string(band + 1) + " On",
                "B" + std::to_string(band + 1), "", 1.0f, 0.0f, 1.0f, true, false, false, 1
            });
            // Type
            params.push_back({
                band * 5 + 1, "Band " + std::to_string(band + 1) + " Type",
                "T" + std::to_string(band + 1), "", 0.0f, 0.0f, 1.0f, true, false, false, 7
            });
            // Frequency
            params.push_back({
                band * 5 + 2, "Band " + std::to_string(band + 1) + " Freq",
                "F" + std::to_string(band + 1), "Hz", kDefaultFreqs[band], 0.0f, 1.0f, true, false, false, 0
            });
            // Gain
            params.push_back({
                band * 5 + 3, "Band " + std::to_string(band + 1) + " Gain",
                "G" + std::to_string(band + 1), "dB", 0.5f, 0.0f, 1.0f, true, false, false, 0
            });
            // Q
            params.push_back({
                band * 5 + 4, "Band " + std::to_string(band + 1) + " Q",
                "Q" + std::to_string(band + 1), "", 0.5f, 0.0f, 1.0f, true, false, false, 0
            });
        }

        // Master bypass
        params.push_back({
            kParamBypass, "Bypass", "BYP", "", 0.0f, 0.0f, 1.0f, true, true, false, 1
        });

        return params;
    }

    std::string getParameterDisplay(uint32_t id) const {
        if (id >= kParamCount) return "";
        const float val = getParameter(id);

        if (id == kParamBypass) {
            return val > 0.5f ? "ON" : "OFF";
        }

        const uint32_t band = id / 5;
        const uint32_t sub = id % 5;

        switch (sub) {
        case 0: return val > 0.5f ? "ON" : "OFF";
        case 1: {
            static const char* names[] = {"Bell", "LoCut", "HiCut", "LoShelf", "HiShelf", "Notch", "BandPass", "Tilt"};
            const uint32_t idx = static_cast<uint32_t>(std::round(val * 7.0f));
            return names[idx];
        }
        case 2: {
            const float freq = freqToHz(val);
            if (freq >= 1000) return std::to_string(freq / 1000).substr(0, 4) + "kHz";
            return std::to_string(static_cast<int>(freq)) + "Hz";
        }
        case 3: {
            const float db = gainToDb(val);
            return (db >= 0 ? "+" : "") + std::to_string(db).substr(0, 5) + "dB";
        }
        case 4: {
            if (id < kNumBands * 5) {
                const uint32_t band = id / 5;
                const FilterType type = static_cast<FilterType>(m_bands[band].type.load(std::memory_order_relaxed));
                if (type == FilterType::LowCut || type == FilterType::HighCut) {
                    return std::to_string(filterStageCount(type, val) * 12) + " dB/oct";
                }
            }
            const float qVal = qToLinear(val);
            return std::to_string(qVal).substr(0, 4);
        }
        default: return "";
        }
    }

    // ---- State ----
    std::vector<uint8_t> saveState() const {
        EQStateBlob blob;
        blob.magic = kStateMagic;
        blob.version = 1;
        for (uint32_t i = 0; i < kParamCount; ++i) {
            blob.params[i] = getParameter(i);
        }
        for (uint32_t i = 0; i < kNumBands; ++i) {
            blob.enabled[i] = m_bands[i].enabled.load(std::memory_order_relaxed) ? 1 : 0;
            blob.types[i] = static_cast<uint8_t>(m_bands[i].type.load(std::memory_order_relaxed));
        }

        const uint8_t* data = reinterpret_cast<const uint8_t*>(&blob);
        return std::vector<uint8_t>(data, data + sizeof(blob));
    }

    bool loadState(const std::vector<uint8_t>& state) {
        if (state.size() < sizeof(EQStateBlob)) return false;

        const EQStateBlob* blob = reinterpret_cast<const EQStateBlob*>(state.data());
        if (blob->magic != kStateMagic) return false;

        for (uint32_t i = 0; i < kParamCount; ++i) {
            setParameter(i, blob->params[i]);
        }
        for (uint32_t i = 0; i < kNumBands; ++i) {
            m_bands[i].enabled.store(blob->enabled[i] != 0, std::memory_order_relaxed);
            m_bands[i].type.store(blob->types[i], std::memory_order_relaxed);
        }
        m_filtersDirty.store(true, std::memory_order_release);
        return true;
    }

    // ---- Editor (headless for now) ----
    bool hasEditor() const { return false; }
    bool openEditor(void*) { return false; }
    void closeEditor() {}
    bool isEditorOpen() const { return false; }
    std::pair<int, int> getEditorSize() const { return {800, 600}; }
    bool resizeEditor(int, int) { return false; }

    const PluginInfo& getInfo() const { return m_info; }
    uint32_t getLatencySamples() const { return 0; }
    uint32_t getTailSamples() const { return 64; } // filter tail
    double getAnalyzerSampleRate() const { return m_sampleRate; }
    bool getAnalyzerWindow(std::array<float, kAnalyzerWindowSize>& out, uint64_t* outSerial = nullptr) const {
        const uint64_t serial = m_publishedAnalyzerSerial.load(std::memory_order_acquire);
        if (serial == 0) {
            return false;
        }

        const uint32_t page = m_publishedAnalyzerPage.load(std::memory_order_acquire);
        out = m_analyzerPages[page];
        if (outSerial) {
            *outSerial = serial;
        }
        return true;
    }

    WatchdogStats getWatchdogStats() const { return {}; }
    void resetWatchdog() {}
    bool isBypassedByWatchdog() const { return false; }
    bool isCrashed() const { return false; }

    // ---- Helpers ----
    void setInfo(const PluginInfo& info) { m_info = info; }

private:
    // Frequency mapping: normalized 0-1 → 20Hz-20kHz (logarithmic)
    static float freqToHz(float norm) {
        const float logMin = std::log10(20.0f);
        const float logMax = std::log10(20000.0f);
        return std::pow(10.0f, logMin + norm * (logMax - logMin));
    }

    // Gain mapping: normalized 0-1 → -18dB to +18dB
    static float gainToDb(float norm) {
        return -18.0f + norm * 36.0f;
    }

    // Q mapping: normalized 0-1 → 0.1 to 10.0
    static float qToLinear(float norm) {
        return 0.1f + norm * 9.9f;
    }

    static uint32_t cutSlopeDbPerOct(float norm) {
        static constexpr std::array<uint32_t, 5> kSlopeDb = {12u, 24u, 48u, 72u, 96u};
        const float clamped = std::clamp(norm, 0.0f, 1.0f);
        const size_t index = static_cast<size_t>(std::round(clamped * static_cast<float>(kSlopeDb.size() - 1)));
        return kSlopeDb[std::min(index, kSlopeDb.size() - 1)];
    }

    static uint32_t filterStageCount(FilterType type, float norm) {
        if (type != FilterType::LowCut && type != FilterType::HighCut) {
            return 1;
        }
        switch (cutSlopeDbPerOct(norm)) {
        case 12u: return 1u;
        case 24u: return 2u;
        case 48u: return 4u;
        case 72u: return 6u;
        case 96u: return 8u;
        default: return 1u;
        }
    }

    static float effectiveFilterQ(FilterType type, float norm) {
        if (type == FilterType::LowCut || type == FilterType::HighCut) {
            // For cascaded cut filters, keep each stage Butterworth-like so
            // slope increases without introducing a resonant peak.
            return 0.70710678f;
        }
        return qToLinear(norm);
    }

    static float dbToNorm(float db) { return (db + 18.0f) / 36.0f; }
    static float freqToNorm(float hz) {
        const float logMin = std::log10(20.0f);
        const float logMax = std::log10(20000.0f);
        return (std::log10(hz) - logMin) / (logMax - logMin);
    }

    void updateAllFilters() {
        for (uint32_t ch = 0; ch < 2; ++ch) {
            for (uint32_t band = 0; band < kNumBands; ++band) {
                if (!m_bands[band].enabled.load(std::memory_order_relaxed)) continue;

                const float freq = freqToHz(m_bands[band].frequency.load(std::memory_order_relaxed));
                const float gain = gainToDb(m_bands[band].gain.load(std::memory_order_relaxed));
                const FilterType type = static_cast<FilterType>(m_bands[band].type.load(std::memory_order_relaxed));
                const float q = effectiveFilterQ(type, m_bands[band].q.load(std::memory_order_relaxed));
                const uint32_t stages = filterStageCount(type, m_bands[band].q.load(std::memory_order_relaxed));

                const auto coeffs = designBiquad(type, freq, gain, q, m_sampleRate);
                const uint32_t stageBase = (ch * kNumBands + band) * kMaxFilterStages;
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

    void publishAnalyzerFrame(float** outputs, uint32_t numChannels, uint32_t numFrames) {
        if (numChannels == 0 || !outputs) {
            return;
        }

        for (uint32_t i = 0; i < numFrames; ++i) {
            float mono = 0.0f;
            uint32_t contributingChannels = 0;

            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                if (!outputs[ch]) {
                    continue;
                }
                mono += outputs[ch][i];
                ++contributingChannels;
            }

            if (contributingChannels == 0) {
                mono = 0.0f;
            } else if (contributingChannels > 1) {
                mono /= static_cast<float>(contributingChannels);
            }

            m_analyzerPages[m_analyzerWritePage][m_analyzerWritePos++] = mono;
            if (m_analyzerWritePos >= kAnalyzerWindowSize) {
                m_publishedAnalyzerPage.store(m_analyzerWritePage, std::memory_order_release);
                m_publishedAnalyzerSerial.fetch_add(1, std::memory_order_release);
                m_analyzerWritePage = 1u - m_analyzerWritePage;
                m_analyzerWritePos = 0;
            }
        }
    }

    PluginInfo m_info;
    double m_sampleRate = 48000.0;
    uint32_t m_maxBlockSize = 512;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_filtersDirty{true};

    std::array<EQBand, kNumBands> m_bands;
    std::array<BiquadFilter, kNumBands * 2 * kMaxFilterStages> m_filters; // 2 channels, staged for steeper cuts
    std::array<std::atomic<float>, kParamCount> m_params;
    std::array<std::array<float, kAnalyzerWindowSize>, 2> m_analyzerPages{};
    std::atomic<uint32_t> m_publishedAnalyzerPage{0};
    std::atomic<uint64_t> m_publishedAnalyzerSerial{0};
    uint32_t m_analyzerWritePage = 0;
    uint32_t m_analyzerWritePos = 0;
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
