// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Cut-self / voice-choke regression: a retriggered note must choke every
// previous voice in the sampler (explicit sampler-level policy), never overlap
// it, and stay disabled for ordinary samples unless the user enables it.

#include "Plugin/SamplerPlugin.h"
#include "PluginHost.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockFrames = 256;
constexpr int kBlocks = 16;
constexpr uint8_t kNote = 60;
constexpr uint8_t kVelocity = 110;
constexpr int kRetriggerBlock = 1;
// Offset 17: the 220 Hz sine peaks at global frame 272.7 (54.5 + one cycle,
// 48000/220 = 218.18 frames), i.e. offset 17 inside the retrigger block
// (block starts at 256). The choke cut lands on the old voice's maximum
// value — the loudest possible step.
constexpr int kChokeOffset = 17;
// Offset 436: two full 220 Hz cycles span 436.36 frames (48000/220 = 218.18
// frames per cycle); 256 + 180 = 436 sits 0.36 frames short of the in-phase
// point, so a non-choked overlap lands ~in-phase and measures as a clean
// ~1.414x amplitude boost instead of a phase-dependent fluke.
constexpr int kInPhaseOffset = 180; // 256 + 180 = 436

void writeLe16(std::ofstream& out, uint16_t value) {
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
}

void writeLe32(std::ofstream& out, uint32_t value) {
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
    out.put(static_cast<char>((value >> 16) & 0xff));
    out.put(static_cast<char>((value >> 24) & 0xff));
}

// 1 s of 220 Hz sine at 0.30 peak. A long one-shot: voices stay audible far
// past the retrigger window, so overlap would be plainly measurable.
bool writeMonoWav(const std::filesystem::path& path, uint32_t frames) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    constexpr uint16_t channels = 1;
    constexpr uint16_t bitsPerSample = 16;
    const uint32_t dataBytes = frames * channels * (bitsPerSample / 8);

    out.write("RIFF", 4);
    writeLe32(out, 36 + dataBytes);
    out.write("WAVEfmt ", 8);
    writeLe32(out, 16);
    writeLe16(out, 1);
    writeLe16(out, channels);
    writeLe32(out, kSampleRate);
    writeLe32(out, kSampleRate * channels * (bitsPerSample / 8));
    writeLe16(out, channels * (bitsPerSample / 8));
    writeLe16(out, bitsPerSample);
    out.write("data", 4);
    writeLe32(out, dataBytes);

    constexpr double twoPi = 6.28318530717958647692;
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSampleRate);
        const float sample = static_cast<float>(std::sin(twoPi * 220.0 * t) * 0.30);
        const auto pcm = static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
        writeLe16(out, static_cast<uint16_t>(pcm));
    }

    return true;
}

struct RenderMetrics {
    float maxAbs = 0.0f;
    float maxFirstDiff = 0.0f;
    float postRetriggerMax = 0.0f;
};

// Renders noteOn(firstNote) at frame 0 and noteOn(secondNote) at
// retriggerOffset within the retrigger block. cutSelf selects the policy;
// mono selects the mono/legato voice path.
bool renderRetrigger(const std::filesystem::path& wavPath, bool cutSelf, uint8_t firstNote,
                     uint8_t secondNote, int retriggerOffset, RenderMetrics* metrics, std::string* error,
                     bool mono = false) {
    Plugins::SamplerPlugin sampler;
    if (!sampler.initialize(kSampleRate, kBlockFrames)) {
        *error = "sampler initialize failed";
        return false;
    }
    if (!sampler.loadSample(wavPath.string())) {
        *error = "sampler failed to load mono wav";
        return false;
    }
    sampler.setRootMidiNote(60);
    sampler.setMaxVoices(8);
    sampler.setMonoMode(mono);
    sampler.setEnvelope(0.001f, 1.5f, 0.5f, 0.05f);
    sampler.setCutSelfMode(cutSelf);
    sampler.activate();

    std::vector<float> left(kBlockFrames, 0.0f);
    std::vector<float> right(kBlockFrames, 0.0f);
    float* outputs[] = {left.data(), right.data()};

    MidiBuffer firstMidi;
    firstMidi.addNoteOn(1, firstNote, kVelocity, 0);
    MidiBuffer secondMidi;
    secondMidi.addNoteOn(1, secondNote, kVelocity, retriggerOffset);

    float prevLeft = 0.0f;
    for (int block = 0; block < kBlocks; ++block) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);

        const MidiBuffer* blockMidi = block == 0 ? &firstMidi : (block == kRetriggerBlock ? &secondMidi : nullptr);
        sampler.process(nullptr, outputs, 0, 2, kBlockFrames, blockMidi, nullptr);

        for (uint32_t i = 0; i < kBlockFrames; ++i) {
            const float value = left[i];
            metrics->maxAbs = std::max(metrics->maxAbs, std::abs(value));
            if (block > 0 || i > 0) {
                metrics->maxFirstDiff = std::max(metrics->maxFirstDiff, std::abs(value - prevLeft));
            }
            if (block >= kRetriggerBlock) {
                metrics->postRetriggerMax = std::max(metrics->postRetriggerMax, std::abs(value));
            }
            prevLeft = value;
        }
    }

    if (metrics->maxAbs <= 1.0e-7f) {
        *error = "sampler output was silent";
        return false;
    }
    return true;
}

