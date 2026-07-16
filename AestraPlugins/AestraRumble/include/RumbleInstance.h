#pragma once

#include "Plugin/PluginHost.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Plugins {

class RumbleInstance : public Aestra::Audio::IPluginInstance {
public:
    RumbleInstance();
#if defined(AESTRA_ENABLE_TEST_LICENSES) && AESTRA_ENABLE_TEST_LICENSES
    enum class TestLicense { GrantRumble };
    explicit RumbleInstance(TestLicense);
#endif
    ~RumbleInstance() override = default;

    bool initialize(double sampleRate, uint32_t maxBlockSize) override;
    void shutdown() override;
    void activate() override;
    void deactivate() override;
    bool isActive() const override { return m_active.load(std::memory_order_acquire); }

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const Aestra::Audio::MidiBuffer* midiInput = nullptr,
                 Aestra::Audio::MidiBuffer* midiOutput = nullptr) override;

    std::vector<Aestra::Audio::PluginParameter> getParameters() const override;
    uint32_t getParameterCount() const override;
    float getParameter(uint32_t id) const override;
    void setParameter(uint32_t id, float value) override;
    std::string getParameterDisplay(uint32_t id) const override;

    std::vector<uint8_t> saveState() const override;
    bool loadState(const std::vector<uint8_t>& state) override;

    bool hasEditor() const override { return false; }
    bool openEditor(void* parentWindow) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {720, 480}; }
    bool resizeEditor(int width, int height) override { return false; }

    const Aestra::Audio::PluginInfo& getInfo() const override;
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override;

    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

