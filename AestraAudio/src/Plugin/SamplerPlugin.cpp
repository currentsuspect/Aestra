#include "Plugin/SamplerPlugin.h"

#include "AestraJSON.h"
#include "AestraLog.h"
#include "Plugin/BuiltInPlugins.h"
#include "GarbageCollector.h"
#include "IO/MiniAudioDecoder.h"
#include "DSP/Interpolators.h"

#include <algorithm>
#include <limits>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace {
// The garbage collector must be constructed before any worker that releases into it (the keep-length render
// service), so it is destroyed after them at exit. Retiring swapped-out samples may no longer touch it early, so
// pin its construction at startup rather than at first use.
[[maybe_unused]] const bool kGarbageCollectorPinned = (Aestra::Audio::GarbageCollector::instance(), true);
} // namespace

namespace Aestra {
namespace Audio {
namespace Plugins {

namespace {
constexpr float kSamplerOutputHeadroom = 0.22f; // ~ -13 dB default trim

bool isSafeSamplerStatePath(const std::string& path) {
    if (path.empty() || path.find("://") != std::string::npos) {
        return false;
    }

    // Block POSIX absolute paths, Windows drive paths, root-relative Windows
    // paths, and UNC/network paths before filesystem normalization can vary by
    // platform.
    if (path[0] == '/' || path[0] == '\\') {
        return false;
    }
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        return false;
    }
    if (path.rfind("\\\\", 0) == 0 || path.rfind("//", 0) == 0) {
        return false;
    }

    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::filesystem::path fsPath(normalized);
    if (fsPath.is_absolute()) {
        return false;
    }
    for (const auto& part : fsPath) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}
}

SamplerPlugin::SamplerPlugin() {
    // defaults
    m_params[kParamAttack].store(0.01f); // 10ms
    m_params[kParamDecay].store(0.1f);   // 100ms
    m_params[kParamSustain].store(0.8f); // 80%
    m_params[kParamRelease].store(0.3f); // 300ms
    m_params[kParamPitch].store(0.5f);   // Center (0 semitones)
    m_maxVoices.store(4, std::memory_order_relaxed);
}

void SamplerPlugin::setEnvelope(float attack, float decay, float sustain, float release) noexcept {
    m_params[kParamAttack].store(std::max(0.001f, attack), std::memory_order_relaxed);
    m_params[kParamDecay].store(std::max(0.001f, decay), std::memory_order_relaxed);
    m_params[kParamSustain].store(std::clamp(sustain, 0.0f, 1.0f), std::memory_order_relaxed);
    m_params[kParamRelease].store(std::max(0.001f, release), std::memory_order_relaxed);
}

void SamplerPlugin::setCoarseSemitones(float semitones) noexcept {
    const float normalized = std::clamp((semitones / 24.0f) + 0.5f, 0.0f, 1.0f);
    m_params[kParamPitch].store(normalized, std::memory_order_relaxed);
}

void SamplerPlugin::setFineTuneCents(float cents) noexcept {
    m_fineTuneCents.store(std::clamp(cents, -100.0f, 100.0f), std::memory_order_relaxed);
}

void SamplerPlugin::setSampleWindow(float startNorm, float endNorm) noexcept {
    const float start = std::clamp(startNorm, 0.0f, 0.999f);
    const float end = std::clamp(endNorm, start + 0.001f, 1.0f);
    m_loopStartNorm.store(start, std::memory_order_relaxed);
    m_loopEndNorm.store(end, std::memory_order_relaxed);
}

void SamplerPlugin::setLoopMode(LoopMode mode) noexcept {
    m_loopMode.store(static_cast<int>(mode), std::memory_order_relaxed);
}

void SamplerPlugin::setLoopEnabled(bool enabled) noexcept {
    setLoopMode(enabled ? LoopMode::Forward : LoopMode::OneShot);
}

void SamplerPlugin::setMaxVoices(int maxVoices) noexcept {
    m_maxVoices.store(std::clamp(maxVoices, 1, kMaxVoices), std::memory_order_relaxed);
}

void SamplerPlugin::setRootMidiNote(int note) noexcept {
    m_rootMidiNote.store(std::clamp(note, 0, 127), std::memory_order_relaxed);
}

void SamplerPlugin::setMonoMode(bool mono) noexcept {
    m_monoMode.store(mono, std::memory_order_relaxed);
}

void SamplerPlugin::setCutSelfMode(bool cutSelf) noexcept {
    m_cutSelfMode.store(cutSelf, std::memory_order_relaxed);
}

void SamplerPlugin::setGlideTimeMs(float glideTimeMs) noexcept {
    m_glideTimeMs.store(std::clamp(glideTimeMs, 0.0f, 2000.0f), std::memory_order_relaxed);
}

void SamplerPlugin::setPitchMode(PitchMode mode) {
    m_pitchMode.store(static_cast<int>(mode), std::memory_order_relaxed);
    if (mode == PitchMode::KeepLength) {
        m_keepLength->prewarm({}); // registers with the render service; notes are prewarmed by the host
    }
}

int SamplerPlugin::keepLengthSemitones(int midiNote) const noexcept {
    const float pitchParam = m_params[kParamPitch].load(std::memory_order_relaxed);
    const float globalSemitones =
        (pitchParam - 0.5f) * 24.0f + (m_fineTuneCents.load(std::memory_order_relaxed) / 100.0f);
    const float noteSemitones = static_cast<float>(midiNote - m_rootMidiNote.load(std::memory_order_relaxed));
    return static_cast<int>(std::lround(globalSemitones + noteSemitones));
}

bool SamplerPlugin::prewarmKeepLength(const std::vector<int>& midiNotes, bool wait,
                                      std::chrono::milliseconds timeout) {
    if (getPitchMode() != PitchMode::KeepLength) return true;
    std::vector<int> semitones;
    semitones.reserve(midiNotes.size());
    for (int note : midiNotes) semitones.push_back(keepLengthSemitones(note));
    if (!wait) {
        m_keepLength->prewarm(semitones);
        return true;
    }
    return m_keepLength->waitUntilSettled(semitones, timeout);
}

void SamplerPlugin::publishToKeepLength(const std::shared_ptr<SampleData>& data) {
    if (!data || data->data.empty()) {
        m_keepLength->setSource(nullptr, 0);
        return;
    }
    // Aliasing: the cache shares ownership of the SampleData, viewing only its frames.
    m_keepLength->setSource(std::shared_ptr<const std::vector<float>>(data, &data->data), data->rate);
}

float SamplerPlugin::getCoarseSemitones() const noexcept {
    const float pitchParam = m_params[kParamPitch].load(std::memory_order_relaxed);
    return (pitchParam - 0.5f) * 24.0f;
}

float SamplerPlugin::getFineTuneCents() const noexcept {
    return m_fineTuneCents.load(std::memory_order_relaxed);
}

SamplerPlugin::LoopMode SamplerPlugin::getLoopMode() const noexcept {
    switch (m_loopMode.load(std::memory_order_relaxed)) {
    case static_cast<int>(LoopMode::Forward):
        return LoopMode::Forward;
    case static_cast<int>(LoopMode::PingPong):
        return LoopMode::PingPong;
    default:
        return LoopMode::OneShot;
    }
}

bool SamplerPlugin::initialize(double sampleRate, uint32_t maxBlockSize) {
    m_sampleRate = sampleRate;
    return true;
}

void SamplerPlugin::shutdown() {
    m_active = false;
    // Force release of data to ensure cleanup
    auto old = m_data.exchange(std::shared_ptr<SampleData>(nullptr), std::memory_order_acq_rel);
    publishSample(nullptr);
    publishToKeepLength(nullptr);
    GarbageCollector::instance().release(old, "SamplerPlugin::SampleData shutdown");
    std::lock_guard<std::mutex> lock(m_retiredMutex);
    for (auto& r : m_retired) GarbageCollector::instance().release(std::move(r), "SamplerPlugin::SampleData retired");
    m_retired.clear();
}

void SamplerPlugin::retireSample(std::shared_ptr<SampleData> old) {
    std::lock_guard<std::mutex> lock(m_retiredMutex);
    if (old) m_retired.push_back(std::move(old));
    // Anything only this list still holds is done sounding.
    for (auto it = m_retired.begin(); it != m_retired.end();) {
        if (it->use_count() == 1) {
            GarbageCollector::instance().release(std::move(*it), "SamplerPlugin::SampleData");
            it = m_retired.erase(it);
        } else {
            ++it;
        }
    }
}

void SamplerPlugin::activate() {
    m_voiceCompSmoothed = 1.0f;
    m_active = true;
    for (auto& v : m_voices)
        v.active = false;
}

void SamplerPlugin::deactivate() {
    m_active = false;
}

const PluginInfo& SamplerPlugin::getInfo() const {
    return BuiltInPlugins::samplerInfo();
}

// ... parameters ...

bool SamplerPlugin::loadSample(const std::string& path) {
    if (!std::filesystem::exists(path))
        return false;

    std::vector<float> data;
    uint32_t rate = 0;
    uint32_t channels = 0;

    // Use Aestra's unified decoder
    if (!decodeAudioFile(path, data, rate, channels)) {
        return false;
    }

    return loadSampleData(path, std::move(data), rate, channels);
}

bool SamplerPlugin::loadSampleData(const std::string& path, std::vector<float> data, uint32_t rate, uint32_t channels) {
    if (data.empty() || rate == 0 || channels == 0) {
        return false;
    }

    // Decode output is interleaved. Keep stereo data in-place and only upmix
    // mono defensively for callers that bypass decodeAudioFile's forceStereo().
    const size_t numFrames = data.size() / std::max<uint32_t>(channels, 1);
    if (channels == 1) {
        std::vector<float> interleaved(numFrames * 2);
        for (size_t i = 0; i < numFrames; ++i) {
            interleaved[i * 2] = data[i];
            interleaved[i * 2 + 1] = data[i];
        }
        data = std::move(interleaved);
    } else if (channels != 2) {
        return false;
    }

    // Prepare new data container
    auto newData = std::make_shared<SampleData>();
    newData->data = std::move(data);
    newData->rate = rate;
    newData->channels = 2;
    newData->path = path;

    // Atomic Swap (Thread-Safe, Lock-Free-ish)
    // std::atomic_exchange uses standard atomics for shared_ptr
    auto oldData = m_data.exchange(newData, std::memory_order_acq_rel);
    publishSample(newData);
    publishToKeepLength(newData);

    // Voices still sounding finish on the old sample (gap-free swap); it is collected once none read it.
    retireSample(std::move(oldData));

    return true;
}

bool SamplerPlugin::normalizeSample(float targetPeak) {
    auto currentData = m_data.load(std::memory_order_acquire);
    if (!currentData || currentData->data.empty()) {
        return false;
    }

    float peak = 0.0f;
    for (float s : currentData->data) {
        peak = std::max(peak, std::abs(s));
    }
    if (peak <= 0.0001f) {
        return false;
    }

    const float gain = std::clamp(targetPeak, 0.1f, 1.0f) / peak;
    auto edited = std::make_shared<SampleData>(*currentData);
    for (auto& s : edited->data) {
        s *= gain;
    }

    auto oldData = m_data.exchange(edited, std::memory_order_acq_rel);
    publishSample(edited);
    publishToKeepLength(edited);
    retireSample(std::move(oldData));
    return true;
}

bool SamplerPlugin::reverseSample() {
    auto currentData = m_data.load(std::memory_order_acquire);
    if (!currentData || currentData->data.empty() || currentData->channels == 0) {
        return false;
    }

    const uint32_t channels = currentData->channels;
    const size_t frames = currentData->data.size() / channels;
    if (frames < 2) {
        return false;
    }

    auto edited = std::make_shared<SampleData>(*currentData);
    auto& d = edited->data;
    for (size_t i = 0; i < frames / 2; ++i) {
        const size_t j = frames - 1 - i;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            std::swap(d[i * channels + ch], d[j * channels + ch]);
        }
    }