// Single note rendered for the overlap-free amplitude baseline.
bool renderSingleNote(const std::filesystem::path& wavPath, float* maxAbs, std::string* error) {
    Plugins::SamplerPlugin sampler;
    if (!sampler.initialize(kSampleRate, kBlockFrames)) {
        *error = "sampler initialize failed";
        return false;
    }
    if (!sampler.loadSample(wavPath.string())) {
        *error = "sampler failed to load mono wav";
        return false;
    }
    sampler.setRootMidiNote(60);
    sampler.setMaxVoices(8);
    sampler.setEnvelope(0.001f, 1.5f, 0.5f, 0.05f);
    sampler.activate();

    std::vector<float> left(kBlockFrames, 0.0f);
    std::vector<float> right(kBlockFrames, 0.0f);
    float* outputs[] = {left.data(), right.data()};

    MidiBuffer midi;
    midi.addNoteOn(1, kNote, kVelocity, 0);

    for (int block = 0; block < kBlocks; ++block) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        sampler.process(nullptr, outputs, 0, 2, kBlockFrames, block == 0 ? &midi : nullptr, nullptr);
        for (uint32_t i = 0; i < kBlockFrames; ++i) {
            *maxAbs = std::max(*maxAbs, std::abs(left[i]));
        }
    }
    return true;
}

} // namespace

