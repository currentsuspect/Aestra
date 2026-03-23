#include "RumbleInstance.h"

#include "Plugin/BuiltInPlugins.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace {
constexpr double kPi = 3.14159265358979323846;

constexpr uint32_t kRumbleStateMagic = 0x524D424Cu; // 'RMBL'
constexpr uint32_t kRumbleStateVersion = 1;

struct RumbleStateBlob {
    uint32_t magic;
    uint32_t version;
    float decay;
    float drive;
    float tone;
    float outputGain;
};
} // namespace

namespace Aestra {
namespace Plugins {

RumbleInstance::RumbleInstance() {
    m_params[kParamDecay].store(0.45f, std::memory_order_relaxed);
    m_params[kParamDrive].store(0.10f, std::memory_order_relaxed);
    m_params[kParamTone].store(0.30f, std::memory_order_relaxed);
    m_params[kParamOutputGain].store(0.50f, std::memory_order_relaxed);
}

bool RumbleInstance::initialize(double sampleRate, uint32_t maxBlockSize) {
    m_sampleRate = sampleRate > 1.0 ? sampleRate : 44100.0;
    m_maxBlockSize = maxBlockSize > 0 ? maxBlockSize : 512;
    m_filterState = 0.0f;
    m_voice = {};
    updateVoiceTuning();
    updateEnvelopeRate();
    updateToneCoefficient();
    return true;
}

void RumbleInstance::shutdown() {
    m_active.store(false, std::memory_order_release);
    m_voice = {};
    m_filterState = 0.0f;
}

void RumbleInstance::activate() {
    m_active.store(true, std::memory_order_release);
    m_filterState = 0.0f;
}

void RumbleInstance::deactivate() {
    m_active.store(false, std::memory_order_release);
    m_voice.active = false;
}

void RumbleInstance::process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                             uint32_t numOutputChannels, uint32_t numFrames,
                             const Aestra::Audio::MidiBuffer* midiInput, Aestra::Audio::MidiBuffer* midiOutput) {
    (void)inputs;
    (void)numInputChannels;
    (void)midiOutput;

    if (!outputs || numOutputChannels == 0) {
        return;
    }

    for (uint32_t c = 0; c < numOutputChannels; ++c) {
        if (outputs[c]) {
            std::memset(outputs[c], 0, sizeof(float) * numFrames);
        }
    }

    if (!isActive()) {
        return;
    }

    updateEnvelopeRate();

    size_t eventIdx = 0;
    size_t eventCount = midiInput ? midiInput->getEventCount() : 0;

    for (uint32_t i = 0; i < numFrames; ++i) {
        while (midiInput && eventIdx < eventCount) {
            const auto& event = midiInput->getEvent(eventIdx);
            if (event.sampleOffset == i) {
                handleMidiEvent(event);
                ++eventIdx;
            } else if (event.sampleOffset < i) {
                ++eventIdx;
            } else {
                break;
            }
        }

        if (!m_voice.active) {
            continue;
        }

        const float raw = static_cast<float>(std::sin(m_voice.phase));
        m_voice.phase += m_voice.phaseIncrement;
        if (m_voice.phase >= (2.0 * kPi)) {
            m_voice.phase = std::fmod(m_voice.phase, 2.0 * kPi);
        }

        m_voice.envelope *= m_voice.releaseCoeff;
        if (m_voice.envelope < 1.0e-5) {
            m_voice.envelope = 0.0;
            m_voice.active = false;
            continue;
        }

        float sample = raw * static_cast<float>(m_voice.envelope) * m_voice.velocity;
        sample *= 0.65f; // keep default voicing in a safer range before coloration
        sample = applyDrive(sample);
        sample = processToneFilter(sample);
        sample *= getOutputGainLinear();
        sample = std::tanh(sample * 0.9f); // final safety stage to avoid runaway peaks in MVP mode

        if (outputs[0]) {
            outputs[0][i] = sample;
        }
        if (numOutputChannels > 1 && outputs[1]) {
            outputs[1][i] = sample;
        }
    }
}

std::vector<Aestra::Audio::PluginParameter> RumbleInstance::getParameters() const {
    using Aestra::Audio::PluginParameter;
    return {
        PluginParameter{kParamDecay, "Decay", "Decay", "s", 0.45f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamDrive, "Drive", "Drive", "%", 0.10f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamTone, "Tone", "Tone", "Hz", 0.30f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamOutputGain, "Output Gain", "Gain", "dB", 0.50f, 0.0f, 1.0f, true, false, false,
                        0},
    };
}

uint32_t RumbleInstance::getParameterCount() const { return kParamCount; }

float RumbleInstance::getParameter(uint32_t id) const {
    if (id >= kParamCount) {
        return 0.0f;
    }
    return m_params[id].load(std::memory_order_relaxed);
}

void RumbleInstance::setParameter(uint32_t id, float value) {
    if (id >= kParamCount) {
        return;
    }
    m_params[id].store(clamp01(value), std::memory_order_relaxed);
    if (id == kParamTone) {
        updateToneCoefficient();
    }
}

std::string RumbleInstance::getParameterDisplay(uint32_t id) const {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);