    auto oldData = m_data.exchange(edited, std::memory_order_acq_rel);
    publishSample(edited);
    publishToKeepLength(edited);
    retireSample(std::move(oldData));
    return true;
}

void SamplerPlugin::process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                            uint32_t numOutputChannels, uint32_t numFrames, const MidiBuffer* midiInput,
                            MidiBuffer* midiOutput) {
    // Clear Outputs
    for (uint32_t c = 0; c < numOutputChannels; ++c) {
        std::memset(outputs[c], 0, numFrames * sizeof(float));
    }

    // RT-safe hard reset (requested by engine on transport restart)
    if (m_resetVoicesRequested.exchange(false, std::memory_order_acq_rel)) {
        for (auto& v : m_voices) {
            v.active = false;
            v.stage = EnvStage::Off;
            v.position = 0.0;
            v.playbackRate = 1.0;
            v.targetPlaybackRate = 1.0;
            v.playbackDirection = 1;
            v.glideActive = false;
            v.stageTime = 0.0;
            v.currentGain = 0.0f;
            v.releaseGain = 0.0f;
            v.ownData.reset(); // refcount drop only: the cache or GarbageCollector owns the buffer
            v.held.reset();
        }
    }
    // Soft release (pause / stop / loop wrap): every sounding voice goes through its own release
    // stage. Voices already releasing keep their tail; their gain is not restarted.
    if (m_releaseVoicesRequested.exchange(false, std::memory_order_acq_rel)) {
        for (auto& v : m_voices) {
            if (!v.active || v.stage == EnvStage::Release || v.stage == EnvStage::Off) continue;
            v.stage = EnvStage::Release;
            v.stageTime = 0.0;
            v.releaseGain = v.currentGain;
        }
    }
    // Idle voices let go of stretched buffers so evicted ones can be collected.
    for (auto& v : m_voices) {
        if (!v.active && v.ownData) v.ownData.reset();
        if (!v.active && v.held) v.held.reset(); // a refcount drop: the retired list owns the buffer
    }

    // Thread-safe access to sample data: a lock-free pin held for the whole block.
    const auto samplePin = m_rtData.readRealtime();
    const std::shared_ptr<SampleData>& currentData = samplePin.shared();
    if (!currentData || currentData->data.empty())
        return;

    // A swap since the last block: voices already sounding finish on the sample they started on, so nothing
    // jumps or cuts; new notes use the new sample. (Refcount copies only; the retired list owns the buffer.)
    if (m_audioLastData != currentData) {
        if (m_audioLastData) {
            for (auto& v : m_voices) {
                if (v.active && !v.ownData && !v.held) v.held = m_audioLastData;
            }
        }
        m_audioLastData = currentData;
    }

    // Source Rate correction
    double baseRate = (double)currentData->rate / m_sampleRate;

    // Preload per-block constants from atomics (UI thread writes, audio thread reads)
    // These change rarely — loading once eliminates per-voice per-sample atomic traffic
    const float attackMs = std::max(m_params[kParamAttack].load(std::memory_order_relaxed), 0.001f);
    const float decayMs = std::max(m_params[kParamDecay].load(std::memory_order_relaxed), 0.001f);
    const float sustainLevel = m_params[kParamSustain].load(std::memory_order_relaxed);
    const float releaseMs = std::max(m_params[kParamRelease].load(std::memory_order_relaxed), 0.001f);
    const float cachedGlideTimeMs = m_glideTimeMs.load(std::memory_order_relaxed);
    const double invSampleRate = 1.0 / m_sampleRate;

    // Events
    uint32_t eventIdx = 0;
    size_t eventCount = midiInput ? midiInput->getEventCount() : 0;

    const auto& sampleVec = currentData->data;
    // Data is now interleaved stereo (2 channels packed as L,R,L,R...)
    const double totalFrames = static_cast<double>(sampleVec.size() / 2);
    if (totalFrames <= 1.0) {
        return;
    }
    const float startNorm = std::clamp(m_loopStartNorm.load(std::memory_order_relaxed), 0.0f, 0.999f);
    const float endNorm = std::clamp(m_loopEndNorm.load(std::memory_order_relaxed), startNorm + 0.001f, 1.0f);
    // The sample window in frames. A keep-length voice plays a stretched copy of a
    // different length, so each voice takes the window of the buffer it plays.
    const auto windowFor = [&](double frames) {
        SampleWindow w{};
        w.total = frames;
        w.start = startNorm * (frames - 1.0);
        w.end = std::max(w.start + 1.0, endNorm * frames);
        w.loopLength = std::max(1.0, w.end - w.start);
        w.pingPongEnd = std::max(w.start, std::min(frames - 1.0, w.end - 1.0));
        w.pingPongSpan = w.pingPongEnd - w.start;
        return w;
    };
    // The window is a function of the voice's frame count and the block's loop
    // points, so it is the SAME for every sample of a voice in a block. It was
    // being rebuilt per voice per sample -- eight double operations and four
    // branches, for a value that did not change -- which was about a third of
    // this plugin's cost.
    //
    // The cache is keyed on everything the window depends on, including the
    // block's loop points, so a voice that changes sample mid-block (a note-on
    // with its own buffer) gets a new window and a voice whose loop points
    // moved gets a new window.
    const auto windowForVoice = [&](Voice& v, double frames) -> const SampleWindow& {
        if (v.windowFrames != frames || v.windowStartNorm != startNorm || v.windowEndNorm != endNorm) {
            v.window = windowFor(frames);
            v.windowFrames = frames;
            v.windowStartNorm = startNorm;
            v.windowEndNorm = endNorm;
        }
        return v.window;
    };
    const LoopMode loopMode = getLoopMode();
    const bool loopEnabled = loopMode != LoopMode::OneShot;
    int activeVoiceCount = 0;
    bool activeVoiceCountDirty = false;
    // The voice compensation is 1/sqrt(activeVoices): it changes only when the
    // number of sounding voices does, and it was costing a square root and a
    // division on every sample of every block.
    float voiceComp = 1.0f;
    const float voiceCompGlideCoeff =
        static_cast<float>(1.0 - std::exp(-1.0 / std::max(1.0, kVoiceCompGlideSeconds * m_sampleRate)));

    // The compact list of sounding voices.
    //
    // The render loop used to walk all 32 voice slots on every sample and skip
    // the inactive ones. At one or two sounding voices that is thirty wasted
    // iterations per sample, and it was most of this plugin's cost: the loop
    // header and the `if (!v.active)` test together were about 74 million
    // instructions in a four-second render, against 90 million for the whole
    // plugin.
    //
    // The list is rebuilt in voice-index order whenever the sounding set
    // changes, so the voices are still summed in exactly the same order and the
    // output is bit-identical.
    std::array<uint8_t, kMaxVoices> activeVoiceIndices{};
    uint8_t activeVoiceIndicesCount = 0;
    const auto rebuildActiveVoices = [&]() {
        activeVoiceIndicesCount = 0;
        for (uint8_t vi = 0; vi < static_cast<uint8_t>(kMaxVoices); ++vi) {
            if (m_voices[vi].active) {
                activeVoiceIndices[activeVoiceIndicesCount++] = vi;
            }
        }
        activeVoiceCount = activeVoiceIndicesCount;
        voiceComp = 1.0f / std::sqrt(static_cast<float>(std::max(1, activeVoiceCount)));
    };
    // The voices that are already sounding when this block starts are in the
    // list from the first sample, exactly as the old full walk had them.
    rebuildActiveVoices();

    for (uint32_t i = 0; i < numFrames; ++i) {
        // Handle MIDI
        while (midiInput && eventIdx < eventCount) {
            const auto& e = midiInput->getEvent(eventIdx);
            if (e.sampleOffset == i) {
                handleMidiEvent(e, baseRate, currentData);
                activeVoiceCountDirty = true;
                eventIdx++;
            } else if (e.sampleOffset < i) {
                eventIdx++; // Skip past
            } else {
                break; // Future
            }
        }

        // Render Voices
        float L = 0.0f;
        float R = 0.0f;
        // One-pole glide toward the current polyphony compensation (see
        // kVoiceCompGlideSeconds); per sample, shared by every voice.
        m_voiceCompSmoothed += (voiceComp - m_voiceCompSmoothed) * voiceCompGlideCoeff;
        if (activeVoiceCountDirty) {
            rebuildActiveVoices();
            activeVoiceCountDirty = false;
        }

        for (uint8_t vi = 0; vi < activeVoiceIndicesCount; ++vi) {
            Voice& v = m_voices[activeVoiceIndices[vi]];
            // A voice can end inside this very sample (its sample ran out), so
            // the list can still name a voice that is no longer sounding.
            if (!v.active)
                continue;

            const double voiceFrames = v.ownData ? static_cast<double>(v.ownData->frames())
                                 : v.held  ? static_cast<double>(v.held->data.size() / 2)
                                           : totalFrames;
            const SampleWindow& w = windowForVoice(v, voiceFrames);
            const std::vector<float>& voiceSamples = v.ownData ? v.ownData->interleaved : (v.held ? v.held->data : sampleVec);
            const double startFrame = w.start;
            const double endFrame = w.end;
            const double loopLength = w.loopLength;
            const double pingPongEndFrame = w.pingPongEnd;
            const double pingPongSpan = w.pingPongSpan;

            if (v.glideActive) {
                if (cachedGlideTimeMs <= 0.0f) {
                    v.playbackRate = v.targetPlaybackRate;
                    v.glideActive = false;
                } else {
                    const double glideSamples = std::max(1.0, (static_cast<double>(cachedGlideTimeMs) * m_sampleRate) / 1000.0);
                    const double glideCoeff = 1.0 - std::exp(-1.0 / glideSamples);
                    v.playbackRate += (v.targetPlaybackRate - v.playbackRate) * glideCoeff;
                    if (std::abs(v.targetPlaybackRate - v.playbackRate) < 1.0e-5) {
                        v.playbackRate = v.targetPlaybackRate;
                        v.glideActive = false;
                    }
                }
            }

            // One-shot end-of-sample fade (#452). No note-off may ever arrive
            // in one-shot mode, so fade toward the endpoint with a bounded,
            // stateless multiplier on top of the normal ADSR — never by
            // mutating the ADSR stage (the old code entered Release at
            // trigger time for samples shorter than the release, capturing
            // releaseGain from a still-zero attack: permanent silence).
            //
            // Units: v.position/endFrame are SOURCE frames; the configured
            // release is OUTPUT time. Convert via |playbackRate|, and clamp
            // the effective fade to the playable one-shot duration so short
            // samples fade across what they have instead of starting at zero.
            float oneShotTail = 1.0f;
            if (!loopEnabled) {
                const double absRate = std::abs(v.playbackRate);
                if (absRate > 1.0e-9) {
                    const double remainingOutputFrames = (endFrame - v.position) / absRate;
                    const double configuredReleaseFrames = static_cast<double>(releaseMs) * m_sampleRate;
                    const double playableOutputFrames = (endFrame - startFrame) / absRate;
                    const double effectiveTailFrames =
                        std::max(1.0, std::min(configuredReleaseFrames, playableOutputFrames));
                    oneShotTail = static_cast<float>(std::clamp(remainingOutputFrames / effectiveTailFrames, 0.0, 1.0));
                }
            }

            // Envelope (uses preloaded ADSR — no atomic loads)
            float env = getEnvelopeLevel(v, invSampleRate, attackMs, decayMs, sustainLevel, releaseMs) * oneShotTail;
            if (v.stage == EnvStage::Off) {
                v.active = false;
                activeVoiceCountDirty = true;
                continue;
            }

            // Sample Lookup
            if (loopMode == LoopMode::PingPong) {
                // Fold any boundary overshoot into a triangle wave. This remains
                // bounded even when extreme pitch makes one frame cross the loop
                // multiple times, and avoids a callback-time correction loop.
                if (pingPongSpan <= 1.0e-9) {
                    v.position = startFrame;
                    v.playbackDirection = 1;
                } else if (v.playbackDirection >= 0 && v.position > pingPongEndFrame) {
                    const double cycle = std::fmod(v.position - pingPongEndFrame, pingPongSpan * 2.0);
                    if (cycle <= pingPongSpan) {
                        v.position = pingPongEndFrame - cycle;
                        v.playbackDirection = -1;
                    } else {
                        v.position = startFrame + (cycle - pingPongSpan);
                        v.playbackDirection = 1;
                    }
                } else if (v.playbackDirection < 0 && v.position < startFrame) {
                    const double cycle = std::fmod(startFrame - v.position, pingPongSpan * 2.0);
                    if (cycle <= pingPongSpan) {
                        v.position = startFrame + cycle;
                        v.playbackDirection = 1;
                    } else {
                        v.position = pingPongEndFrame - (cycle - pingPongSpan);
                        v.playbackDirection = -1;
                    }
                }
            } else {
                if (v.position < startFrame) {
                    v.position = startFrame;
                }
                if (v.position >= endFrame && loopMode == LoopMode::Forward) {
                    const double offset = v.position - startFrame;
                    const double wraps = std::floor(offset / loopLength);
                    v.position = startFrame + offset - wraps * loopLength;
                } else if (v.position >= endFrame) {
                    v.active = false;
                    activeVoiceCountDirty = true;
                    continue;
                }
            }
            if (v.position >= voiceFrames) {
                v.active = false;
                activeVoiceCountDirty = true;
                continue;
            }

            // Sinc64Turbo Interpolation (data is now interleaved)
            float outL = 0.0f, outR = 0.0f;
            if (v.position >= 0.0 && v.position < voiceFrames) {
                Interpolators::Sinc64Turbo::interpolate(
                    voiceSamples.data(),     // interleaved stereo [L,R,L,R...]
                    voiceFrames,              // total frames (not samples)
                    v.position,                // fractional position
                    outL, outR                // output
                );
            }

            float gain = (v.velocity * v.velocity) * env * kSamplerOutputHeadroom * m_voiceCompSmoothed;
            L += outL * gain * v.gainL;
            R += outR * gain * v.gainR;

            // Advance
            v.position += v.playbackRate * static_cast<double>(
                                                 loopMode == LoopMode::PingPong ? v.playbackDirection : 1);
        }

        // Guard against non-finite output: sinc interpolation at extreme
        // playback rates (or a corrupted sample) can produce NaN/Inf, which
        // must not leave the plugin and poison the engine mix.
        outputs[0][i] = std::isfinite(L) ? L : 0.0f;
        outputs[1][i] = std::isfinite(R) ? R : 0.0f;
    }
}