int main() {
    std::error_code ec;
    const auto tmpDir = std::filesystem::temp_directory_path(ec);
    if (ec) {
        std::cerr << "failed to resolve temp directory: " << ec.message() << "\n";
        return 1;
    }

    const auto uniqueId =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    const auto wavPath = tmpDir / ("aestra_sampler_cutself_" + uniqueId + ".wav");
    if (!writeMonoWav(wavPath, kSampleRate)) {
        std::cerr << "failed to write mono wav\n";
        return 1;
    }

    auto fail = [&](const std::string& message) {
        std::cerr << "FAIL: " << message << "\n";
        std::filesystem::remove(wavPath, ec);
        return 1;
    };

    float singleMax = 0.0f;
    std::string error;
    if (!renderSingleNote(wavPath, &singleMax, &error)) {
        return fail("single-note baseline: " + error);
    }
    if (singleMax <= 0.0f) {
        return fail("single-note baseline silent");
    }

    RenderMetrics cutOn;
    if (!renderRetrigger(wavPath, true, kNote, kNote, kChokeOffset, &cutOn, &error)) {
        return fail("cut-self retrigger render: " + error);
    }

    // 1. Choke discontinuity: the old voice is hard-cut at the retrigger.
    //    The cut lands at the old voice's instantaneous value (~0.9x peak);
    //    smooth playback never steps that hard.
    if (cutOn.maxFirstDiff <= singleMax * 0.50f) {
        return fail("cut-self: retrigger must hard-cut the previous voice (no discontinuity seen)");
    }

    // 2. The new trigger still sounds (choke kills the OLD voice, not the new).
    if (cutOn.postRetriggerMax < 0.10f * singleMax) {
        return fail("cut-self: new voice was silenced by the choke");
    }

    // 3. No overlap: post-choke output stays at single-voice amplitude.
    if (cutOn.maxAbs > singleMax * 1.10f) {
        return fail("cut-self: output exceeded single-voice amplitude (overlap remains)");
    }

    // 4. Default (cut-self OFF): ordinary samples are untouched — retrigger
    //    stays smooth (no choke discontinuity) and the voices overlap.
    RenderMetrics defaultOff;
    if (!renderRetrigger(wavPath, false, kNote, kNote, kInPhaseOffset, &defaultOff, &error)) {
        return fail("default retrigger render: " + error);
    }
    if (defaultOff.maxFirstDiff > singleMax * 0.50f) {
        return fail("default sampler: retrigger must not choke (hard discontinuity seen)");
    }
    if (defaultOff.maxAbs <= singleMax * 1.10f) {
        return fail("default sampler: retrigger should overlap (no amplitude boost seen)");
    }
    if (cutOn.maxAbs >= defaultOff.maxAbs) {
        return fail("cut-self must reduce retrigger amplitude vs default overlap");
    }

    // 5. Different pitches are still choked: Cut-Self belongs to the sampler
    //    voice group, not to one MIDI note. This is the Piano Roll 808 case:
    //    changing pitch must not allow the prior hit to overlap the next one.
    //    Use the peak-aligned offset so the hard choke is observable rather
    //    than hiding it at the sine's zero crossing.
    RenderMetrics differentPitch;
    if (!renderRetrigger(wavPath, true, kNote, kNote + 12, kChokeOffset, &differentPitch, &error)) {
        return fail("different-pitch render: " + error);
    }
    if (differentPitch.maxFirstDiff <= singleMax * 0.50f) {
        return fail("cut-self: different-pitch trigger did not choke the playing voice");
    }
    if (differentPitch.maxAbs > singleMax * 1.10f) {
        return fail("cut-self: different-pitch output exceeded single-voice amplitude");
    }

    // 6. Mono mode: cut-self still wins over legato. With cut-self OFF a mono
    //    retrigger glides the held voice (smooth continuation, no restart);
    //    with cut-self ON the held voice is choked and the note restarts, so
    //    the retrigger shows the hard choke step and no overlap boost.
    RenderMetrics monoLegato;
    if (!renderRetrigger(wavPath, false, kNote, kNote, kChokeOffset, &monoLegato, &error, true)) {
        return fail("mono legato render: " + error);
    }
    if (monoLegato.maxFirstDiff > singleMax * 0.50f) {
        return fail("mono legato: retrigger must glide, not restart (hard step seen)");
    }
    RenderMetrics monoCutSelf;
    if (!renderRetrigger(wavPath, true, kNote, kNote, kChokeOffset, &monoCutSelf, &error, true)) {
        return fail("mono cut-self render: " + error);
    }
    if (monoCutSelf.maxFirstDiff <= singleMax * 0.50f) {
        return fail("mono cut-self: retrigger must choke the held voice (no step seen)");
    }
    if (monoCutSelf.maxAbs > singleMax * 1.10f) {
        return fail("mono cut-self: retrigger output exceeded single-voice amplitude");
    }

    // 7. State roundtrip + legacy state default.
    {
        Plugins::SamplerPlugin writer;
        writer.initialize(kSampleRate, kBlockFrames);
        if (writer.isCutSelfMode()) {
            return fail("fresh sampler must default to cut-self disabled");
        }
        writer.setCutSelfMode(true);
        const auto state = writer.saveState();

        Plugins::SamplerPlugin reader;
        reader.initialize(kSampleRate, kBlockFrames);
        if (!reader.loadState(state)) {
            return fail("cut-self state roundtrip load failed");
        }
        if (!reader.isCutSelfMode()) {
            return fail("cut-self flag lost in state roundtrip");
        }
    }
    {
        // Older projects have no cutSelfMode key — must load as disabled.
        Plugins::SamplerPlugin legacy;
        legacy.initialize(kSampleRate, kBlockFrames);
        const std::string legacyJson = "{\"params\":[0.0,0.0,0.0,0.0,0.0]}";
        if (!legacy.loadState(std::vector<uint8_t>(legacyJson.begin(), legacyJson.end()))) {
            return fail("legacy state load failed");
        }
        if (legacy.isCutSelfMode()) {
            return fail("legacy state without cutSelfMode must default to disabled");
        }
    }

    std::filesystem::remove(wavPath, ec);
    std::cout << "sampler cut-self: retrigger chokes, no overlap, polyphony and ADSR unaffected\n";
    return 0;
}
