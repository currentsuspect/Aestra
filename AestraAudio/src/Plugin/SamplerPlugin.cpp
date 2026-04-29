#include "Plugin/SamplerPlugin.h"

#include "AestraJSON.h"
#include "Plugin/BuiltInPlugins.h"
#include "GarbageCollector.h"
#include "IO/MiniAudioDecoder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

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

void SamplerPlugin::setLoopEnabled(bool enabled) noexcept {
    m_loopEnabled.store(enabled, std::memory_order_relaxed);
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

void SamplerPlugin::setGlideTimeMs(float glideTimeMs) noexcept {
    m_glideTimeMs.store(std::clamp(glideTimeMs, 0.0f, 2000.0f), std::memory_order_relaxed);
}

float SamplerPlugin::getCoarseSemitones() const noexcept {
    const float pitchParam = m_params[kParamPitch].load(std::memory_order_relaxed);
    return (pitchParam - 0.5f) * 24.0f;
}

float SamplerPlugin::getFineTuneCents() const noexcept {
    return m_fineTuneCents.load(std::memory_order_relaxed);
}

bool SamplerPlugin::initialize(double sampleRate, uint32_t maxBlockSize) {
    m_sampleRate = sampleRate;
    return true;
}

void SamplerPlugin::shutdown() {
    m_active = false;
    // Force release of data to ensure cleanup
    auto old = std::atomic_exchange(&m_data, std::shared_ptr<SampleData>(nullptr));
    GarbageCollector::instance().release(old);
}

void SamplerPlugin::activate() {
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

    // Prepare new data container
    auto newData = std::make_shared<SampleData>();
    newData->data = std::move(data);
    newData->rate = rate;
    newData->channels = channels;
    newData->path = path;

    // Atomic Swap (Thread-Safe, Lock-Free-ish)
    // std::atomic_exchange uses standard atomics for shared_ptr
    auto oldData = std::atomic_exchange(&m_data, newData);

    // Safely dispose of old data via Garbage Collector (avoids delete on Audio Thread)
    GarbageCollector::instance().release(oldData);

    return true;
}

bool SamplerPlugin::normalizeSample(float targetPeak) {
    auto currentData = std::atomic_load(&m_data);
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

    auto oldData = std::atomic_exchange(&m_data, edited);
    GarbageCollector::instance().release(oldData);
    return true;
}

bool SamplerPlugin::reverseSample() {
    auto currentData = std::atomic_load(&m_data);
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

    auto oldData = std::atomic_exchange(&m_data, edited);
    GarbageCollector::instance().release(oldData);
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
            v.glideActive = false;
            v.stageTime = 0.0;
            v.currentGain = 0.0f;
            v.releaseGain = 0.0f;
        }
    }

    // Thread-safe access to sample data
    auto currentData = std::atomic_load(&m_data);
    if (!currentData || currentData->data.empty())
        return;

    // Source Rate correction
    double baseRate = (double)currentData->rate / m_sampleRate;

    // Events
    uint32_t eventIdx = 0;
    size_t eventCount = midiInput ? midiInput->getEventCount() : 0;

    const auto& sampleVec = currentData->data;
    uint32_t channels = currentData->channels;
    const double totalFrames = static_cast<double>(sampleVec.size() / std::max<uint32_t>(channels, 1));
    if (totalFrames <= 1.0) {
        return;
    }
    const float startNorm = std::clamp(m_loopStartNorm.load(std::memory_order_relaxed), 0.0f, 0.999f);
    const float endNorm = std::clamp(m_loopEndNorm.load(std::memory_order_relaxed), startNorm + 0.001f, 1.0f);
    const double startFrame = startNorm * (totalFrames - 1.0);
    const double endFrame = std::max(startFrame + 1.0, endNorm * totalFrames);
    const double loopLength = std::max(1.0, endFrame - startFrame);
    const bool loopEnabled = m_loopEnabled.load(std::memory_order_relaxed);
    int activeVoiceCount = 0;
    for (const auto& v : m_voices) {
        if (v.active) {
            ++activeVoiceCount;
        }
    }
    bool activeVoiceCountDirty = false;

    for (uint32_t i = 0; i < numFrames; ++i) {
        // Handle MIDI
        while (midiInput && eventIdx < eventCount) {
            const auto& e = midiInput->getEvent(eventIdx);
            if (e.sampleOffset == i) {
                handleMidiEvent(e, baseRate);
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
        if (activeVoiceCountDirty) {
            activeVoiceCount = 0;
            for (const auto& v : m_voices) {
                if (v.active) {
                    ++activeVoiceCount;
                }
            }
            activeVoiceCountDirty = false;
        }
        const float voiceComp = 1.0f / std::sqrt(static_cast<float>(std::max(1, activeVoiceCount)));

        for (auto& v : m_voices) {
            if (!v.active)
                continue;

            if (v.glideActive) {
                const float glideTimeMs = m_glideTimeMs.load(std::memory_order_relaxed);
                if (glideTimeMs <= 0.0f) {
                    v.playbackRate = v.targetPlaybackRate;
                    v.glideActive = false;
                } else {
                    const double glideSamples = std::max(1.0, (static_cast<double>(glideTimeMs) * m_sampleRate) / 1000.0);
                    const double glideCoeff = 1.0 - std::exp(-1.0 / glideSamples);
                    v.playbackRate += (v.targetPlaybackRate - v.playbackRate) * glideCoeff;
                    if (std::abs(v.targetPlaybackRate - v.playbackRate) < 1.0e-5) {
                        v.playbackRate = v.targetPlaybackRate;
                        v.glideActive = false;
                    }
                }
            }

            // In one-shot mode there may be no note-off; trigger a timed release near the end
            // so ADSR remains meaningful for per-hit shaping.
            if (!loopEnabled && v.stage != EnvStage::Release && v.stage != EnvStage::Off) {
                const double framesToEnd = endFrame - v.position;
                const float releaseSec = std::max(m_params[kParamRelease].load(std::memory_order_relaxed), 0.001f);
                const double releaseFrames = releaseSec * m_sampleRate;
                if (framesToEnd <= releaseFrames) {
                    v.stage = EnvStage::Release;
                    v.stageTime = 0.0;
                    v.releaseGain = std::max(0.0f, v.currentGain);
                }
            }

            // Envelope
            float env = getEnvelopeLevel(v, 1.0 / m_sampleRate);
            if (v.stage == EnvStage::Off) {
                v.active = false;
                activeVoiceCountDirty = true;
                continue;
            }

            // Sample Lookup
            if (v.position < startFrame) {
                v.position = startFrame;
            }
            if (v.position >= endFrame) {
                if (loopEnabled) {
                    v.position = startFrame + std::fmod(v.position - startFrame, loopLength);
                } else {
                    v.active = false;
                    activeVoiceCountDirty = true;
                    continue;
                }
            }
            if (v.position >= totalFrames) {
                v.active = false;
                activeVoiceCountDirty = true;
                continue;
            }

            // Linear Interpolation
            double pos = v.position;
            uint64_t idx = (uint64_t)pos;
            double frac = pos - idx;

            // Safe sample fetching with Mono/Stereo support
            // Check basic bounds for first channel
            if (idx * channels < sampleVec.size()) {
                float sL = sampleVec[idx * channels];
                float sR =
                    (channels > 1 && (idx * channels + 1 < sampleVec.size())) ? sampleVec[idx * channels + 1] : sL;

                // Next sample (interpolating to)
                float nL = 0.0f, nR = 0.0f;
                const bool nextInWindow = static_cast<double>(idx + 1) < endFrame;
                if (nextInWindow && (idx + 1) * channels < sampleVec.size()) {
                    nL = sampleVec[(idx + 1) * channels];
                    nR = (channels > 1 && ((idx + 1) * channels + 1 < sampleVec.size()))
                             ? sampleVec[(idx + 1) * channels + 1]
                             : nL;
                } else {
                    nL = loopEnabled ? sampleVec[static_cast<uint64_t>(startFrame) * channels] : 0.0f;
                    if (channels > 1 && loopEnabled && static_cast<uint64_t>(startFrame) * channels + 1 < sampleVec.size()) {
                        nR = sampleVec[static_cast<uint64_t>(startFrame) * channels + 1];
                    } else {
                        nR = loopEnabled ? nL : 0.0f;
                    }
                }

                float outL = sL + frac * (nL - sL);
                float outR = sR + frac * (nR - sR);

                float gain = (v.velocity * v.velocity) * env * kSamplerOutputHeadroom * voiceComp;
                L += outL * gain;
                R += outR * gain;
            }

            // Advance
            v.position += v.playbackRate;
        }

        outputs[0][i] = L;
        outputs[1][i] = R;
    }
}

void SamplerPlugin::handleMidiEvent(const MidiBuffer::Event& event, double baseRate) {
    uint8_t status = event.data[0] & 0xF0;
    uint8_t note = event.data[1];
    uint8_t velocity = event.data[2];

    if (status == 0x90 && velocity > 0) { // Note On
        const float pitchParam = m_params[kParamPitch].load(std::memory_order_relaxed);
        const float globalSemitones = (pitchParam - 0.5f) * 24.0f + (m_fineTuneCents.load(std::memory_order_relaxed) / 100.0f);
        const int rootMidiNote = m_rootMidiNote.load(std::memory_order_relaxed);
        const float noteSemitones = static_cast<float>(note) - static_cast<float>(rootMidiNote);
        const float ratio = std::pow(2.0f, (globalSemitones + noteSemitones) / 12.0f);
        const double targetRate = baseRate * ratio;

        if (m_monoMode.load(std::memory_order_relaxed)) {
            double noteStartFrame = 0.0;
            if (auto currentData = std::atomic_load(&m_data); currentData && currentData->channels > 0) {
                const double totalFrames = static_cast<double>(currentData->data.size() / currentData->channels);
                noteStartFrame = std::clamp(static_cast<double>(m_loopStartNorm.load(std::memory_order_relaxed)), 0.0, 0.999) *
                                 std::max(1.0, totalFrames - 1.0);
            }

            auto& voice = m_voices[0];
            const bool legato = voice.active && voice.stage != EnvStage::Release && voice.stage != EnvStage::Off;

            voice.active = true;
            voice.note = note;
            voice.velocity = velocity / 127.0f;

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
                voice.position = noteStartFrame;
                voice.playbackRate = targetRate;
                voice.targetPlaybackRate = targetRate;
                voice.glideActive = false;
                voice.stage = EnvStage::Attack;
                voice.stageTime = 0.0;
                voice.currentGain = 0.0f;
            }
            return;
        }

        const int maxVoices = std::clamp(m_maxVoices.load(std::memory_order_relaxed), 1, kMaxVoices);
        double noteStartFrame = 0.0;
        if (auto currentData = std::atomic_load(&m_data); currentData && currentData->channels > 0) {
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
        }

        freeVoice->active = true;
        freeVoice->note = note;
        freeVoice->velocity = velocity / 127.0f;
        freeVoice->position = noteStartFrame;
        freeVoice->playbackRate = targetRate;
        freeVoice->targetPlaybackRate = targetRate;
        freeVoice->glideActive = false;
        freeVoice->stage = EnvStage::Attack;
        freeVoice->stageTime = 0.0;
        freeVoice->currentGain = 0.0f;
    } else if (status == 0x80 || (status == 0x90 && velocity == 0)) { // Note Off
        // ADSR-driven one-shot: note-off enters release so envelope fully shapes each hit.
        for (auto& v : m_voices) {
            if (v.active && v.note == note && v.stage != EnvStage::Release) {
                v.stage = EnvStage::Release;
                v.stageTime = 0.0;
                v.releaseGain = v.currentGain;
            }
        }
    }
}

