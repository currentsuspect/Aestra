#include "RumbleInstance.h"

#include "AestraJSON.h"
#include "EntitlementStore.h"
#include "Plugin/BuiltInPlugins.h"
#include "RumblePluginRegistration.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTransientDecaySeconds = 0.012;
constexpr float kAmpAttackDefaultNormalized = 0.04081632653f; // 3 ms on a 1..50 ms mapping
constexpr float kC1Frequency = 32.70319566f;

constexpr uint32_t kRumbleStateMagic = 0x524D424Cu; // 'RMBL'
constexpr uint32_t kRumbleStateVersionV1 = 1;
constexpr uint32_t kRumbleStateVersionV2 = 2;

struct RumbleStateBlobV1 {
    uint32_t magic;
    uint32_t version;
    float decay;
    float drive;
    float tone;
    float outputGain;
};

struct RumbleStateBlobV2 {
    uint32_t magic;
    uint32_t version;
    float decay;
    float drive;
    float tone;
    float outputGain;
    float pitchAmount;
    float pitchDecay;
    float ampAttack;
    float resonance;
    float transientAmount;
};
} // namespace

namespace Aestra {
namespace Plugins {

RumbleInstance::RumbleInstance() {
    m_params[kParamAmpDecay].store(0.45f, std::memory_order_relaxed);
    m_params[kParamDrive].store(0.10f, std::memory_order_relaxed);
    m_params[kParamTone].store(0.30f, std::memory_order_relaxed);
    m_params[kParamOutputGain].store(0.50f, std::memory_order_relaxed);
    m_params[kParamPitchAmount].store(0.35f, std::memory_order_relaxed);
    m_params[kParamPitchDecay].store(0.25f, std::memory_order_relaxed);
    m_params[kParamPitchCurve].store(0.42f, std::memory_order_relaxed);
    m_params[kParamAmpAttack].store(kAmpAttackDefaultNormalized, std::memory_order_relaxed);
    m_params[kParamResonance].store(0.55f, std::memory_order_relaxed);
    m_params[kParamTransientAmount].store(0.30f, std::memory_order_relaxed);
    m_params[kParamClickLevel].store(0.25f, std::memory_order_relaxed);
    m_params[kParamClickDecay].store(0.20f, std::memory_order_relaxed);
    m_params[kParamClickTone].store(0.35f, std::memory_order_relaxed);
    m_params[kParamGlideTime].store(0.15f, std::memory_order_relaxed);
    m_params[kParamGlideCurve].store(0.50f, std::memory_order_relaxed);
    m_params[kParamGlideMode].store(0.0f, std::memory_order_relaxed);
    m_params[kParamRetriggerMode].store(0.0f, std::memory_order_relaxed);
    m_params[kParamFilterEnvAmount].store(0.50f, std::memory_order_relaxed);
    m_params[kParamFilterKeytrack].store(0.0f, std::memory_order_relaxed);
    m_params[kParamSatMode].store(0.0f, std::memory_order_relaxed);
    m_params[kParamVelocityToAmp].store(0.75f, std::memory_order_relaxed);
    m_params[kParamTune].store(0.50f, std::memory_order_relaxed);
    m_params[kParamFine].store(0.50f, std::memory_order_relaxed);

    if (!Aestra::License::EntitlementStore().canAccess(Aestra::License::ProductFeature::Rumble)) {
        m_licensed = false;
        return;
    }
    m_licensed = true;
}

bool RumbleInstance::initialize(double sampleRate, uint32_t maxBlockSize) {
    m_sampleRate = sampleRate > 1.0 ? sampleRate : 44100.0;
    m_maxBlockSize = maxBlockSize > 0 ? maxBlockSize : 512;
    m_sessionSeedMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
    m_processedSamples = 0;

    for (uint32_t i = 0; i < kParamCount; ++i) {
        m_smoothedParams[i] = getParameter(i);
    }

    m_parameterSmoothingCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(m_sampleRate * 0.005));
    m_oversampleAntiAliasAlpha =
        1.0f - std::exp(-2.0f * static_cast<float>(kPi) * static_cast<float>(m_sampleRate * 0.5f) /
                        static_cast<float>(m_sampleRate * 2.0f));
    m_dcBlockerR = 1.0f - static_cast<float>((2.0 * kPi * 5.0) / m_sampleRate);
    m_dcBlockerR = std::clamp(m_dcBlockerR, 0.0f, 0.9999f);
    m_smoothedSatMode = getParameter(kParamSatMode) >= 0.5f ? 1.0f : 0.0f;

