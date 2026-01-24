#include "Plugin/SamplerPlugin.h"
#include "IO/MiniAudioDecoder.h"
#include "GarbageCollector.h"
#include <filesystem>
#include <cmath>
#include <algorithm>
#include "AestraJSON.h"

namespace Aestra {
namespace Audio {
namespace Plugins {

SamplerPlugin::SamplerPlugin() {
    // defaults
    m_params[kParamAttack].store(0.01f); // 10ms
    m_params[kParamDecay].store(0.1f);   // 100ms
    m_params[kParamSustain].store(1.0f); // Full
    m_params[kParamRelease].store(0.1f); // 100ms
    m_params[kParamPitch].store(0.5f);   // Center (0 semitones)
}

bool SamplerPlugin::initialize(double sampleRate, uint32_t maxBlockSize) {
    m_sampleRate = sampleRate;
    return true;
}

void SamplerPlugin::shutdown() {
    m_active = false;
    // Force release of data to ensure cleanup
    m_activeData.store(nullptr, std::memory_order_release);
    auto old = m_dataHolder;
    m_dataHolder = nullptr;
    if (old) {
        GarbageCollector::instance().release(old);
    }
}

void SamplerPlugin::activate() {
    m_active = true;
    for (auto& v : m_voices) v.active = false;
}

void SamplerPlugin::deactivate() {
    m_active = false;
}

const PluginInfo& SamplerPlugin::getInfo() const {
    static PluginInfo info;
    if (info.id.empty()) {
        info.id = "com.Aestrastudios.sampler";
        info.name = "Aestra Sampler";
        info.vendor = "Aestra Studios";
        info.version = "1.0.0";
        info.category = "Instrument";
        info.format = PluginFormat::Internal;
        info.type = PluginType::Instrument;
        info.numAudioInputs = 0;
        info.numAudioOutputs = 2;
        info.hasMidiInput = true;
        info.hasMidiOutput = false;
    }
    return info;
}

// ... parameters ...

bool SamplerPlugin::loadSample(const std::string& path) {
    if (!std::filesystem::exists(path)) return false;

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

    // Context Swap:
    // 1. Update RT pointer (Atomic Store) - Wait-Free
    m_activeData.store(newData.get(), std::memory_order_release);

    // 2. Update Owner (Main Thread)
    auto oldData = m_dataHolder;
    m_dataHolder = newData;

    // 3. Defer destruction of old owner
    if (oldData) {
        GarbageCollector::instance().release(oldData);
    }
    
    return true;
}

void SamplerPlugin::process(const float* const* inputs, float** outputs,
                          uint32_t numInputChannels, uint32_t numOutputChannels,
                          uint32_t numFrames,
                          const MidiBuffer* midiInput,
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
            v.stageTime = 0.0;
            v.currentGain = 0.0f;
            v.releaseGain = 0.0f;
        }
    }
    
    // Thread-safe access to sample data
    auto* currentData = m_activeData.load(std::memory_order_acquire);
    if (!currentData || currentData->data.empty()) return;

    // Parameters
    float pitchParam = m_params[kParamPitch].load(std::memory_order_relaxed);
    float semitones = (pitchParam - 0.5f) * 24.0f;
    float pitchRatio = std::pow(2.0f, semitones / 12.0f);
    
    // Source Rate correction
    double baseRate = (double)currentData->rate / m_sampleRate;
    double rate = baseRate * pitchRatio;

    // Events
    uint32_t eventIdx = 0;
    size_t eventCount = midiInput ? midiInput->getEventCount() : 0;

    const auto& sampleVec = currentData->data;
    uint32_t channels = currentData->channels;