float SamplerPlugin::getEnvelopeLevel(Voice& v, double dt) {
    v.stageTime += dt;

    float a = m_params[kParamAttack].load(std::memory_order_relaxed);
    float d = m_params[kParamDecay].load(std::memory_order_relaxed);
    float s = m_params[kParamSustain].load(std::memory_order_relaxed);
    float r = m_params[kParamRelease].load(std::memory_order_relaxed);

    a = std::max(a, 0.001f);
    d = std::max(d, 0.001f);
    r = std::max(r, 0.001f);

    switch (v.stage) {
    case EnvStage::Attack:
        if (v.stageTime >= a) {
            v.stage = EnvStage::Decay;
            v.stageTime = 0.0;
            v.currentGain = 1.0f;
        } else {
            v.currentGain = v.stageTime / a;
        }
        break;
    case EnvStage::Decay:
        if (v.stageTime >= d) {
            v.stage = EnvStage::Sustain;
            v.stageTime = 0.0;
            v.currentGain = s;
        } else {
            v.currentGain = 1.0f - (v.stageTime / d) * (1.0f - s);
        }
        break;
    case EnvStage::Sustain:
        v.currentGain = s;
        break;
    case EnvStage::Release:
        if (v.stageTime >= r) {
            v.stage = EnvStage::Off;
            v.currentGain = 0.0f;
        } else {
            v.currentGain = v.releaseGain * (1.0f - v.stageTime / r);
        }
        break;
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
    params.push_back({kParamPitch, "Pitch", "Ptc", "st", 0.0f, -24.0f, 24.0f});
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
    if (id < kParamCount)
        m_params[id].store(value);
}

std::string SamplerPlugin::getParameterDisplay(uint32_t id) const {
    if (id >= kParamCount)
        return "";
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
    json.set("loopEnabled", Aestra::JSON(m_loopEnabled.load(std::memory_order_relaxed)));
    json.set("maxVoices", Aestra::JSON(static_cast<double>(m_maxVoices.load(std::memory_order_relaxed))));
    json.set("rootMidiNote", Aestra::JSON(static_cast<double>(m_rootMidiNote.load(std::memory_order_relaxed))));
    json.set("monoMode", Aestra::JSON(m_monoMode.load(std::memory_order_relaxed)));
    json.set("glideTimeMs", Aestra::JSON(static_cast<double>(m_glideTimeMs.load(std::memory_order_relaxed))));

    // Sample Path
    {
        auto current = std::atomic_load(&m_data);
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
    if (json.has("loopEnabled")) {
        m_loopEnabled.store(json["loopEnabled"].asBool(), std::memory_order_relaxed);
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