    m_voice = {};
    m_voice.baseFrequency = midiNoteToFrequency(m_voice.note);
    m_voice.tunedFrequency = m_voice.baseFrequency;
    m_voice.currentEffectivePitch_hz = static_cast<float>(m_voice.baseFrequency);
    m_voice.glideSourcePitch_hz = static_cast<float>(m_voice.baseFrequency);
    m_voice.glideTargetPitch_hz = static_cast<float>(m_voice.baseFrequency);
    m_heldNotes.fill(false);
    m_heldVelocities.fill(0);
    m_heldNoteOrder.clear();
    resetVoiceProcessingState();
    return true;
}

void RumbleInstance::shutdown() {
    m_active.store(false, std::memory_order_release);
    m_voice = {};
    m_heldNotes.fill(false);
    m_heldVelocities.fill(0);
    m_heldNoteOrder.clear();
    resetVoiceProcessingState();
}

void RumbleInstance::activate() {
    m_active.store(true, std::memory_order_release);
    resetVoiceProcessingState();
    m_processedSamples = 0;
    m_heldNotes.fill(false);
    m_heldVelocities.fill(0);
    m_heldNoteOrder.clear();
}

void RumbleInstance::deactivate() {
    m_active.store(false, std::memory_order_release);
    m_voice.active = false;
    m_voice.noteIsHeld = false;
    m_heldNotes.fill(false);
    m_heldVelocities.fill(0);
    m_heldNoteOrder.clear();
}

void RumbleInstance::process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                             uint32_t numOutputChannels, uint32_t numFrames, const Aestra::Audio::MidiBuffer* midiInput,
                             Aestra::Audio::MidiBuffer* midiOutput) {
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

    if (!m_licensed) {
        return;
    }

    if (!isActive()) {
        return;
    }

    size_t eventIdx = 0;
    const size_t eventCount = midiInput ? midiInput->getEventCount() : 0;

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

        updateSmoothedParameters();

        if (!m_voice.active) {
            ++m_processedSamples;
            continue;
        }

        const float ampDecaySeconds = mapAmpDecaySeconds(m_smoothedParams[kParamAmpDecay]);
        const float driveAmount = mapDriveAmount(m_smoothedParams[kParamDrive]);
        const float toneHz = mapToneHz(m_smoothedParams[kParamTone]);
        const float outputGain = mapOutputGainLinear(m_smoothedParams[kParamOutputGain]);
        const float pitchAmountSemitones = mapPitchAmountSemitones(m_smoothedParams[kParamPitchAmount]);
        const float pitchDecaySeconds = mapPitchDecaySeconds(m_smoothedParams[kParamPitchDecay]);
        const float pitchCurveExponent = mapPitchCurveExponent(getParameter(kParamPitchCurve));
        const float resonanceQ = mapResonanceQ(m_smoothedParams[kParamResonance]);
        const float transientAmount = mapTransientAmount(m_smoothedParams[kParamTransientAmount]);
        const float clickLevel = mapClickLevel(getParameter(kParamClickLevel));
        const float clickToneHz = mapClickToneHz(m_smoothedParams[kParamClickTone]);
        const float glideTimeSeconds = mapGlideTimeSeconds(m_smoothedParams[kParamGlideTime]);
        const float filterEnvAmount = mapFilterEnvAmount(m_smoothedParams[kParamFilterEnvAmount]);
        const float filterKeytrack = mapFilterKeytrack(m_smoothedParams[kParamFilterKeytrack]);

        m_voice.decayCoeff = std::exp(std::log(1.0e-4) / (std::max(0.01f, ampDecaySeconds) * m_sampleRate));
        m_voice.clickCoeff =
            std::exp(std::log(1.0e-4) /
                     (std::max(0.001f, mapClickDecaySeconds(m_smoothedParams[kParamClickDecay])) * m_sampleRate));

        if (m_voice.attackActive) {
            m_voice.amplitudeEnvelope = std::min(1.0, m_voice.amplitudeEnvelope + m_voice.attackIncrement);
            if (m_voice.attackSamplesRemaining > 0) {
                --m_voice.attackSamplesRemaining;
            }
            if (m_voice.attackSamplesRemaining == 0 || m_voice.amplitudeEnvelope >= 1.0) {
                m_voice.amplitudeEnvelope = 1.0;
                m_voice.attackActive = false;
            }
        } else {
            m_voice.amplitudeEnvelope *= m_voice.decayCoeff;
        }

        if (m_voice.pitchDecayProgress < 1.0) {
            m_voice.pitchDecayProgress = std::min(1.0, m_voice.pitchDecayProgress + m_voice.pitchDecayIncrement);
        }
        const double pitchEnvelope =
            1.0 - std::pow(std::clamp(m_voice.pitchDecayProgress, 0.0, 1.0), static_cast<double>(pitchCurveExponent));

        m_voice.transientEnvelope *= m_voice.transientCoeff;
        m_voice.clickEnvelope *= m_voice.clickCoeff;

        const double effectiveSemitones = static_cast<double>(pitchAmountSemitones) * pitchEnvelope;
        const float pitchedBase = m_voice.glideTargetPitch_hz * std::pow(2.0, effectiveSemitones / 12.0);

        if (m_voice.isGliding) {
            const float glideStep = 1.0f / std::max(1.0f, glideTimeSeconds * static_cast<float>(m_sampleRate));
            m_voice.glideProgress = std::clamp(m_voice.glideProgress + glideStep, 0.0f, 1.0f);
            const float shaped = std::pow(m_voice.glideProgress, m_voice.glideCurveExponent);
            m_voice.currentEffectivePitch_hz =
                m_voice.glideSourcePitch_hz + shaped * (pitchedBase - m_voice.glideSourcePitch_hz);
            if (m_voice.glideProgress >= 1.0f) {
                m_voice.isGliding = false;
            }
        } else {
            m_voice.currentEffectivePitch_hz = pitchedBase;
        }

        m_voice.phaseIncrement = (2.0 * kPi * std::max(1.0e-6f, m_voice.currentEffectivePitch_hz)) / m_sampleRate;

        const float fundamental = static_cast<float>(std::sin(m_voice.phase));
        const float overtone = static_cast<float>(std::sin(m_voice.phase * 2.0));

        const float clickFilterAlpha =
            1.0f - std::exp(-2.0f * static_cast<float>(kPi) * clickToneHz / static_cast<float>(m_sampleRate));
        const float rawClickNoise = nextClickNoiseSample(m_voice.clickNoiseState);
        m_voice.clickFilterState += clickFilterAlpha * (rawClickNoise - m_voice.clickFilterState);
        const float clickSample = m_voice.clickFilterState * static_cast<float>(m_voice.clickEnvelope) * clickLevel;

        float sample = fundamental + overtone * static_cast<float>(m_voice.transientEnvelope) * transientAmount;

        m_voice.phase += m_voice.phaseIncrement;
        if (m_voice.phase >= (2.0 * kPi)) {
            m_voice.phase = std::fmod(m_voice.phase, 2.0 * kPi);
        }

        sample *= static_cast<float>(m_voice.amplitudeEnvelope) * m_voice.ampPeak;
        sample *= 0.58f;

        const float driveInput = sample * (1.0f + driveAmount * 3.0f);
        const float softDriven = processOversampledTanh(driveInput, 1.0f, m_driveStageSoft);
        const float hardMidpoint = 0.5f * (m_driveStageHard.previousInput + driveInput);
        const float hardMidClipped = std::clamp(hardMidpoint, -0.8f, 0.8f);
        m_driveStageHard.antiAliasState +=
            m_oversampleAntiAliasAlpha * (hardMidClipped - m_driveStageHard.antiAliasState);
        const float hardClipped = std::clamp(driveInput, -0.8f, 0.8f);
        m_driveStageHard.antiAliasState += m_oversampleAntiAliasAlpha * (hardClipped - m_driveStageHard.antiAliasState);
        m_driveStageHard.previousInput = driveInput;
        const float driven = softDriven + m_smoothedSatMode * (m_driveStageHard.antiAliasState - softDriven);

        const float semitoneOffset =
            12.0f * std::log2(std::max(static_cast<float>(m_voice.tunedFrequency), 1.0e-6f) / kC1Frequency);
        const float keytrackOffset = filterKeytrack * semitoneOffset * (toneHz / 12.0f);
        const float envOffset = filterEnvAmount * static_cast<float>(m_voice.amplitudeEnvelope) * 3920.0f;
        const float cutoffHz = toneHz + envOffset + keytrackOffset;
        sample = processFilter(driven, cutoffHz, resonanceQ);
        sample += clickSample;
        sample *= outputGain;
        sample = processOversampledTanh(sample, 0.9f, m_outputLimiterStage);
        sample = processDcBlocker(sample);

        if (!m_voice.attackActive && m_voice.amplitudeEnvelope < 1.0e-5 && m_voice.clickEnvelope < 1.0e-5 &&
            m_voice.transientEnvelope < 1.0e-5) {
            m_voice.active = false;
            m_voice.amplitudeEnvelope = 0.0;
            m_voice.clickEnvelope = 0.0;
            ++m_processedSamples;
            continue;
        }

        if (outputs[0]) {
            outputs[0][i] = sample;
        }
        if (numOutputChannels > 1 && outputs[1]) {
            outputs[1][i] = sample;
        }
        ++m_processedSamples;
    }
}