private:
    void initializeDefaultParameters();

    enum ParamID : uint32_t {
        kParamAmpDecay = 0,
        kParamDrive,
        kParamTone,
        kParamOutputGain,
        kParamPitchAmount,
        kParamPitchDecay,
        kParamPitchCurve,
        kParamAmpAttack,
        kParamResonance,
        kParamTransientAmount,
        kParamClickLevel,
        kParamClickDecay,
        kParamClickTone,
        kParamGlideTime,
        kParamGlideCurve,
        kParamGlideMode,
        kParamRetriggerMode,
        kParamFilterEnvAmount,
        kParamFilterKeytrack,
        kParamSatMode,
        kParamVelocityToAmp,
        kParamTune,
        kParamFine,
        kParamHarmonics,
        kParamSubClean,
        kParamCount,
    };

    struct PendingNote {
        uint8_t note = 36;
        float velocity = 1.0f;
    };

    struct OversampledTanhStage {
        float previousInput = 0.0f;
        float antiAliasState = 0.0f;
    };

    struct Voice {
        bool active = false;
        uint8_t note = 36;
        float velocity = 1.0f;
        float ampPeak = 1.0f;
        double baseFrequency = 65.406391;
        double tunedFrequency = 65.406391;
        float currentEffectivePitch_hz = 65.406391f;
        float glideSourcePitch_hz = 65.406391f;
        float glideTargetPitch_hz = 65.406391f;
        float glideProgress = 1.0f;
        float glideCurveExponent = 1.0f;
        bool isGliding = false;
        bool noteIsHeld = false;
        double phase = 0.0;
        double phaseIncrement = 0.0;
        double resonatorMagnitude = 0.0;
        double amplitudeEnvelope = 0.0;
        double decayCoeff = 0.0;
        double pitchDecayProgress = 1.0;
        double pitchDecayIncrement = 0.0;
        double attackPitchEnvelope = 0.0;
        double attackPitchCoeff = 0.0;
        double transientEnvelope = 0.0;
        double transientCoeff = 0.0;
        double clickEnvelope = 0.0;
        double clickCoeff = 0.0;
        bool attackActive = false;
        uint32_t attackSamplesRemaining = 0;
        double attackIncrement = 0.0;
        float filterIc1Eq = 0.0f;
        float filterIc2Eq = 0.0f;
        float filterCachedCutoffHz = -1.0f;
        float filterCachedQ = -1.0f;
        float filterCachedG = 0.0f;
        float filterCachedDamping = 1.0f;
        float clickFilterState = 0.0f;
        float clickSlowFilterState = 0.0f;
        float dcInputPrev = 0.0f;
        float dcOutputPrev = 0.0f;
        uint32_t clickNoiseState = 0;
    };

    struct DspParameters {
        float driveAmount = 0.0f;
        float driveMix = 0.0f;
        float toneHz = 1000.0f;
        float outputGain = 1.0f;
        float pitchAmountSemitones = 0.0f;
        float pitchCurveExponent = 1.0f;
        float resonanceQ = 0.5f;
        float transientAmount = 0.0f;
        float clickLevel = 0.0f;
        float clickToneHz = 1000.0f;
        float clickFilterAlpha = 0.0f;
        float glideTimeSeconds = 0.01f;
        float filterEnvAmount = 0.0f;
        float filterKeytrack = 0.0f;
        float harmonics = 0.0f;
        float subClean = 1.0f;
        float decayCoeff = 0.0f;
        float clickCoeff = 0.0f;
        float satMode = 0.0f;
    };

    void handleMidiEvent(const Aestra::Audio::MidiBuffer::Event& event);
    void beginNote(uint8_t note, uint8_t velocity, bool resetEnvelopes);
    void handleNoteOff(uint8_t note);
    void rememberHeldNote(uint8_t note);
    void forgetHeldNote(uint8_t note);
    void resetVoiceProcessingState();
    void refreshParameterTargets();
    bool updateSmoothedParameters();
    DspParameters mapDspParameters() const;
    static bool isSmoothedParameter(uint32_t id);
    static const char* getParameterKey(uint32_t id);
    static float midiNoteToFrequency(uint8_t note);
    static float clamp01(float value);
    static uint32_t seedClickNoise(uint8_t note, uint64_t samplePosition, uint32_t triggerIndex);
    static float nextClickNoiseSample(uint32_t& state);
    float mapAmpDecaySeconds(float normalized) const;
    float mapDriveAmount(float normalized) const;
    float mapToneHz(float normalized) const;
    float mapOutputGainLinear(float normalized) const;
    float mapPitchAmountSemitones(float normalized) const;
    float mapPitchDecaySeconds(float normalized) const;
    float mapPitchCurveExponent(float normalized) const;
    float mapAmpAttackSeconds(float normalized) const;
    float mapResonanceQ(float normalized) const;
    float mapTransientAmount(float normalized) const;
    float mapClickLevel(float normalized) const;
    float mapClickDecaySeconds(float normalized) const;
    float mapClickToneHz(float normalized) const;
    float mapGlideTimeSeconds(float normalized) const;
    float mapGlideCurveExponent(float normalized) const;
    float mapFilterEnvAmount(float normalized) const;
    float mapFilterKeytrack(float normalized) const;
    float mapVelocityToAmp(float normalized) const;
    float mapTuneSemitones(float normalized) const;
    float mapFineCents(float normalized) const;
    float tunedNoteFrequency(uint8_t note) const;
    uint8_t getMostRecentHeldNote() const;
    float processFilter(float input, float cutoffHz, float resonanceQ);
    float processDcBlocker(float input);
    float processOversampledTanh(float input, float drive, OversampledTanhStage& stage);
    static float processSafetyKnee(float input);

    std::atomic<bool> m_active{false};
    double m_sampleRate = 44100.0;
    uint32_t m_maxBlockSize = 512;

    std::array<std::atomic<float>, kParamCount> m_params;
    std::array<float, kParamCount> m_smoothedParams{};
    std::array<float, kParamCount> m_parameterTargets{};
    Voice m_voice;
    bool m_licensed = true;
    OversampledTanhStage m_driveStageSoft;
    OversampledTanhStage m_driveStageHard;
    float m_parameterSmoothingCoeff = 0.0f;
    float m_oversampleAntiAliasAlpha = 0.0f;
    float m_dcBlockerR = 0.0f;
    float m_smoothedSatMode = 0.0f;
    bool m_parameterSmoothingActive = false;
    uint64_t m_processedSamples = 0;
    uint32_t m_triggerIndex = 0;
    std::array<bool, 128> m_heldNotes{};
    std::array<uint8_t, 128> m_heldVelocities{};
    std::array<uint8_t, 128> m_heldNoteOrder{};
    uint8_t m_heldNoteCount = 0;
};

} // namespace Plugins
} // namespace Aestra