void SamplerPlugin::handleMidiEvent(const MidiBuffer::Event& event, double baseRate,
                                    const std::shared_ptr<SampleData>& currentData) {
    uint8_t status = event.data[0] & 0xF0;
    uint8_t note = event.data[1];
    uint8_t velocity = event.data[2];

    if (status == 0xA0) { // Per-note pan, sent just before the matching note-on
        m_pendingNotePan[note & 0x7F] = velocity; // 1..127, 64 = centre
        return;
    }

    if (status == 0x90 && velocity > 0) { // Note On
        // Consume the pending pan for this note (0 = none → centre) and
        // re-arm the slot so later notes without pan stay centred. Unity at
        // centre keeps unpanned projects bit-identical to before.
        float noteGainL = 1.0f;
        float noteGainR = 1.0f;
        {
            const uint8_t panByte = m_pendingNotePan[note & 0x7F];
            m_pendingNotePan[note & 0x7F] = 0;
            if (panByte != 0 && panByte != 64) {
                const float pan = std::clamp((static_cast<float>(panByte) - 64.0f) / 63.0f, -1.0f, 1.0f);
                noteGainL = std::min(1.0f, 1.0f - pan);
                noteGainR = std::min(1.0f, 1.0f + pan);
            }
        }
        const float pitchParam = m_params[kParamPitch].load(std::memory_order_relaxed);
        const float globalSemitones = (pitchParam - 0.5f) * 24.0f + (m_fineTuneCents.load(std::memory_order_relaxed) / 100.0f);
        const int rootMidiNote = m_rootMidiNote.load(std::memory_order_relaxed);
        const float noteSemitones = static_cast<float>(note) - static_cast<float>(rootMidiNote);
        const float ratio = std::pow(2.0f, (globalSemitones + noteSemitones) / 12.0f);
        const double targetRate = baseRate * ratio;

        // Keep-length: play this pitch's stretched copy (same targetRate, so the pitch is
        // unchanged; the copy was lengthened by `ratio` beforehand, so the note lasts as long
        // as the source). Not rendered yet: ask for it, and this hit plays resampled.
        std::shared_ptr<const KeepLengthCache::Buffer> keepBuffer;
        if (getPitchMode() == PitchMode::KeepLength) {
            const int semitones = static_cast<int>(std::lround(globalSemitones + noteSemitones));
            keepBuffer = m_keepLength->tryGet(semitones);
            if (!keepBuffer) m_keepLength->request(semitones);
        }
        const auto startFrameFor = [&](double sharedStartFrame) {
            if (!keepBuffer) return sharedStartFrame;
            return std::clamp(static_cast<double>(m_loopStartNorm.load(std::memory_order_relaxed)), 0.0, 0.999) *
                   std::max(1.0, static_cast<double>(keepBuffer->frames()) - 1.0);
        };

        // Cut-self: a new trigger chokes every previous voice in this sampler,
        // so retriggered one-shots (e.g. an 808) never overlap in the Piano
        // Roll when their pitches differ. This is an explicit voice-layer
        // policy, independent of mono mode, ADSR, and unit type — it runs
        // before the mono branch so a mono retrigger chokes the held voice
        // instead of gliding into it.
        if (m_cutSelfMode.load(std::memory_order_relaxed)) {
            // Choked, not cut: each sounding voice fades over kDeclickSeconds.
            // The old note is still gone almost at once (no audible overlap),
            // without the step to zero that ticks on low sounds.
            for (auto& v : m_voices) {
                if (v.active) declickVoice(v);
            }
        }

        if (m_monoMode.load(std::memory_order_relaxed)) {
            double noteStartFrame = 0.0;
            if (currentData && currentData->channels > 0) {
                const double totalFrames = static_cast<double>(currentData->data.size() / currentData->channels);
                noteStartFrame = std::clamp(static_cast<double>(m_loopStartNorm.load(std::memory_order_relaxed)), 0.0, 0.999) *
                                 std::max(1.0, totalFrames - 1.0);
            }

            auto& voice = m_voices[0];
            const bool legato = voice.active && voice.stage != EnvStage::Release && voice.stage != EnvStage::Off;
            if (!legato && voice.active && voice.currentGain > 0.0f) {
                // A mono restart reuses slot 0. Whatever is still sounding
                // there (a release tail, a choked voice) moves to a free slot
                // and fades out, instead of being overwritten mid-waveform.
                for (size_t i = 1; i < m_voices.size(); ++i) {
                    if (!m_voices[i].active) {
                        m_voices[i] = voice;
                        declickVoice(m_voices[i]);
                        break;
                    }
                }
            }
            voice.order = m_nextVoiceOrder++;
            voice.noteId = event.noteId; // the previous note no longer owns the voice; its note-off won't match

            voice.active = true;
            voice.note = note;
            voice.velocity = velocity / 127.0f;
            voice.gainL = noteGainL;
            voice.gainR = noteGainR;

            if (legato) {
                voice.targetPlaybackRate = targetRate;
                const float glideTimeMs = m_glideTimeMs.load(std::memory_order_relaxed);
                if (glideTimeMs <= 0.0f) {
                    voice.playbackRate = targetRate;
                    voice.targetPlaybackRate = targetRate;
                    voice.glideActive = false;
                } else {
                    voice.glideActive = std::abs(voice.targetPlaybackRate - voice.playbackRate) > 1.0e-5;
                }
            } else {
                voice.ownData = keepBuffer;
                voice.held.reset();
                voice.position = startFrameFor(noteStartFrame);
                voice.playbackRate = targetRate;
                voice.targetPlaybackRate = targetRate;
                voice.playbackDirection = 1;
                voice.glideActive = false;
                voice.stage = EnvStage::Attack;
                voice.stageTime = 0.0;
                voice.currentGain = 0.0f;
                voice.fadeSeconds = 0.0;
            }
            return;
        }

        const int maxVoices = std::clamp(m_maxVoices.load(std::memory_order_relaxed), 1, kMaxVoices);

        double noteStartFrame = 0.0;
        if (currentData && currentData->channels > 0) {
            const double totalFrames = static_cast<double>(currentData->data.size() / currentData->channels);
            noteStartFrame = std::clamp(static_cast<double>(m_loopStartNorm.load(std::memory_order_relaxed)), 0.0, 0.999) *
                             std::max(1.0, totalFrames - 1.0);
        }
        // Find free voice
        Voice* freeVoice = nullptr;
        Voice* oldestRelease = nullptr;
        Voice* oldestActive = nullptr;
        double maxReleaseTime = -1.0;
        double maxActiveTime = -1.0;

        for (int i = 0; i < maxVoices; ++i) {
            auto& v = m_voices[static_cast<size_t>(i)];
            if (!v.active) {
                freeVoice = &v;
                break;
            }
            if (v.stage == EnvStage::Release) {
                if (v.stageTime > maxReleaseTime) {
                    maxReleaseTime = v.stageTime;
                    oldestRelease = &v;
                }
            } else {
                if (v.position > maxActiveTime) {
                    maxActiveTime = v.position;
                    oldestActive = &v;
                }
            }
        }

        if (!freeVoice) {
            freeVoice = oldestRelease ? oldestRelease : oldestActive;
            if (!freeVoice)
                freeVoice = &m_voices[0]; // Fallback
            // Every configured slot is busy, so this one is about to be overwritten.
            // Whatever is still sounding there fades out instead of being cut
            // mid-waveform: park it in a spare slot outside the configured polyphony,
            // exactly as a mono restart does. Without this, a one-voice cut-self
            // retrigger still steps to zero in a single sample and ticks.
            if (freeVoice->active && freeVoice->stage != EnvStage::Off) {
                for (size_t i = static_cast<size_t>(maxVoices); i < m_voices.size(); ++i) {
                    if (!m_voices[i].active) {
                        m_voices[i] = *freeVoice;
                        declickVoice(m_voices[i]);
                        break;
                    }
                }
            }
        }
        freeVoice->order = m_nextVoiceOrder++;
        freeVoice->noteId = event.noteId;

        freeVoice->active = true;
        freeVoice->note = note;
        freeVoice->velocity = velocity / 127.0f;
        freeVoice->gainL = noteGainL;
        freeVoice->gainR = noteGainR;
        freeVoice->ownData = keepBuffer;
        freeVoice->held.reset();
        freeVoice->position = startFrameFor(noteStartFrame);
        freeVoice->fadeSeconds = 0.0;
        freeVoice->playbackRate = targetRate;
        freeVoice->targetPlaybackRate = targetRate;
        freeVoice->playbackDirection = 1;
        freeVoice->glideActive = false;
        freeVoice->stage = EnvStage::Attack;
        freeVoice->stageTime = 0.0;
        freeVoice->currentGain = 0.0f;
    } else if (status == 0x80 || (status == 0x90 && velocity == 0)) { // Note Off
        // One note-off ends one note. With the scheduler's id: exactly that note's voice,
        // or nothing if it is already gone. Without one (live MIDI): the oldest held
        // voice of this pitch.
        Voice* oldest = nullptr;
        for (auto& v : m_voices) {
            if (!v.active || v.note != note || v.stage == EnvStage::Release || v.stage == EnvStage::Off) continue;
            if (event.noteId != 0) {
                if (v.noteId == event.noteId) {
                    oldest = &v;
                    break;
                }
                continue;
            }
            if (!oldest || v.order < oldest->order) oldest = &v;
        }
        if (oldest) {
            oldest->stage = EnvStage::Release;
            oldest->stageTime = 0.0;
            oldest->releaseGain = oldest->currentGain;
        }
    }
}