std::vector<Aestra::Audio::PluginParameter> RumbleInstance::getParameters() const {
    using Aestra::Audio::PluginParameter;
    return {
        PluginParameter{kParamAmpDecay, "AmpDecay", "AmpDec", "s", 0.45f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamDrive, "Drive", "Drive", "%", 0.10f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamTone, "Tone", "Tone", "Hz", 0.30f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamOutputGain, "OutputGain", "Gain", "dB", 0.50f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamPitchAmount, "PitchAmount", "Pitch", "st", 0.35f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamPitchDecay, "PitchDecay", "P Dec", "s", 0.25f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamPitchCurve, "PitchCurve", "P Curv", "", 0.42f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamAmpAttack, "AmpAttack", "Attack", "ms", kAmpAttackDefaultNormalized, 0.0f, 1.0f, true,
                        false, false, 0},
        PluginParameter{kParamResonance, "Resonance", "Reso", "Q", 0.55f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamTransientAmount, "TransientAmount", "Trans", "%", 0.30f, 0.0f, 1.0f, true, false, false,
                        0},
        PluginParameter{kParamClickLevel, "ClickLevel", "Click", "%", 0.25f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamClickDecay, "ClickDecay", "ClkDec", "s", 0.20f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamClickTone, "ClickTone", "ClkTon", "Hz", 0.35f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamGlideTime, "GlideTime", "Glide", "s", 0.15f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamGlideCurve, "GlideCurve", "GlCurv", "", 0.50f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamGlideMode, "GlideMode", "GlMode", "", 0.0f, 0.0f, 1.0f, true, false, false, 1},
        PluginParameter{kParamRetriggerMode, "RetriggerMode", "RtMode", "", 0.0f, 0.0f, 1.0f, true, false, false, 1},
        PluginParameter{kParamFilterEnvAmount, "FilterEnvAmount", "F Env", "", 0.50f, 0.0f, 1.0f, true, false, false,
                        0},
        PluginParameter{kParamFilterKeytrack, "FilterKeytrack", "F Key", "%", 0.0f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamSatMode, "SatMode", "SatMd", "", 0.0f, 0.0f, 1.0f, true, false, false, 1},
        PluginParameter{kParamVelocityToAmp, "VelocityToAmp", "VelAmp", "%", 0.75f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamTune, "Tune", "Tune", "st", 0.50f, 0.0f, 1.0f, true, false, false, 0},
        PluginParameter{kParamFine, "Fine", "Fine", "ct", 0.50f, 0.0f, 1.0f, true, false, false, 0},
    };
}

