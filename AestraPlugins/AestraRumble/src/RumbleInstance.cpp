// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RumbleInstance.h"

#include <optional>

#include "AestraJSON.h"
#include "RumblePluginRegistration.h"
#include "RumblePresetBank.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace Aestra {
namespace Plugins {

namespace {
// Slide reaches into a ringing note only when it is on; the sketch treated anything this small as off.
constexpr double kSlideOff = 0.02;
// Level used before any note's level is known: a typical raw peak, so a first note is near the right loudness.
constexpr double kFallbackPeak = 1.2;
// The 808's own range, keys C3 to C6 (Aestra's naming, C3 = MIDI 60), is levelled as soon as the sound
// changes, nearest C4 first. Any other key is levelled when it is first played (until then it borrows the
// nearest levelled key). Levels are kept per key, not per pitch.
constexpr int kLevelRangeLow = 60;
constexpr int kLevelRangeHigh = 96;
constexpr int kLevelCentre = 72;

struct ParameterInfo {
    const char* key;
    const char* name;
    const char* shortName;
    const char* unit;
    uint32_t steps;
};
constexpr ParameterInfo kParameterInfo[RumbleInstance::kParameterCount] = {
    {"Punch", "Punch", "Punch", "st", 0},
    {"Tail", "Tail", "Tail", "s", 0},
    {"Weight", "Weight", "Weight", "%", 0},
    {"Tone", "Tone", "Tone", "Hz", 0},
    {"Dirt", "Dirt", "Dirt", "%", 0},
    {"Slide", "Slide", "Slide", "ms", 0},
    {"DropTime", "Drop time", "Drop", "ms", 0},
    {"Hold", "Hold", "Hold", "ms", 0},
    {"Curve", "Curve", "Curve", "%", 0},
    {"Second", "2nd harmonic", "2nd", "%", 0},
    {"Third", "3rd harmonic", "3rd", "%", 0},
    {"ClipType", "Clip type", "Clip", "", 2},
    {"Click", "Click", "Click", "%", 0},
    {"ClickLength", "Click length", "Clk len", "ms", 0},
    {"ClickTone", "Click tone", "Clk tone", "%", 0},
    {"Velocity", "Velocity", "Vel", "dB", 0},
};

// Transparent below 0.95, then a soft knee that never passes 0.99.
float ceiling(float x) {
    constexpr float kKnee = 0.95f, kRoom = 0.04f;
    const float a = std::fabs(x);
    if (a <= kKnee) return x;
    const float y = kKnee + kRoom * std::tanh((a - kKnee) / kRoom);
    return x < 0.0f ? -y : y;
}

std::string printf1(const char* format, double value) {
    char text[48];
    std::snprintf(text, sizeof(text), format, value);
    return text;
}
} // namespace

RumbleInstance::RumbleInstance() {
    const RumbleSound defaults = rumbleDefaultSound();
    for (size_t id = 0; id < RumbleSound::Count; ++id) {
        m_params[id].store(static_cast<float>(defaults[id]), std::memory_order_relaxed);
    }
    m_seed.store(defaults.seed, std::memory_order_relaxed);
    m_params[kVelocity].store(kDefaultVelocity, std::memory_order_relaxed);
    for (size_t note = 0; note < 128; ++note) {
        m_peak[note].store(0.0f, std::memory_order_relaxed);
        m_peakVersion[note].store(0, std::memory_order_relaxed);
        m_wanted[note].store(false, std::memory_order_relaxed);
    }
    m_worker = std::thread(&RumbleInstance::levelWorker, this);
}

RumbleInstance::~RumbleInstance() {
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_workerStop = true;
    }
    m_workerWake.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

bool RumbleInstance::initialize(double sampleRate, uint32_t maxBlockSize) {
    (void)maxBlockSize;
    const double rate = sampleRate > 1.0 ? sampleRate : 48000.0;
    const bool rateChanged = rate != m_sampleRate;
    m_sampleRate = rate;
    m_voice.prepare(rate);
    m_fading.prepare(rate);
    m_held.fill(false);
    m_heldCount = 0;
    m_workerRate.store(rate, std::memory_order_release);
    if (rateChanged) {
        soundChanged(); // a note's level depends on the sample rate
    }
    return true;
}

void RumbleInstance::shutdown() {
    m_active.store(false, std::memory_order_release);
    m_voice.prepare(m_sampleRate);
    m_fading.prepare(m_sampleRate);
    m_held.fill(false);
    m_heldCount = 0;
}

void RumbleInstance::activate() {
    m_voice.prepare(m_sampleRate);
    m_fading.prepare(m_sampleRate);
    m_held.fill(false);
    m_heldCount = 0;
    m_active.store(true, std::memory_order_release);
}

void RumbleInstance::deactivate() {
    m_active.store(false, std::memory_order_release);
    m_voice.prepare(m_sampleRate);
    m_fading.prepare(m_sampleRate);
    m_held.fill(false);
    m_heldCount = 0;
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
    if (!isActive()) {
        return;
    }

    // An audition from the editor: struck at full velocity, like a key, without touching the held keys.
    if (const int key = m_auditionKey.exchange(-1, std::memory_order_acq_rel); key >= 0) {
        if (m_voice.active()) {
            m_fading = m_voice;
            m_fading.stop();
        }
        m_voice.start(sound(), pitchOfKey(key), levelFor(key), velocityGain(getParameter(kVelocity), 127));
    }

    size_t eventIndex = 0;
    const size_t eventCount = midiInput ? midiInput->getEventCount() : 0;
    for (uint32_t i = 0; i < numFrames; ++i) {
        while (eventIndex < eventCount) {
            const auto& event = midiInput->getEvent(eventIndex);
            if (event.sampleOffset > i) {
                break;
            }
            handleMidiEvent(event); // an event past its offset still plays, now rather than never
            ++eventIndex;
        }
        if (!m_voice.active() && !m_fading.active()) {
            continue;
        }
        // A single note peaks at the levelled 0.85 and passes untouched; only a cut (new note over the fading one)
        // or a note played before its level is known can go higher, and the ceiling keeps that under 0.99.
        const float sample = ceiling(m_voice.next() + m_fading.next());
        for (uint32_t c = 0; c < numOutputChannels; ++c) {
            if (outputs[c]) {
                outputs[c][i] = sample;
            }
        }
    }
    while (eventIndex < eventCount) {
        handleMidiEvent(midiInput->getEvent(eventIndex++));
    }
}

void RumbleInstance::handleMidiEvent(const Aestra::Audio::MidiBuffer::Event& event) {
    if (event.size < 2) {
        return;
    }
    const uint8_t status = event.data[0] & 0xF0;
    const uint8_t note = event.data[1] & 0x7F;
    const uint8_t velocity = event.size >= 3 ? event.data[2] : 0;

    if (status == 0x90 && velocity > 0) {
        const bool overlapping = m_heldCount > 0;
        if (!m_held[note]) {
            m_held[note] = true;
            ++m_heldCount;
        }
        const RumbleSound now = sound();
        if (overlapping && now[RumbleSound::Slide] > kSlideOff && m_voice.active()) {
            m_voice.slideTo(pitchOfKey(note));
            return;
        }
        if (m_voice.active()) {
            m_fading = m_voice;
            m_fading.stop();
        }
        m_voice.start(now, pitchOfKey(note), levelFor(note), velocityGain(getParameter(kVelocity), velocity));
    } else if (status == 0x80 || (status == 0x90 && velocity == 0)) {
        // A one-shot rings for its own length; letting go of the key only ends the overlap that makes a slide.
        if (m_held[note]) {
            m_held[note] = false;
            --m_heldCount;
        }
    } else if (status == 0xB0 && (note == 120 || note == 123)) {
        m_voice.stop();
        m_fading.stop();
        m_held.fill(false);
        m_heldCount = 0;
    }
}

double RumbleInstance::levelFor(int note) {
    const uint32_t version = m_soundVersion.load(std::memory_order_acquire);
    if (m_peakVersion[note].load(std::memory_order_acquire) == version) {
        return m_peak[note].load(std::memory_order_relaxed);
    }
    m_wanted[note].store(true, std::memory_order_relaxed);
    for (int distance = 1; distance < 128; ++distance) {
        for (int other : {note - distance, note + distance}) {
            if (other >= 0 && other < 128 && m_peakVersion[other].load(std::memory_order_acquire) == version) {
                return m_peak[other].load(std::memory_order_relaxed);
            }
        }
    }
    const float stale = m_peak[note].load(std::memory_order_relaxed);
    return stale > 0.0f ? stale : kFallbackPeak;
}

void RumbleInstance::soundChanged() {
    m_soundVersion.fetch_add(1, std::memory_order_acq_rel);
    m_workerWake.notify_one();
}

// The next note to level for this version: one the audio thread asked for, else the next unlevelled note of
// the 808's range, nearest its centre first. -1 when there is nothing to do.
int RumbleInstance::nextNoteToLevel(uint32_t version) {
    for (int note = 0; note < 128; ++note) {
        if (m_wanted[note].load(std::memory_order_relaxed)) {
            if (m_peakVersion[note].load(std::memory_order_acquire) != version) {
                return note;
            }
            m_wanted[note].store(false, std::memory_order_relaxed);
        }
    }
    for (int distance = 0; distance <= kLevelRangeHigh - kLevelRangeLow; ++distance) {
        for (int note : {kLevelCentre - distance, kLevelCentre + distance}) {
            if (note >= kLevelRangeLow && note <= kLevelRangeHigh &&
                m_peakVersion[note].load(std::memory_order_acquire) != version) {
                return note;
            }
        }
    }
    return -1;
}

void RumbleInstance::levelWorker() {
    for (;;) {
        uint32_t version = 0;
        int note = -1;
        {
            std::unique_lock<std::mutex> lock(m_workerMutex);
            for (;;) {
                if (m_workerStop) {
                    return;
                }
                version = m_soundVersion.load(std::memory_order_acquire);
                note = nextNoteToLevel(version);
                if (note >= 0) {
                    break;
                }
                m_levelsDone.notify_all();
                // Woken by a change; the short timeout also picks up keys played outside the range, since the
                // audio thread only raises a flag and never wakes anyone.
                m_workerWake.wait_for(lock, std::chrono::milliseconds(20));
            }
        }
        const RumbleSound snapshot = sound();
        const double rate = m_workerRate.load(std::memory_order_acquire);
        if (m_soundVersion.load(std::memory_order_acquire) != version) {
            continue; // changed while being read
        }
        const double peak = RumbleVoice::rawPeak(snapshot, pitchOfKey(note), rate);
        if (m_soundVersion.load(std::memory_order_acquire) == version) {
            m_peak[note].store(static_cast<float>(peak), std::memory_order_relaxed);
            m_peakVersion[note].store(version, std::memory_order_release);
            m_wanted[note].store(false, std::memory_order_relaxed);
        }
    }
}

bool RumbleInstance::levelsReady() const {
    const uint32_t version = m_soundVersion.load(std::memory_order_acquire);
    for (int note = kLevelRangeLow; note <= kLevelRangeHigh; ++note) {
        if (m_peakVersion[note].load(std::memory_order_acquire) != version) {
            return false;
        }
    }
    return true;
}

bool RumbleInstance::waitForLevels(int timeoutMs) const {
    std::unique_lock<std::mutex> lock(m_workerMutex);
    return m_levelsDone.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return levelsReady(); });
}