void SamplerPlugin::declickVoice(Voice& v) {
    if (!v.active || v.stage == EnvStage::Off) return;
    // Fade from where the envelope is right now, never from a stale value.
    v.releaseGain = v.currentGain;
    v.stage = EnvStage::Release;
    v.stageTime = 0.0;
    v.fadeSeconds = kDeclickSeconds;
}

float SamplerPlugin::getEnvelopeLevel(Voice& v, double dt, float attack, float decay, float sustain, float release) {
    v.stageTime += dt;

    switch (v.stage) {
    case EnvStage::Attack:
        if (v.stageTime >= attack) {
            v.stage = EnvStage::Decay;
            v.stageTime = 0.0;
            v.currentGain = 1.0f;
        } else {
            v.currentGain = static_cast<float>(v.stageTime / attack);
        }
        break;
    case EnvStage::Decay:
        if (v.stageTime >= decay) {
            v.stage = EnvStage::Sustain;
            v.stageTime = 0.0;
            v.currentGain = sustain;
        } else {
            v.currentGain = 1.0f - static_cast<float>(v.stageTime / decay) * (1.0f - sustain);
        }
        break;
    case EnvStage::Sustain:
        v.currentGain = sustain;
        break;
    case EnvStage::Release: {
        const double length = v.fadeSeconds > 0.0 ? v.fadeSeconds : release;
        if (v.stageTime >= length) {
            v.stage = EnvStage::Off;
            v.currentGain = 0.0f;
        } else {
            v.currentGain = v.releaseGain * static_cast<float>(1.0 - v.stageTime / length);
        }
        break;
    }
    case EnvStage::Off:
        v.currentGain = 0.0f;
        break;
    }
    return v.currentGain;
}