uint32_t RumbleInstance::getParameterCount() const {
    return kParamCount;
}

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

    float clamped = clamp01(value);
    if (id == kParamGlideMode || id == kParamRetriggerMode || id == kParamSatMode) {
        clamped = clamped >= 0.5f ? 1.0f : 0.0f;
    }
    m_params[id].store(clamped, std::memory_order_relaxed);
}

std::string RumbleInstance::getParameterDisplay(uint32_t id) const {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);

    switch (id) {
    case kParamAmpDecay:
        out << mapAmpDecaySeconds(getParameter(kParamAmpDecay)) << " s";
        break;
    case kParamDrive:
        out << (mapDriveAmount(getParameter(kParamDrive)) * 100.0f) << "%";
        break;
    case kParamTone:
        out << mapToneHz(getParameter(kParamTone)) << " Hz";
        break;
    case kParamOutputGain:
        out << ((getParameter(kParamOutputGain) * 24.0f) - 12.0f) << " dB";
        break;
    case kParamPitchAmount:
        out << mapPitchAmountSemitones(getParameter(kParamPitchAmount)) << " st";
        break;
    case kParamPitchDecay:
        out << mapPitchDecaySeconds(getParameter(kParamPitchDecay)) << " s";
        break;
    case kParamPitchCurve:
        out << mapPitchCurveExponent(getParameter(kParamPitchCurve)) << "x";
        break;
    case kParamAmpAttack:
        out << (mapAmpAttackSeconds(getParameter(kParamAmpAttack)) * 1000.0f) << " ms";
        break;
    case kParamResonance:
        out << mapResonanceQ(getParameter(kParamResonance)) << " Q";
        break;
    case kParamTransientAmount:
        out << (mapTransientAmount(getParameter(kParamTransientAmount)) * 100.0f) << "%";
        break;
    case kParamClickLevel:
        out << (mapClickLevel(getParameter(kParamClickLevel)) * 100.0f) << "%";
        break;
    case kParamClickDecay:
        out << (mapClickDecaySeconds(getParameter(kParamClickDecay)) * 1000.0f) << " ms";
        break;
    case kParamClickTone:
        out << mapClickToneHz(getParameter(kParamClickTone)) << " Hz";
        break;
    case kParamGlideTime:
        out << mapGlideTimeSeconds(getParameter(kParamGlideTime)) << " s";
        break;
    case kParamGlideCurve:
        out << mapGlideCurveExponent(getParameter(kParamGlideCurve)) << "x";
        break;
    case kParamGlideMode:
        return getParameter(kParamGlideMode) >= 0.5f ? "Legato" : "Always";
    case kParamRetriggerMode:
        return getParameter(kParamRetriggerMode) >= 0.5f ? "Legato" : "Retrigger";
    case kParamFilterEnvAmount:
        out << mapFilterEnvAmount(getParameter(kParamFilterEnvAmount));
        break;
    case kParamFilterKeytrack:
        out << (mapFilterKeytrack(getParameter(kParamFilterKeytrack)) * 100.0f) << "%";
        break;
    case kParamSatMode:
        return getParameter(kParamSatMode) >= 0.5f ? "Hard" : "Soft";
    case kParamVelocityToAmp:
        out << (mapVelocityToAmp(getParameter(kParamVelocityToAmp)) * 100.0f) << "%";
        break;
    case kParamTune:
        out << mapTuneSemitones(getParameter(kParamTune)) << " st";
        break;
    case kParamFine:
        out << mapFineCents(getParameter(kParamFine)) << " ct";
        break;
    default:
        return "0";
    }
    return out.str();
}

std::vector<uint8_t> RumbleInstance::saveState() const {
    Aestra::JSON json = Aestra::JSON::object();
    json.set("schema", Aestra::JSON("rumble-preset"));
    json.set("version", Aestra::JSON(1.0));
    json.set("name", Aestra::JSON("Untitled"));
    json.set("author", Aestra::JSON(""));
    Aestra::JSON tags = Aestra::JSON::array();
    json.set("tags", tags);

    Aestra::JSON params = Aestra::JSON::object();
    for (uint32_t i = 0; i < kParamCount; ++i) {
        params.set(getParameterKey(i), Aestra::JSON(static_cast<double>(getParameter(i))));
    }
    json.set("params", params);

    const std::string state = json.toString(2);
    return std::vector<uint8_t>(state.begin(), state.end());
}