RumbleSound RumbleInstance::sound() const {
    RumbleSound result;
    for (size_t id = 0; id < RumbleSound::Count; ++id) {
        result[id] = m_params[id].load(std::memory_order_relaxed);
    }
    result.seed = m_seed.load(std::memory_order_relaxed);
    return result;
}

void RumbleInstance::setSound(const RumbleSound& sound) {
    for (size_t id = 0; id < RumbleSound::Count; ++id) {
        if (!std::isfinite(sound[id])) continue; // keep the current value, as setParameter does
        m_params[id].store(static_cast<float>(std::clamp(sound[id], 0.0, 1.0)), std::memory_order_relaxed);
    }
    m_seed.store(sound.seed, std::memory_order_relaxed);
    soundChanged();
}

std::vector<Aestra::Audio::PluginParameter> RumbleInstance::getParameters() const {
    const RumbleSound defaults = rumbleDefaultSound();
    std::vector<Aestra::Audio::PluginParameter> parameters;
    parameters.reserve(kParameterCount);
    for (uint32_t id = 0; id < kParameterCount; ++id) {
        const auto& info = kParameterInfo[id];
        const float fallback = id == kVelocity ? kDefaultVelocity : static_cast<float>(defaults[id]);
        parameters.push_back(Aestra::Audio::PluginParameter{id, info.name, info.shortName, info.unit, fallback, 0.0f,
                                                            1.0f, true, false, false, info.steps});
    }
    return parameters;
}