//==============================================================================
// Parameter Management
//==============================================================================

std::vector<PluginParameter> SamplerPlugin::getParameters() const {
    std::vector<PluginParameter> params;
    params.push_back({kParamAttack, "Attack", "Att", "s", 0.01f, 0.0f, 2.0f});
    params.push_back({kParamDecay, "Decay", "Dec", "s", 0.1f, 0.0f, 2.0f});
    params.push_back({kParamSustain, "Sustain", "Sus", "", 1.0f, 0.0f, 1.0f});
    params.push_back({kParamRelease, "Release", "Rel", "s", 0.5f, 0.0f, 5.0f});
    // Pitch is STORED normalized (0.5 = 0 st; see getCoarseSemitones). The
    // declared default/range must match the stored encoding: plugin creation
    // writes param.defaultValue straight back via setParameter (BuiltInPlugins
    // applyInfo), and a semitone-encoded 0.0 default here transposed every
    // fresh sampler down an octave.
    params.push_back({kParamPitch, "Pitch", "Ptc", "", 0.5f, 0.0f, 1.0f});
    return params;
}

uint32_t SamplerPlugin::getParameterCount() const {
    return kParamCount;
}

float SamplerPlugin::getParameter(uint32_t id) const {
    if (id < kParamCount)
        return m_params[id].load();
    return 0.0f;
}