    switch (id) {
    case kParamDecay:
        out << getDecaySeconds() << " s";
        break;
    case kParamDrive:
        out << (getDriveAmount() * 100.0f) << "%";
        break;
    case kParamTone:
        out << getToneHz() << " Hz";
        break;
    case kParamOutputGain:
        out << ((getParameter(kParamOutputGain) * 24.0f) - 12.0f) << " dB";
        break;
    default:
        return "0";
    }
    return out.str();
}

std::vector<uint8_t> RumbleInstance::saveState() const {
    RumbleStateBlob blob{kRumbleStateMagic, kRumbleStateVersion, getParameter(kParamDecay), getParameter(kParamDrive),
                         getParameter(kParamTone), getParameter(kParamOutputGain)};
    std::vector<uint8_t> bytes(sizeof(blob));
    std::memcpy(bytes.data(), &blob, sizeof(blob));
    return bytes;
}

bool RumbleInstance::loadState(const std::vector<uint8_t>& state) {
    if (state.size() != sizeof(RumbleStateBlob)) {
        return false;
    }
    RumbleStateBlob blob{};
    std::memcpy(&blob, state.data(), sizeof(blob));
    if (blob.magic != kRumbleStateMagic || blob.version != kRumbleStateVersion) {
        return false;
    }
    setParameter(kParamDecay, blob.decay);
    setParameter(kParamDrive, blob.drive);
    setParameter(kParamTone, blob.tone);
    setParameter(kParamOutputGain, blob.outputGain);
    return true;
}

const Aestra::Audio::PluginInfo& RumbleInstance::getInfo() const {
    return Aestra::Audio::BuiltInPlugins::rumbleInfo();
}

uint32_t RumbleInstance::getTailSamples() const {
    return static_cast<uint32_t>(std::ceil(getDecaySeconds() * m_sampleRate));
}

void RumbleInstance::handleMidiEvent(const Aestra::Audio::MidiBuffer::Event& event) {
    if (event.size < 3) {
        return;
    }

    const uint8_t status = event.data[0] & 0xF0;
    const uint8_t note = event.data[1];
    const uint8_t velocity = event.data[2];

    if (status == 0x90 && velocity > 0) {
        m_voice.active = true;
        m_voice.note = note;
        m_voice.velocity = std::max(0.05f, velocity / 127.0f);
        m_voice.phase = 0.0;
        m_voice.envelope = 1.0;
        updateVoiceTuning();
        updateEnvelopeRate();
    } else if (status == 0x80 || (status == 0x90 && velocity == 0)) {
        // MVP behavior: classic 808-style decay continues naturally after note-off.
        (void)note;
    }
}

float RumbleInstance::midiNoteToFrequency(uint8_t note) {
    return 440.0f * std::pow(2.0f, (static_cast<int>(note) - 69) / 12.0f);
}

float RumbleInstance::clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }

float RumbleInstance::getDecaySeconds() const {
    const float normalized = getParameter(kParamDecay);
    return 0.12f + (normalized * 2.88f);
}

float RumbleInstance::getDriveAmount() const { return getParameter(kParamDrive) * 2.5f; }

float RumbleInstance::getToneHz() const {
    const float normalized = getParameter(kParamTone);
    return 80.0f + normalized * 3920.0f;
}

float RumbleInstance::getOutputGainLinear() const {
    const float db = (getParameter(kParamOutputGain) * 24.0f) - 12.0f;
    return std::pow(10.0f, db / 20.0f);
}

void RumbleInstance::updateVoiceTuning() {
    const float frequency = midiNoteToFrequency(m_voice.note);
    m_voice.phaseIncrement = (2.0 * kPi * frequency) / m_sampleRate;
}

void RumbleInstance::updateEnvelopeRate() {
    const float decaySeconds = std::max(0.01f, getDecaySeconds());
    m_voice.releaseCoeff = std::exp(std::log(1.0e-4) / (decaySeconds * m_sampleRate));
}

void RumbleInstance::updateToneCoefficient() {
    const float cutoff = getToneHz();
    const float x = std::exp(-2.0f * static_cast<float>(kPi) * cutoff / static_cast<float>(m_sampleRate));
    m_filterAlpha = 1.0f - x;
}

float RumbleInstance::applyDrive(float input) const {
    const float drive = getDriveAmount();
    return std::tanh(input * (1.0f + drive * 3.0f));
}

float RumbleInstance::processToneFilter(float input) {
    m_filterState += m_filterAlpha * (input - m_filterState);
    return m_filterState;
}

} // namespace Plugins
} // namespace Aestra