bool RumbleInstance::loadState(const std::vector<uint8_t>& state) {
    if (state.size() >= sizeof(uint32_t) * 2) {
        uint32_t magic = 0;
        uint32_t version = 0;
        std::memcpy(&magic, state.data(), sizeof(uint32_t));
        std::memcpy(&version, state.data() + sizeof(uint32_t), sizeof(uint32_t));

        if (magic == kRumbleStateMagic) {
            if (version == kRumbleStateVersionV2 && state.size() == sizeof(RumbleStateBlobV2)) {
                RumbleStateBlobV2 blob{};
                std::memcpy(&blob, state.data(), sizeof(blob));

                setParameter(kParamAmpDecay, blob.decay);
                setParameter(kParamDrive, blob.drive);
                setParameter(kParamTone, blob.tone);
                setParameter(kParamOutputGain, blob.outputGain);
                setParameter(kParamPitchAmount, blob.pitchAmount);
                setParameter(kParamPitchDecay, blob.pitchDecay);
                setParameter(kParamPitchCurve, 0.42f);
                setParameter(kParamAmpAttack, blob.ampAttack);
                setParameter(kParamResonance, blob.resonance);
                setParameter(kParamTransientAmount, blob.transientAmount);
                setParameter(kParamClickLevel, 0.25f);
                setParameter(kParamClickDecay, 0.20f);
                setParameter(kParamClickTone, 0.35f);
                setParameter(kParamGlideTime, 0.15f);
                setParameter(kParamGlideCurve, 0.50f);
                setParameter(kParamGlideMode, 0.0f);
                setParameter(kParamRetriggerMode, 0.0f);
                setParameter(kParamFilterEnvAmount, 0.50f);
                setParameter(kParamFilterKeytrack, 0.0f);
                setParameter(kParamSatMode, 0.0f);
                setParameter(kParamVelocityToAmp, 0.75f);
                setParameter(kParamTune, 0.50f);
                setParameter(kParamFine, 0.50f);
                return true;
            }

            if (version == kRumbleStateVersionV1 && state.size() == sizeof(RumbleStateBlobV1)) {
                RumbleStateBlobV1 blob{};
                std::memcpy(&blob, state.data(), sizeof(blob));

                setParameter(kParamAmpDecay, blob.decay);
                setParameter(kParamDrive, blob.drive);
                setParameter(kParamTone, blob.tone);
                setParameter(kParamOutputGain, blob.outputGain);
                setParameter(kParamPitchAmount, 0.35f);
                setParameter(kParamPitchDecay, 0.25f);
                setParameter(kParamPitchCurve, 0.42f);
                setParameter(kParamAmpAttack, kAmpAttackDefaultNormalized);
                setParameter(kParamResonance, 0.55f);
                setParameter(kParamTransientAmount, 0.30f);
                setParameter(kParamClickLevel, 0.25f);
                setParameter(kParamClickDecay, 0.20f);
                setParameter(kParamClickTone, 0.35f);
                setParameter(kParamGlideTime, 0.15f);
                setParameter(kParamGlideCurve, 0.50f);
                setParameter(kParamGlideMode, 0.0f);
                setParameter(kParamRetriggerMode, 0.0f);
                setParameter(kParamFilterEnvAmount, 0.50f);
                setParameter(kParamFilterKeytrack, 0.0f);
                setParameter(kParamSatMode, 0.0f);
                setParameter(kParamVelocityToAmp, 0.75f);
                setParameter(kParamTune, 0.50f);
                setParameter(kParamFine, 0.50f);
                return true;
            }
            return false;
        }
    }

    const std::string jsonString(state.begin(), state.end());
    Aestra::JSON json = Aestra::JSON::parse(jsonString);
    if (!json.isObject() || !json.has("schema") || !json["schema"].isString() ||
        json["schema"].asString() != "rumble-preset" || !json.has("params") || !json["params"].isObject()) {
        return false;
    }

    auto& params = json["params"];
    for (uint32_t i = 0; i < kParamCount; ++i) {
        const char* key = getParameterKey(i);
        if (params.has(key) && params[key].isNumber()) {
            setParameter(i, static_cast<float>(params[key].asNumber()));
        }
    }
    return true;
}

const Aestra::Audio::PluginInfo& RumbleInstance::getInfo() const {
    return rumblePluginInfo();
}

uint32_t RumbleInstance::getTailSamples() const {
    const float attackSeconds = mapAmpAttackSeconds(getParameter(kParamAmpAttack));
    const float ampDecaySeconds = mapAmpDecaySeconds(getParameter(kParamAmpDecay));
    const float clickDecaySeconds = mapClickDecaySeconds(getParameter(kParamClickDecay));
    return static_cast<uint32_t>(
        std::ceil((attackSeconds + std::max(ampDecaySeconds, clickDecaySeconds)) * m_sampleRate));
}