uint32_t RumbleInstance::getParameterCount() const {
    return kParameterCount;
}

float RumbleInstance::getParameter(uint32_t id) const {
    return id < kParameterCount ? m_params[id].load(std::memory_order_relaxed) : 0.0f;
}

double RumbleInstance::velocityGain(double amount, int velocity) {
    const double hit = std::clamp(velocity, 1, 127) / 127.0;
    return std::pow(10.0, -std::clamp(amount, 0.0, 1.0) * 30.0 * (1.0 - hit) / 20.0);
}

void RumbleInstance::setParameter(uint32_t id, float value) {
    if (id >= kParameterCount || !std::isfinite(value)) {
        return;
    }
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    if (id == kVelocity) {
        m_params[id].store(clamped, std::memory_order_relaxed); // how it is played: levels do not change
        return;
    }
    if (m_params[id].exchange(clamped, std::memory_order_relaxed) != clamped) {
        soundChanged();
    }
}

std::string RumbleInstance::getParameterDisplay(uint32_t id) const {
    if (id >= kParameterCount) {
        return {};
    }
    if (id == kVelocity) {
        const double range = getParameter(kVelocity) * 30.0;
        return range < 0.5 ? std::string("Off") : printf1("%.0f dB range", range);
    }
    const RumbleSound now = sound();
    const double v = now[id];
    switch (id) {
    case RumbleSound::Punch:
        return printf1("%.0f st drop", RumbleVoice::dropSemitones(v));
    case RumbleSound::Tail:
        return printf1("%.2f s", RumbleVoice::tailSeconds(v));
    case RumbleSound::Tone: {
        const double hz = RumbleVoice::toneHz(v);
        return hz < 1000.0 ? printf1("%.0f Hz", hz) : printf1("%.1f kHz", hz / 1000.0);
    }
    case RumbleSound::Slide:
        return v <= kSlideOff ? std::string("Off") : printf1("%.0f ms", v * 300.0);
    case RumbleSound::DropTime:
        return printf1("%.0f ms", RumbleVoice::dropSeconds(now) * 1000.0);
    case RumbleSound::Hold:
        return printf1("%.0f ms", RumbleVoice::holdSeconds(v) * 1000.0);
    case RumbleSound::ClipType:
        return v < 0.34 ? "Soft" : v < 0.67 ? "Hard" : "Fold";
    case RumbleSound::ClickLength:
        return printf1("%.0f ms", 2.0 + v * 20.0);
    default:
        return printf1("%.0f %%", v * 100.0);
    }
}