void SamplerPlugin::setParameter(uint32_t id, float value) {
    if (id >= kParamCount)
        return;
    if (!std::isfinite(value))
        return; // NaN would poison the envelope smoothers
    // No range clamp here: Attack/Decay/Release are declared 0-2 and 0-5, not
    // 0-1, so a blanket clamp would halve the sampler's envelope ranges.
    m_params[id].store(value);
}

std::string SamplerPlugin::getParameterDisplay(uint32_t id) const {
    if (id >= kParamCount)
        return "";
    if (id == kParamPitch) {
        // Stored normalized; display in musician units.
        return std::to_string(getCoarseSemitones()) + " st";
    }
    float val = m_params[id].load();
    return std::to_string(val);
}

std::vector<uint8_t> SamplerPlugin::saveState() const {
    Aestra::JSON json = Aestra::JSON::object();

    // Params
    Aestra::JSON pArr = Aestra::JSON::array();
    for (const auto& p : m_params) {
        pArr.push(Aestra::JSON(static_cast<double>(p.load())));
    }
    json.set("params", pArr);
    json.set("fineTuneCents", Aestra::JSON(static_cast<double>(m_fineTuneCents.load(std::memory_order_relaxed))));
    json.set("loopStartNorm", Aestra::JSON(static_cast<double>(m_loopStartNorm.load(std::memory_order_relaxed))));
    json.set("loopEndNorm", Aestra::JSON(static_cast<double>(m_loopEndNorm.load(std::memory_order_relaxed))));
    const LoopMode loopMode = getLoopMode();
    json.set("loopMode", Aestra::JSON(static_cast<double>(static_cast<int>(loopMode))));
    // Retain the old boolean so older builds can still open new forward or
    // bidirectional projects as an ordinary enabled loop.
    json.set("loopEnabled", Aestra::JSON(loopMode != LoopMode::OneShot));
    json.set("maxVoices", Aestra::JSON(static_cast<double>(m_maxVoices.load(std::memory_order_relaxed))));
    json.set("pitchMode", Aestra::JSON(static_cast<double>(static_cast<int>(getPitchMode()))));
    json.set("rootMidiNote", Aestra::JSON(static_cast<double>(m_rootMidiNote.load(std::memory_order_relaxed))));
    json.set("monoMode", Aestra::JSON(m_monoMode.load(std::memory_order_relaxed)));
    json.set("cutSelfMode", Aestra::JSON(m_cutSelfMode.load(std::memory_order_relaxed)));
    json.set("glideTimeMs", Aestra::JSON(static_cast<double>(m_glideTimeMs.load(std::memory_order_relaxed))));

    // Sample Path
    {
        auto current = m_data.load(std::memory_order_acquire);
        if (current && !current->path.empty()) {
            json.set("samplePath", Aestra::JSON(current->path));
        }
    }

    std::string s = json.toString();
    return std::vector<uint8_t>(s.begin(), s.end());
}