void RumbleInstance::handleMidiEvent(const Aestra::Audio::MidiBuffer::Event& event) {
    if (event.size < 3) {
        return;
    }

    const uint8_t status = event.data[0] & 0xF0;
    const uint8_t note = event.data[1];
    const uint8_t velocity = event.data[2];

    if (status == 0x90 && velocity > 0) {
        const bool hadHeldNote = m_voice.noteIsHeld;
        m_heldNotes[note] = true;
        m_heldVelocities[note] = velocity;
        m_heldNoteOrder.erase(std::remove(m_heldNoteOrder.begin(), m_heldNoteOrder.end(), note), m_heldNoteOrder.end());
        m_heldNoteOrder.push_back(note);

        m_voice.glideSourcePitch_hz = m_voice.currentEffectivePitch_hz > 0.0f
                                          ? m_voice.currentEffectivePitch_hz
                                          : static_cast<float>(tunedNoteFrequency(note));
        m_voice.glideTargetPitch_hz = tunedNoteFrequency(note);
        m_voice.baseFrequency = midiNoteToFrequency(note);
        m_voice.tunedFrequency = m_voice.glideTargetPitch_hz;

        const bool glideAlways = getParameter(kParamGlideMode) < 0.5f;
        const bool glideActive = glideAlways || (hadHeldNote && getParameter(kParamGlideMode) >= 0.5f);
        if (glideActive) {
            m_voice.isGliding = true;
            m_voice.glideProgress = 0.0f;
        } else {
            m_voice.isGliding = false;
            m_voice.glideProgress = 1.0f;
            m_voice.glideSourcePitch_hz = m_voice.glideTargetPitch_hz;
        }

        m_voice.glideCurveExponent = mapGlideCurveExponent(getParameter(kParamGlideCurve));
        const bool resetEnvelopes = getParameter(kParamRetriggerMode) < 0.5f;
        beginNote(note, velocity, resetEnvelopes);
        m_voice.noteIsHeld = true;
    } else if (status == 0x80 || (status == 0x90 && velocity == 0)) {
        handleNoteOff(note);
    }
}

void RumbleInstance::beginNote(uint8_t note, uint8_t velocity, bool resetEnvelopes) {
    const bool wasActive = m_voice.active;
    m_voice.active = true;
    m_voice.note = note;
    m_voice.velocity = velocity / 127.0f;
    m_voice.baseFrequency = midiNoteToFrequency(note);
    m_voice.tunedFrequency = m_voice.glideTargetPitch_hz;
    if (!wasActive) {
        m_voice.phase = 0.0;
    }
    m_voice.phaseIncrement = (2.0 * kPi * m_voice.currentEffectivePitch_hz) / m_sampleRate;
    if (resetEnvelopes || !wasActive) {
        m_voice.amplitudeEnvelope = 0.0;
        m_voice.pitchDecayProgress = 0.0;
    }
    m_voice.transientEnvelope = 1.0;
    m_voice.clickEnvelope = 1.0;

    if (resetEnvelopes || !wasActive) {
        const float attackSeconds = mapAmpAttackSeconds(m_smoothedParams[kParamAmpAttack]);
        const uint32_t attackSamples =
            std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(attackSeconds * m_sampleRate)));
        m_voice.attackSamplesRemaining = attackSamples;
        m_voice.attackIncrement = 1.0 / static_cast<double>(attackSamples);
        m_voice.attackActive = true;
    }

    m_voice.decayCoeff = std::exp(
        std::log(1.0e-4) / (std::max(0.01f, mapAmpDecaySeconds(m_smoothedParams[kParamAmpDecay])) * m_sampleRate));
    if (resetEnvelopes || !wasActive) {
        m_voice.pitchDecayIncrement = 1.0 / std::max(1.0f, mapPitchDecaySeconds(m_smoothedParams[kParamPitchDecay]) *
                                                               static_cast<float>(m_sampleRate));
    }
    m_voice.transientCoeff = std::exp(std::log(1.0e-4) / (kTransientDecaySeconds * m_sampleRate));
    m_voice.clickCoeff = std::exp(
        std::log(1.0e-4) / (std::max(0.001f, mapClickDecaySeconds(m_smoothedParams[kParamClickDecay])) * m_sampleRate));
    const float velocityToAmp = mapVelocityToAmp(getParameter(kParamVelocityToAmp));
    m_voice.ampPeak = 1.0f - velocityToAmp + velocityToAmp * m_voice.velocity;

    const uint64_t timestampMs = m_sessionSeedMs + (m_processedSamples * 1000ULL) / static_cast<uint64_t>(m_sampleRate);
    m_voice.clickNoiseState = seedClickNoise(note, timestampMs);
    m_voice.currentEffectivePitch_hz = wasActive ? m_voice.currentEffectivePitch_hz : m_voice.glideTargetPitch_hz;
    m_voice.noteIsHeld = std::any_of(m_heldNotes.begin(), m_heldNotes.end(), [](bool held) { return held; });
}

