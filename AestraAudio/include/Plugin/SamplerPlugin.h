// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"
#include "Plugin/KeepLengthCache.h"

#include <array>
#include <atomic>
#include <memory>
#include "AestraAtomicSharedPtr.h"
#include "Core/RealtimeSharedSlot.h"
#include <mutex>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Plugins {

class SamplerPlugin : public IPluginInstance {
public:
    // Values are persisted in sampler state; append new modes rather than renumbering these.
    enum class LoopMode : int { OneShot = 0, Forward = 1, PingPong = 2 };
    // Persisted; append rather than renumber. Resample: pitch changes speed (tape-style).
    // KeepLength: pitch without the speed change (a pre-rendered time-stretched copy per semitone).
    enum class PitchMode : int { Resample = 0, KeepLength = 1 };

    SamplerPlugin();
    ~SamplerPlugin() override = default;

    // IPluginInstance Implementation
    bool initialize(double sampleRate, uint32_t maxBlockSize) override;
    void shutdown() override;
    void activate() override;
    void deactivate() override;
    bool isActive() const override { return m_active; }

    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* midiInput = nullptr, MidiBuffer* midiOutput = nullptr) override;

    std::vector<PluginParameter> getParameters() const override;
    uint32_t getParameterCount() const override;
    float getParameter(uint32_t id) const override;
    void setParameter(uint32_t id, float value) override;
    /// Render-thread automation contract (V8-A5): m_params is atomic storage.
    bool supportsRealtimeAutomation() const noexcept override { return true; }
    void applyAutomation(uint32_t id, float normalizedValue) noexcept override { setParameter(id, normalizedValue); }
    std::string getParameterDisplay(uint32_t id) const override;

    std::vector<uint8_t> saveState() const override;
    bool loadState(const std::vector<uint8_t>& state) override;

    bool hasEditor() const override { return false; }
    bool openEditor(void* parentWindow) override { return false; }
    void closeEditor() override {}
    bool isEditorOpen() const override { return false; }
    std::pair<int, int> getEditorSize() const override { return {0, 0}; }
    bool resizeEditor(int width, int height) override { return false; }

    const PluginInfo& getInfo() const override;
    uint32_t getLatencySamples() const override { return 0; }
    uint32_t getTailSamples() const override { return 0; }

    WatchdogStats getWatchdogStats() const override { return {}; }
    void resetWatchdog() override {}
    bool isBypassedByWatchdog() const override { return false; }
    bool isCrashed() const override { return false; }

    // Custom Methods
    bool loadSample(const std::string& path);
    bool loadSampleData(const std::string& path, std::vector<float> data, uint32_t rate, uint32_t channels);
    void setSampleRate(double sampleRate) { m_sampleRate = sampleRate; }
    void setEnvelope(float attack, float decay, float sustain, float release) noexcept;
    void setCoarseSemitones(float semitones) noexcept;
    void setFineTuneCents(float cents) noexcept;
    void setSampleWindow(float startNorm, float endNorm) noexcept;
    void setLoopMode(LoopMode mode) noexcept;
    void setLoopEnabled(bool enabled) noexcept;
    void setMaxVoices(int maxVoices) noexcept;
    void setRootMidiNote(int note) noexcept;
    void setMonoMode(bool mono) noexcept;
    void setCutSelfMode(bool cutSelf) noexcept;
    void setGlideTimeMs(float glideTimeMs) noexcept;
    void setPitchMode(PitchMode mode);
    /// Non-RT. Queue keep-length renders for these MIDI notes; with @p wait, block until they
    /// are ready (or @p timeout passes). Returns false on timeout. No-op in Resample mode.
    bool prewarmKeepLength(const std::vector<int>& midiNotes, bool wait,
                           std::chrono::milliseconds timeout = std::chrono::seconds(30));
    bool normalizeSample(float targetPeak = 0.95f);
    bool reverseSample();

    float getCoarseSemitones() const noexcept;
    float getFineTuneCents() const noexcept;
    float getLoopStartNorm() const noexcept { return m_loopStartNorm.load(std::memory_order_relaxed); }
    float getLoopEndNorm() const noexcept { return m_loopEndNorm.load(std::memory_order_relaxed); }
    LoopMode getLoopMode() const noexcept;
    bool isLoopEnabled() const noexcept { return getLoopMode() != LoopMode::OneShot; }
    int getMaxVoices() const noexcept { return m_maxVoices.load(std::memory_order_relaxed); }
    int getRootMidiNote() const noexcept { return m_rootMidiNote.load(std::memory_order_relaxed); }
    bool isMonoMode() const noexcept { return m_monoMode.load(std::memory_order_relaxed); }
    bool isCutSelfMode() const noexcept { return m_cutSelfMode.load(std::memory_order_relaxed); }
    float getGlideTimeMs() const noexcept { return m_glideTimeMs.load(std::memory_order_relaxed); }
    PitchMode getPitchMode() const noexcept { return static_cast<PitchMode>(m_pitchMode.load(std::memory_order_relaxed)); }
    float getAttack() const noexcept { return m_params[kParamAttack].load(std::memory_order_relaxed); }
    float getDecay() const noexcept { return m_params[kParamDecay].load(std::memory_order_relaxed); }
    float getSustain() const noexcept { return m_params[kParamSustain].load(std::memory_order_relaxed); }
    float getRelease() const noexcept { return m_params[kParamRelease].load(std::memory_order_relaxed); }

    // RT-safe: requests an immediate voice reset on the next process() call.
    void requestHardResetVoices() noexcept { m_resetVoicesRequested.store(true, std::memory_order_release); }

    // RT-safe: asks every sounding voice to enter its ADSR release on the next process() call,
    // BEFORE that block's MIDI is handled (so a note that starts in the same block still sounds).
    // This is what a pause, a stop and a timeline loop wrap need: the queued note-offs for notes
    // still held at that moment are discarded with the transport jump, and without this the voices
    // sustain until the sample runs out. A tight release stays tight.
    void requestReleaseAllVoices() noexcept { m_releaseVoicesRequested.store(true, std::memory_order_release); }