bool SamplerPlugin::loadState(const std::vector<uint8_t>& state) {
    if (state.empty())
        return false;
    std::string s(state.begin(), state.end());

    Aestra::JSON json = Aestra::JSON::parse(s);
    if (!json.isObject())
        return false;

    if (json["params"].isArray()) {
        const auto& pArr = json["params"];
        for (size_t i = 0; i < pArr.size() && i < kParamCount; ++i) {
            m_params[i].store(static_cast<float>(pArr[i].asNumber()));
        }
    }

    if (json.has("fineTuneCents")) {
        m_fineTuneCents.store(std::clamp(static_cast<float>(json["fineTuneCents"].asNumber()), -100.0f, 100.0f),
                              std::memory_order_relaxed);
    }
    if (json.has("loopStartNorm") || json.has("loopEndNorm")) {
        const float startNorm = json.has("loopStartNorm") ? static_cast<float>(json["loopStartNorm"].asNumber()) : 0.0f;
        const float endNorm = json.has("loopEndNorm") ? static_cast<float>(json["loopEndNorm"].asNumber()) : 1.0f;
        setSampleWindow(startNorm, endNorm);
    }
    // New state carries the complete mode. Historical state only had the
    // enabled flag, which maps losslessly to one-shot or forward playback.
    if (json.has("loopMode")) {
        const int storedMode = static_cast<int>(json["loopMode"].asNumber());
        setLoopMode(storedMode == static_cast<int>(LoopMode::PingPong)
                        ? LoopMode::PingPong
                        : (storedMode == static_cast<int>(LoopMode::Forward) ? LoopMode::Forward : LoopMode::OneShot));
    } else if (json.has("loopEnabled")) {
        setLoopEnabled(json["loopEnabled"].asBool());
    }
    // Absent in older projects, and unknown values from newer ones, mean Resample:
    // the only behaviour those projects were made with.
    if (json.has("pitchMode") && json["pitchMode"].isNumber() &&
        static_cast<int>(json["pitchMode"].asNumber()) == static_cast<int>(PitchMode::KeepLength)) {
        setPitchMode(PitchMode::KeepLength);
    } else {
        setPitchMode(PitchMode::Resample);
    }
    if (json.has("maxVoices")) {
        setMaxVoices(static_cast<int>(json["maxVoices"].asNumber()));
    }
    if (json.has("rootMidiNote")) {
        setRootMidiNote(static_cast<int>(json["rootMidiNote"].asNumber()));
    }
    if (json.has("monoMode")) {
        setMonoMode(json["monoMode"].asBool());
    }
    if (json.has("cutSelfMode")) {
        setCutSelfMode(json["cutSelfMode"].asBool());
    }
    if (json.has("glideTimeMs")) {
        setGlideTimeMs(static_cast<float>(json["glideTimeMs"].asNumber()));
    }

    if (json.has("samplePath")) {
        std::string path = json["samplePath"].asString();
        if (!path.empty()) {
            if (!isSafeSamplerStatePath(path)) {
                return false;
            }
            loadSample(path);
        }
    }
    return true;
}

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