    for (uint32_t i = 0; i < numFrames; ++i) {
        // Handle MIDI
        while (midiInput && eventIdx < eventCount) {
            const auto& e = midiInput->getEvent(eventIdx);
            if (e.sampleOffset == i) {
                handleMidiEvent(e);
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
        
        for (auto& v : m_voices) {
            if (!v.active) continue;
            
            // Envelope
            float env = getEnvelopeLevel(v, 1.0 / m_sampleRate);
            if (v.stage == EnvStage::Off) {
                v.active = false;
                continue;
            }
            
            // Sample Lookup
            if (v.position >= sampleVec.size() / channels) {
                v.active = false;
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
                float sR = (channels > 1 && (idx * channels + 1 < sampleVec.size()))
                           ? sampleVec[idx * channels + 1]
                           : sL;
                
                // Next sample (interpolating to)
                float nL = 0.0f, nR = 0.0f;
                if ((idx + 1) * channels < sampleVec.size()) {
                    nL = sampleVec[(idx + 1) * channels];
                    nR = (channels > 1 && ((idx + 1) * channels + 1 < sampleVec.size()))
                         ? sampleVec[(idx + 1) * channels + 1]
                         : nL;
                } else {
                    // Boundary condition: hold last sample or loop? Here we hold/zero
                    nL = 0.0f; // sL; // or zero?
                    nR = 0.0f; // sR;
                }
                
                float outL = sL + frac * (nL - sL);
                float outR = sR + frac * (nR - sR);
                
                float gain = (v.velocity * v.velocity) * env; // Quadratic velocity
                L += outL * gain;
                R += outR * gain;
            }
            
            // Advance
            v.position += rate;
        }
        
        // Master Attenuation to prevent clipping with polyphony
        outputs[0][i] = L * 0.5f;
        outputs[1][i] = R * 0.5f;
    }
}

void SamplerPlugin::handleMidiEvent(const MidiBuffer::Event& event) {
    uint8_t status = event.data[0] & 0xF0;
    uint8_t note = event.data[1];
    uint8_t velocity = event.data[2];

    if (status == 0x90 && velocity > 0) { // Note On
        // Find free voice
        Voice* freeVoice = nullptr;
        Voice* oldestRelease = nullptr;
        Voice* oldestActive = nullptr;
        double maxReleaseTime = -1.0;
        double maxActiveTime = -1.0;

        for (auto& v : m_voices) {
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
            if (!freeVoice) freeVoice = &m_voices[0]; // Fallback
        }

        freeVoice->active = true;
        freeVoice->note = note;
        freeVoice->velocity = velocity / 127.0f;
        freeVoice->position = 0.0;
        freeVoice->stage = EnvStage::Attack;
        freeVoice->stageTime = 0.0;
        freeVoice->currentGain = 0.0f;
    } else if (status == 0x80 || (status == 0x90 && velocity == 0)) { // Note Off
        // [MODIFIED] One-Shot Mode Default: Ignore Note-Offs
        // User requested "patterns should play the full audio no cut unless specified"
        // Future: Add boolean parameter for OneShot vs Gate mode.
        /*
        for (auto& v : m_voices) {
            if (v.active && v.note == note && v.stage != EnvStage::Release) {
                v.stage = EnvStage::Release;
                v.stageTime = 0.0;
                v.releaseGain = v.currentGain;
            }
        }
        */
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
    params.push_back({ kParamAttack,  "Attack",  "Att", "s",  0.01f, 0.0f,  2.0f });
    params.push_back({ kParamDecay,   "Decay",   "Dec", "s",  0.1f,  0.0f,  2.0f });
    params.push_back({ kParamSustain, "Sustain", "Sus", "",   1.0f,  0.0f,  1.0f });
    params.push_back({ kParamRelease, "Release", "Rel", "s",  0.5f,  0.0f,  5.0f });
    params.push_back({ kParamPitch,   "Pitch",   "Ptc", "st", 0.0f, -24.0f, 24.0f });
    return params;
}

uint32_t SamplerPlugin::getParameterCount() const {
    return kParamCount;
}

float SamplerPlugin::getParameter(uint32_t id) const {
    if (id < kParamCount) return m_params[id].load();
    return 0.0f;
}

void SamplerPlugin::setParameter(uint32_t id, float value) {
    if (id < kParamCount) m_params[id].store(value);
}

std::string SamplerPlugin::getParameterDisplay(uint32_t id) const {
    if (id >= kParamCount) return "";
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
     
     // Sample Path
     {
         auto current = m_dataHolder; // Safe on main thread
         if (current && !current->path.empty()) {
             json.set("samplePath", Aestra::JSON(current->path));
         }
     }
     
     std::string s = json.toString();
     return std::vector<uint8_t>(s.begin(), s.end());
}

bool SamplerPlugin::loadState(const std::vector<uint8_t>& state) {
    if (state.empty()) return false;
    std::string s(state.begin(), state.end());
    
    Aestra::JSON json = Aestra::JSON::parse(s);
    if (!json.isObject()) return false;
    
    if (json["params"].isArray()) {
        const auto& pArr = json["params"];
        for (size_t i = 0; i < pArr.size() && i < kParamCount; ++i) {
             m_params[i].store(static_cast<float>(pArr[i].asNumber()));
        }
    }
    
    if (json.has("samplePath")) {
        std::string path = json["samplePath"].asString();
        if (!path.empty()) {
            loadSample(path);
        }
    }
    return true;
}

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