void RumbleInstance::handleNoteOff(uint8_t note) {
    m_heldNotes[note] = false;
    m_heldVelocities[note] = 0;
    m_heldNoteOrder.erase(std::remove(m_heldNoteOrder.begin(), m_heldNoteOrder.end(), note), m_heldNoteOrder.end());
    m_voice.noteIsHeld = std::any_of(m_heldNotes.begin(), m_heldNotes.end(), [](bool held) { return held; });

    if (getParameter(kParamGlideMode) >= 0.5f && note == m_voice.note && m_voice.noteIsHeld) {
        const uint8_t heldNote = getMostRecentHeldNote();
        const uint8_t heldVelocity = std::max<uint8_t>(1, m_heldVelocities[heldNote]);
        const bool hadHeldNote = m_voice.noteIsHeld;
        m_voice.glideSourcePitch_hz = m_voice.currentEffectivePitch_hz;
        m_voice.glideTargetPitch_hz = tunedNoteFrequency(heldNote);
        m_voice.baseFrequency = midiNoteToFrequency(heldNote);
        m_voice.tunedFrequency = m_voice.glideTargetPitch_hz;
        const bool glideAlways = getParameter(kParamGlideMode) < 0.5f;
        const bool glideActive = glideAlways || (hadHeldNote && getParameter(kParamGlideMode) >= 0.5f);
        if (glideActive) {
            m_voice.isGliding = true;
            m_voice.glideProgress = 0.0f;
        } else {
            m_voice.isGliding = false;
            m_voice.glideProgress = 1.0f;
            m_voice.glideSourcePitch_hz = m_voice.glideTargetPitch_hz;
        }
        m_voice.glideCurveExponent = mapGlideCurveExponent(getParameter(kParamGlideCurve));
        const bool resetEnvelopes = getParameter(kParamRetriggerMode) < 0.5f;
        beginNote(heldNote, heldVelocity, resetEnvelopes);
    }
}

void RumbleInstance::resetVoiceProcessingState() {
    m_voice.filterIc1Eq = 0.0f;
    m_voice.filterIc2Eq = 0.0f;
    m_voice.clickFilterState = 0.0f;
    m_voice.dcInputPrev = 0.0f;
    m_voice.dcOutputPrev = 0.0f;
    m_driveStageSoft = {};
    m_driveStageHard = {};
    m_outputLimiterStage = {};
}

void RumbleInstance::updateSmoothedParameters() {
    for (uint32_t i = 0; i < kParamCount; ++i) {
        const float target = m_params[i].load(std::memory_order_relaxed);
        switch (i) {
        case kParamAmpDecay:
        case kParamDrive:
        case kParamTone:
        case kParamOutputGain:
        case kParamPitchAmount:
        case kParamPitchDecay:
        case kParamAmpAttack:
        case kParamResonance:
        case kParamTransientAmount:
        case kParamClickDecay:
        case kParamClickTone:
        case kParamGlideTime:
        case kParamFilterEnvAmount:
        case kParamFilterKeytrack:
            m_smoothedParams[i] += (target - m_smoothedParams[i]) * m_parameterSmoothingCoeff;
            break;
        default:
            m_smoothedParams[i] = target;
            break;
        }
    }

    const float satTarget = m_params[kParamSatMode].load(std::memory_order_relaxed) >= 0.5f ? 1.0f : 0.0f;
    m_smoothedSatMode += (satTarget - m_smoothedSatMode) * m_parameterSmoothingCoeff;
}

const char* RumbleInstance::getParameterKey(uint32_t id) {
    switch (id) {
    case kParamAmpDecay:
        return "AmpDecay";
    case kParamDrive:
        return "Drive";
    case kParamTone:
        return "Tone";
    case kParamOutputGain:
        return "OutputGain";
    case kParamPitchAmount:
        return "PitchAmount";
    case kParamPitchDecay:
        return "PitchDecay";
    case kParamPitchCurve:
        return "PitchCurve";
    case kParamAmpAttack:
        return "AmpAttack";
    case kParamResonance:
        return "Resonance";
    case kParamTransientAmount:
        return "TransientAmount";
    case kParamClickLevel:
        return "ClickLevel";
    case kParamClickDecay:
        return "ClickDecay";
    case kParamClickTone:
        return "ClickTone";
    case kParamGlideTime:
        return "GlideTime";
    case kParamGlideCurve:
        return "GlideCurve";
    case kParamGlideMode:
        return "GlideMode";
    case kParamRetriggerMode:
        return "RetriggerMode";
    case kParamFilterEnvAmount:
        return "FilterEnvAmount";
    case kParamFilterKeytrack:
        return "FilterKeytrack";
    case kParamSatMode:
        return "SatMode";
    case kParamVelocityToAmp:
        return "VelocityToAmp";
    case kParamTune:
        return "Tune";
    case kParamFine:
        return "Fine";
    default:
        return "";
    }
}

float RumbleInstance::midiNoteToFrequency(uint8_t note) {
    return 440.0f * std::pow(2.0f, (static_cast<int>(note) - 69) / 12.0f);
}

float RumbleInstance::clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

uint32_t RumbleInstance::seedClickNoise(uint8_t note, uint64_t timestampMs) {
    uint64_t seed = (static_cast<uint64_t>(note) << 32) ^ timestampMs ^ 0x9e3779b97f4a7c15ULL;
    uint32_t state = static_cast<uint32_t>((seed >> 32) ^ (seed & 0xffffffffULL));
    if (state == 0) {
        state = 0x12345678U;
    }
    return state;
}