private:
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_resetVoicesRequested{false};
    std::atomic<bool> m_releaseVoicesRequested{false};
    double m_sampleRate = 44100.0;

    // Sample Data encapsulated for Atomic Swap
    struct SampleData {
        std::vector<float> data; // Interleaved
        uint32_t channels = 2;
        uint32_t rate = 44100;
        std::string path;
    };

    // Shared Ptr accessed atomically (C++11/17 free functions)
    // No mutex needed for access anymore!
    AtomicSharedPtr<SampleData> m_data;
    // The same sample for process(): AtomicSharedPtr::load takes a pooled mutex in this C++17
    // build. Every m_data swap publishes here too (publishSample).
    RealtimeSharedSlot<SampleData> m_rtData;
    void publishSample(const std::shared_ptr<SampleData>& data) { m_rtData.publish(data); }

    // Gap-free swap: the sample the audio thread last rendered (audio thread only), and the swapped-out buffers that
    // sounding voices may still be reading (non-RT, under m_retiredMutex).
    std::shared_ptr<SampleData> m_audioLastData;
    std::mutex m_retiredMutex;
    std::vector<std::shared_ptr<SampleData>> m_retired;
    /// Park a swapped-out sample until no voice reads it, then hand it to the garbage collector. Non-RT only.
    void retireSample(std::shared_ptr<SampleData> old);

    // Parameters
    enum ParamID { kParamAttack = 0, kParamDecay, kParamSustain, kParamRelease, kParamPitch, kParamCount };
    std::array<std::atomic<float>, kParamCount> m_params;
    std::atomic<float> m_fineTuneCents{0.0f};
    std::atomic<float> m_loopStartNorm{0.0f};
    std::atomic<float> m_loopEndNorm{1.0f};
    std::atomic<int> m_loopMode{static_cast<int>(LoopMode::OneShot)};
    std::atomic<int> m_maxVoices{4};
    std::atomic<int> m_rootMidiNote{60}; // C3 default
    std::atomic<bool> m_monoMode{false};
    std::atomic<bool> m_cutSelfMode{false};
    std::atomic<int> m_pitchMode{static_cast<int>(PitchMode::Resample)};
    std::shared_ptr<KeepLengthCache> m_keepLength = std::make_shared<KeepLengthCache>();
    /// Total semitone shift a note plays at (note vs root, plus coarse/fine/pitch), rounded.
    int keepLengthSemitones(int midiNote) const noexcept;
    /// Hand the current sample to the keep-length cache (non-RT; every m_data swap calls it).
    void publishToKeepLength(const std::shared_ptr<SampleData>& data);
    std::atomic<float> m_glideTimeMs{80.0f};

    // Voice Architecture
    enum class EnvStage { Attack, Decay, Sustain, Release, Off };

    /// The playable window of a voice's sample: where it starts, where it ends,
    /// and the loop geometry. A function of the voice's frame count and the
    /// block's loop points, and therefore the same for every sample of a voice
    /// in a block.
    struct SampleWindow {
        double total = 0.0;
        double start = 0.0;
        double end = 0.0;
        double loopLength = 1.0;
        double pingPongEnd = 0.0;
        double pingPongSpan = 0.0;
    };

    struct Voice {
        bool active = false;
        int note = 0;
        float velocity = 0.0f;
        float gainL = 1.0f; // Per-note pan, latched at note-on (unity centre)
        float gainR = 1.0f;
        double position = 0.0; // Sample index
        double playbackRate = 1.0; // sample index increment/frame
        double targetPlaybackRate = 1.0;
        int playbackDirection = 1;
        bool glideActive = false;
        EnvStage stage = EnvStage::Off;
        double stageTime = 0.0; // Seconds in current stage
        float currentGain = 0.0f;
        float releaseGain = 0.0f; // Gain at start of release
        // > 0: this release is a declick fade of that many seconds (a choked or
        // stolen voice), not the user's Release time.
        double fadeSeconds = 0.0;
        uint64_t order = 0;       // Trigger order: an id-less note-off ends the oldest held voice of its pitch
        uint32_t noteId = 0;      // The scheduler's note identity (0 = none, e.g. live MIDI)
        // Keep-length voices play their own stretched copy; null plays the shared sample.
        std::shared_ptr<const KeepLengthCache::Buffer> ownData;
        // A sample swapped out while this voice sounded: the voice finishes on the sample it started on. Set by
        // the audio thread (a refcount copy); the plugin's retired list keeps the buffer alive past the voice, so
        // dropping this reference is never the last one on the audio thread.
        std::shared_ptr<SampleData> held;
        // The cached window, and the inputs it was computed from. Rebuilt only
        // when one of them changes, so a voice that changes sample mid-block
        // or a block whose loop points moved still gets the right window.
        SampleWindow window{};
        double windowFrames = -1.0;
        double windowStartNorm = -1.0;
        double windowEndNorm = -1.0;
    };

    static constexpr int kMaxVoices = 32;
    std::array<Voice, kMaxVoices> m_voices;

    // Per-note pan latch: the pattern scheduler sends pan as polyphonic
    // aftertouch (0xA0, note, 0..127) just before the matching note-on, which
    // consumes and re-centres its slot. Audio-thread only — no atomics needed.
    std::array<uint8_t, 128> m_pendingNotePan{};

    // Note ownership. A scheduled note-off carries its note's id and ends exactly the
    // voice that note started; if that voice is already gone (choked, stolen, taken over
    // in mono, or its sample ran out) the note-off does nothing. Live MIDI has no id:
    // it ends the oldest held voice of its pitch (exact, since a key can't be pressed
    // twice without being released). Audio-thread only.
    uint64_t m_nextVoiceOrder = 1;

    // Helpers
    void handleMidiEvent(const MidiBuffer::Event& event, double baseRate,
                         const std::shared_ptr<SampleData>& currentData);
    void renderVoice(Voice& voice, float* outL, float* outR, uint32_t frames);
    float getEnvelopeLevel(Voice& voice, double dt, float attack, float decay, float sustain, float release);
    /// A voice cut off by a new note (cut-self, a mono retrigger) fades over this
    /// long instead of stopping dead: an instant cut jumps the waveform to zero
    /// mid-cycle, which on a sub-bass is an audible tick.
    static constexpr double kDeclickSeconds = 0.004;
    /// The polyphony gain (1/sqrt(voices)) glides to its new value over about
    /// this long instead of jumping when a voice starts or ends: a jump scales
    /// every sounding voice in one sample, which ticks on low sounds.
    static constexpr double kVoiceCompGlideSeconds = 0.005;
    float m_voiceCompSmoothed = 1.0f;
    void declickVoice(Voice& v);
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