const char* RumbleInstance::parameterKey(uint32_t id) {
    return id < kParameterCount ? kParameterInfo[id].key : "";
}

const char* RumbleInstance::parameterName(uint32_t id) {
    return id < kParameterCount ? kParameterInfo[id].name : "";
}

std::vector<uint8_t> RumbleInstance::stateOfSound(const RumbleSound& sound) {
    Aestra::JSON json = Aestra::JSON::object();
    json.set("schema", Aestra::JSON("rumble-sound"));
    json.set("version", Aestra::JSON(2.0));
    Aestra::JSON params = Aestra::JSON::object();
    for (uint32_t id = 0; id < RumbleSound::Count; ++id) {
        params.set(parameterKey(id), Aestra::JSON(sound[id]));
    }
    json.set("params", params);
    json.set("seed", Aestra::JSON(static_cast<double>(sound.seed)));
    const std::string text = json.toString(2);
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> RumbleInstance::saveState() const {
    Aestra::JSON json = Aestra::JSON::object();
    json.set("schema", Aestra::JSON("rumble-sound"));
    json.set("version", Aestra::JSON(2.0));
    Aestra::JSON params = Aestra::JSON::object();
    for (uint32_t id = 0; id < kParameterCount; ++id) {
        params.set(parameterKey(id), Aestra::JSON(static_cast<double>(getParameter(id))));
    }
    json.set("params", params);
    json.set("seed", Aestra::JSON(static_cast<double>(m_seed.load(std::memory_order_relaxed))));
    const std::string text = json.toString(2);
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::optional<RumbleSound> RumbleInstance::soundOfState(const std::vector<uint8_t>& state) {
    const std::string text(state.begin(), state.end());
    Aestra::JSON json = Aestra::JSON::parse(text);
    if (!json.isObject() || !json.has("schema") || !json["schema"].isString() ||
        json["schema"].asString() != "rumble-sound" || !json.has("params") || !json["params"].isObject()) {
        return std::nullopt; // includes the old engine's states: nobody used it (founder, 2026-11-12)
    }
    RumbleSound loaded = rumbleDefaultSound();
    auto& params = json["params"];
    for (uint32_t id = 0; id < RumbleSound::Count; ++id) {
        if (params.has(parameterKey(id)) && params[parameterKey(id)].isNumber()) {
            loaded[id] = params[parameterKey(id)].asNumber();
        }
    }
    if (json.has("seed") && json["seed"].isNumber()) {
        const double seed = json["seed"].asNumber();
        if (std::isfinite(seed) && seed >= 0.0 && seed <= 4294967295.0) {
            loaded.seed = static_cast<uint32_t>(seed); // anything else is a damaged file: keep the default grain
        }
    }
    return loaded;
}

bool RumbleInstance::loadState(const std::vector<uint8_t>& state) {
    const std::optional<RumbleSound> loaded = soundOfState(state);
    if (!loaded) return false;
    setSound(*loaded);
    // A project carries Velocity; a sound on its own (a factory or kept preset) does not, and leaves it as it is.
    const std::string text(state.begin(), state.end());
    Aestra::JSON json = Aestra::JSON::parse(text);
    const char* velocityKey = parameterKey(kVelocity);
    if (json["params"].has(velocityKey) && json["params"][velocityKey].isNumber()) {
        setParameter(kVelocity, static_cast<float>(json["params"][velocityKey].asNumber()));
    }
    return true;
}

const Aestra::Audio::PluginInfo& RumbleInstance::getInfo() const {
    return rumblePluginInfo();
}

uint32_t RumbleInstance::getTailSamples() const {
    return RumbleVoice::lengthSamples(sound(), m_sampleRate);
}

} // namespace Plugins
} // namespace Aestra