float RumbleInstance::nextClickNoiseSample(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    const float normalized = static_cast<float>(state & 0x00ffffffU) / static_cast<float>(0x00800000U);
    return normalized - 1.0f;
}

float RumbleInstance::mapAmpDecaySeconds(float normalized) const {
    return 0.12f + normalized * 2.88f;
}

float RumbleInstance::mapDriveAmount(float normalized) const {
    return normalized * 2.5f;
}

float RumbleInstance::mapToneHz(float normalized) const {
    return 80.0f + normalized * 3920.0f;
}

float RumbleInstance::mapOutputGainLinear(float normalized) const {
    const float db = (normalized * 24.0f) - 12.0f;
    return std::pow(10.0f, db / 20.0f);
}

float RumbleInstance::mapPitchAmountSemitones(float normalized) const {
    return normalized * 48.0f;
}

float RumbleInstance::mapPitchDecaySeconds(float normalized) const {
    return 0.01f + normalized * 1.99f;
}

float RumbleInstance::mapPitchCurveExponent(float normalized) const {
    return 0.2f + normalized * 3.8f;
}

float RumbleInstance::mapAmpAttackSeconds(float normalized) const {
    return 0.001f + normalized * 0.049f;
}

float RumbleInstance::mapResonanceQ(float normalized) const {
    return 0.5f + normalized * 7.5f;
}

float RumbleInstance::mapTransientAmount(float normalized) const {
    return normalized;
}

float RumbleInstance::mapClickLevel(float normalized) const {
    return normalized;
}

float RumbleInstance::mapClickDecaySeconds(float normalized) const {
    return 0.001f + normalized * 0.029f;
}

float RumbleInstance::mapClickToneHz(float normalized) const {
    return 800.0f + normalized * 7200.0f;
}

float RumbleInstance::mapGlideTimeSeconds(float normalized) const {
    return 0.01f + normalized * 0.99f;
}

float RumbleInstance::mapGlideCurveExponent(float normalized) const {
    return 0.2f + normalized * 3.8f;
}

float RumbleInstance::mapFilterEnvAmount(float normalized) const {
    return (normalized * 2.0f) - 1.0f;
}

float RumbleInstance::mapFilterKeytrack(float normalized) const {
    return normalized;
}

float RumbleInstance::mapVelocityToAmp(float normalized) const {
    return normalized;
}

float RumbleInstance::mapTuneSemitones(float normalized) const {
    return (normalized * 48.0f) - 24.0f;
}

float RumbleInstance::mapFineCents(float normalized) const {
    return (normalized * 200.0f) - 100.0f;
}

float RumbleInstance::tunedNoteFrequency(uint8_t note) const {
    const double base = midiNoteToFrequency(note);
    const double tuneSemitones = static_cast<double>(mapTuneSemitones(getParameter(kParamTune)));
    const double fineSemitones = static_cast<double>(mapFineCents(getParameter(kParamFine))) / 100.0;
    return static_cast<float>(base * std::pow(2.0, (tuneSemitones + fineSemitones) / 12.0));
}

uint8_t RumbleInstance::getMostRecentHeldNote() const {
    return m_heldNoteOrder.empty() ? m_voice.note : m_heldNoteOrder.back();
}

float RumbleInstance::processFilter(float input, float cutoffHz, float resonanceQ) {
    const float clampedCutoff = std::clamp(cutoffHz, 20.0f, static_cast<float>(m_sampleRate * 0.45f));
    const float g = std::tan(static_cast<float>(kPi) * clampedCutoff / static_cast<float>(m_sampleRate));
    const float q = std::max(0.5f, resonanceQ);
    const float damping = 1.0f / (2.0f * q);
    const float denom = 1.0f + 2.0f * damping * g + g * g;
    const float hp = (input - (2.0f * damping + g) * m_voice.filterIc1Eq - m_voice.filterIc2Eq) / denom;
    const float bp = g * hp + m_voice.filterIc1Eq;
    const float lp = g * bp + m_voice.filterIc2Eq;

    m_voice.filterIc1Eq = g * hp + bp;
    m_voice.filterIc2Eq = g * bp + lp;
    return lp;
}

float RumbleInstance::processDcBlocker(float input) {
    const float output = input - m_voice.dcInputPrev + m_dcBlockerR * m_voice.dcOutputPrev;
    m_voice.dcInputPrev = input;
    m_voice.dcOutputPrev = output;
    return output;
}

float RumbleInstance::processOversampledTanh(float input, float drive, OversampledTanhStage& stage) {
    const float midpoint = 0.5f * (stage.previousInput + input);

    const float nonlinearMid = std::tanh(midpoint * drive);
    stage.antiAliasState += m_oversampleAntiAliasAlpha * (nonlinearMid - stage.antiAliasState);

    const float nonlinearCurrent = std::tanh(input * drive);
    stage.antiAliasState += m_oversampleAntiAliasAlpha * (nonlinearCurrent - stage.antiAliasState);

    stage.previousInput = input;
    return stage.antiAliasState;
}

} // namespace Plugins
} // namespace Aestra
