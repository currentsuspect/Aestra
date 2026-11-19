// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Rumble, the 808 instrument (FD-80, SPEC-025). Plays the voice the founder approved (RumbleVoice): each note
// is one finished one-shot, levelled to the same peak as every other note and every other sound.
#pragma once

#include "Plugin/PluginHost.h"
#include "RumbleVoice.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Aestra {
namespace Plugins {

class RumbleInstance : public Aestra::Audio::IPluginInstance {
public:
    RumbleInstance();
    ~RumbleInstance() override;

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

    // The whole sound, settings and click seed together. For the editor and for Find (SPEC-025).
    RumbleSound sound() const;
    void setSound(const RumbleSound& sound);

    // True once the 808 range (C1 to C4) is levelled for the current sound. For tests and a busy indicator.
    bool levelsReady() const;
    // Waits until levelsReady() or the timeout. Never from the audio thread.
    bool waitForLevels(int timeoutMs) const;

    // Keys sit 36 semitones above the pitch they play: the DAW's root note C3 (MIDI 60, Aestra's naming) plays
    // the sound at its home pitch C0 (MIDI 24), the way a sampler plays its root key (founder, 2026-11-12).
    static constexpr int kKeyShift = 36;
    static constexpr int pitchOfKey(int key) { return key - kKeyShift; }

    static constexpr float kDefaultVelocity = 0.5f;
    static const char* parameterKey(uint32_t id);
    // A sound on its own as a state loadState takes: the fifteen settings and the grain, no Velocity.
    static std::vector<uint8_t> stateOfSound(const RumbleSound& sound);
    // The sound in a state, or nothing when it is not a Rumble sound.
    static std::optional<RumbleSound> soundOfState(const std::vector<uint8_t>& state);
    static const char* parameterName(uint32_t id);

    // The sound's fifteen settings, then Velocity: how much a softer hit plays quieter. Velocity is how the sound
    // is played, not what it is, so it is not part of RumbleSound and Find never changes it.
    static constexpr uint32_t kVelocity = RumbleSound::Count;
    static constexpr uint32_t kParameterCount = RumbleSound::Count + 1;
    // The gain of a hit: 1 at full velocity; at Velocity 1 the softest hit is 30 dB down.
    static double velocityGain(double amount, int velocity);


private:
    void handleMidiEvent(const Aestra::Audio::MidiBuffer::Event& event);
    double levelFor(int note);
    void soundChanged();
    void levelWorker();
    int nextNoteToLevel(uint32_t version);

    std::atomic<bool> m_active{false};
    double m_sampleRate = 48000.0;

    std::array<std::atomic<float>, kParameterCount> m_params;
    std::atomic<uint32_t> m_seed{1};

    // Two voices: the one ringing, and the one fading out under a new note.
    RumbleVoice m_voice;
    RumbleVoice m_fading;
    std::array<bool, 128> m_held{};
    int m_heldCount = 0;

    // Note levels are found off the audio thread (RumbleVoice::rawPeak is milliseconds of work). The audio
    // thread only reads them; until a note's level for the current sound is ready it uses the nearest note
    // that is ready, and asks for its own.
    std::atomic<uint32_t> m_soundVersion{1};
    std::atomic<double> m_workerRate{48000.0};
    std::array<std::atomic<float>, 128> m_peak;
    std::array<std::atomic<uint32_t>, 128> m_peakVersion;
    std::array<std::atomic<bool>, 128> m_wanted;
    mutable std::mutex m_workerMutex;
    mutable std::condition_variable m_workerWake;
    mutable std::condition_variable m_levelsDone;
    bool m_workerStop = false;
    std::thread m_worker;
};

} // namespace Plugins
} // namespace Aestra
