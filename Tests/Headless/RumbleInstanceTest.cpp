// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The Rumble instrument plays the approved voice (FD-80, SPEC-025 R2): note for note the same samples as
// RumbleVoice::render, at any block size, levelled, mono-identical, with slide, cut-off and saved sounds.

#include "Plugin/PluginHost.h"
#include "RumbleInstance.h"
#include "RumblePresetBank.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using Aestra::Audio::MidiBuffer;
using namespace Aestra::Plugins;

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "failed: " << message << '\n';
    }
}

struct Note {
    uint64_t frame;
    bool on;
    uint8_t pitch;
    uint8_t velocity = 127; // full velocity plays the approved sound exactly
};

// Plays notes through process() in blocks and returns the left channel; checks right equals left.
std::vector<float> play(RumbleInstance& rumble, const std::vector<Note>& notes, uint64_t frames, uint32_t block) {
    std::vector<float> out;
    out.reserve(frames);
    std::vector<float> left(block), right(block);
    float* outputs[2] = {left.data(), right.data()};
    for (uint64_t start = 0; start < frames; start += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(block, frames - start));
        MidiBuffer midi;
        for (const Note& note : notes) {
            if (note.frame >= start && note.frame < start + n) {
                const uint32_t offset = static_cast<uint32_t>(note.frame - start);
                if (note.on) {
                    midi.addNoteOn(1, note.pitch, note.velocity, offset);
                } else {
                    midi.addNoteOff(1, note.pitch, 0, offset);
                }
            }
        }
        rumble.process(nullptr, outputs, 0, 2, n, &midi, nullptr);
        for (uint32_t i = 0; i < n; ++i) {
            if (left[i] != right[i] || !std::isfinite(left[i])) {
                check(false, "output is finite and the same on both sides");
                return out;
            }
            out.push_back(left[i]);
        }
    }
    return out;
}

void ready(RumbleInstance& rumble, double rate, uint32_t block) {
    rumble.initialize(rate, block);
    rumble.activate();
    check(rumble.waitForLevels(30000), "levels for C1 to C4 are found");
}

void playsTheApprovedVoiceExactly() {
    // Every starting sound at two notes in one block size, and the first at every block size and more notes.
    for (size_t index = 0; index < kRumbleFactoryPresets.size(); ++index) {
        const auto& preset = kRumbleFactoryPresets[index];
        const RumbleSound sound = rumbleSoundOf(preset);
        const std::vector<int> notes = index == 0 ? std::vector<int>{24, 29, 36, 43} : std::vector<int>{29, 36};
        const std::vector<uint32_t> blocks = index == 0 ? std::vector<uint32_t>{1, 64, 127, 512} : std::vector<uint32_t>{127};
        for (int note : notes) {
            const auto reference = RumbleVoice::render(sound, note, 48000.0);
            for (uint32_t block : blocks) {
                RumbleInstance rumble;
                rumble.setSound(sound);
                ready(rumble, 48000.0, block);
                const auto out = play(rumble, {{0, true, static_cast<uint8_t>(note)}, {4800, false, static_cast<uint8_t>(note)}},
                                      reference.size() + 2000, block);
                double worst = 0.0;
                for (size_t i = 0; i < out.size(); ++i) {
                    const double expected = i < reference.size() ? reference[i] : 0.0;
                    worst = std::max(worst, std::fabs(out[i] - expected));
                }
                check(worst < 1.0e-6, std::string(preset.name) + " note " + std::to_string(note) + " block " +
                                          std::to_string(block) + " plays the approved sound (worst " + std::to_string(worst) + ")");
            }
        }
    }
}

void everyNoteIsLevelled() {
    RumbleInstance rumble;
    rumble.setSound(rumbleSoundOf(kRumbleFactoryPresets[5])); // Hard clip: the loudest raw sound
    ready(rumble, 44100.0, 256);
    for (int note = 24; note <= 60; note += 5) {
        const auto out = play(rumble, {{0, true, static_cast<uint8_t>(note)}}, 44100 * 5, 256);
        float peak = 0.0f;
        for (float v : out) {
            peak = std::max(peak, std::fabs(v));
        }
        check(std::fabs(peak - 0.85f) < 1.0e-4f, "note " + std::to_string(note) + " peaks at the levelled 0.85, got " + std::to_string(peak));
    }
}

void aKeyOutsideTheRangeIsLevelledWhenPlayed() {
    RumbleInstance rumble;
    ready(rumble, 48000.0, 128);
    play(rumble, {{0, true, 84}}, 48000, 128); // C6: borrows the nearest level and asks for its own
    bool levelled = false;
    for (int i = 0; i < 400 && !levelled; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto out = play(rumble, {{0, true, 84}}, 48000 * 2, 128);
        float peak = 0.0f;
        for (float v : out) {
            peak = std::max(peak, std::fabs(v));
        }
        levelled = std::fabs(peak - 0.85f) < 1.0e-4f;
    }
    check(levelled, "a key played outside C1 to C4 gets its own exact level soon after");
}

void aNewNoteCutsTheOldOneCleanly() {
    RumbleInstance rumble;
    rumble.setSound(rumbleSoundOf(kRumbleFactoryPresets[3])); // Long tail: still loud when cut
    ready(rumble, 48000.0, 256);
    const auto out = play(rumble, {{0, true, 29}, {200, false, 29}, {24000, true, 31}}, 72000, 256);
    float peak = 0.0f;
    for (float v : out) {
        peak = std::max(peak, std::fabs(v));
    }
    check(peak < 1.7f, "the outgoing note fades under the new one instead of stacking");
    RumbleInstance solo;
    solo.setSound(rumbleSoundOf(kRumbleFactoryPresets[3]));
    ready(solo, 48000.0, 256);
    const auto alone = play(solo, {{0, true, 31}}, 48000, 256);
    // 30 ms after the cut the old note is gone: only the new note sounds.
    double worst = 0.0;
    for (size_t i = 24000 + 1440; i < 24000 + 4800; ++i) {
        worst = std::max(worst, static_cast<double>(std::fabs(out[i] - alone[i - 24000])));
    }
    check(worst < 1.0e-6, "the old note is gone 30 ms after a new one starts");
}

void slideOnlyWhenNotesOverlapAndSlideIsOn() {
    RumbleSound glide = rumbleSoundOf(kRumbleFactoryPresets[6]); // Glide lead: Slide 0.5
    RumbleInstance rumble;
    rumble.setSound(glide);
    ready(rumble, 48000.0, 64);
    const auto slid = play(rumble, {{0, true, 29}, {9600, true, 36}, {14400, false, 29}}, 30000, 64);
    RumbleInstance fresh;
    fresh.setSound(glide);
    ready(fresh, 48000.0, 64);
    const auto restarted = play(fresh, {{0, true, 29}, {9000, false, 29}, {9600, true, 36}}, 30000, 64);
    double differs = 0.0;
    for (size_t i = 9600; i < 14400; ++i) {
        differs = std::max(differs, static_cast<double>(std::fabs(slid[i] - restarted[i])));
    }
    check(differs > 0.05, "an overlapping note with Slide on bends the ringing note instead of starting again");

    glide[RumbleSound::Slide] = 0.0;
    RumbleInstance off;
    off.setSound(glide);
    ready(off, 48000.0, 64);
    RumbleInstance offFresh;
    offFresh.setSound(glide);
    ready(offFresh, 48000.0, 64);
    const auto a = play(off, {{0, true, 29}, {9600, true, 36}}, 20000, 64);
    const auto b = play(offFresh, {{0, true, 29}, {9000, false, 29}, {9600, true, 36}}, 20000, 64);
    double same = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        same = std::max(same, static_cast<double>(std::fabs(a[i] - b[i])));
    }
    check(same < 1.0e-6, "with Slide off an overlapping note simply starts again");
}

void soundsSaveAndLoad() {
    RumbleInstance a;
    RumbleSound sound = rumbleSoundOf(kRumbleFactoryPresets[2]);
    sound[RumbleSound::Hold] = 0.77;
    sound.seed = 123456789u;
    a.setSound(sound);
    const auto state = a.saveState();
    RumbleInstance b;
    check(b.loadState(state), "a saved sound loads");
    const RumbleSound loaded = b.sound();
    for (size_t id = 0; id < RumbleSound::Count; ++id) {
        check(std::fabs(loaded[id] - static_cast<float>(sound[id])) < 1.0e-6, std::string("setting ") + RumbleInstance::parameterKey(static_cast<uint32_t>(id)) + " survives saving");
    }
    check(loaded.seed == sound.seed, "the click grain survives saving");
    const std::string oldEngine = R"({"schema":"rumble-preset","version":1,"params":{"AmpDecay":0.5}})";
    check(!b.loadState(std::vector<uint8_t>(oldEngine.begin(), oldEngine.end())), "the old engine's state is not taken as a sound");
    check(b.getParameterCount() == RumbleInstance::kParameterCount && b.getParameters().size() == RumbleInstance::kParameterCount, "fifteen sound settings and Velocity");
    b.setParameter(0, 7.0f);
    check(b.getParameter(0) == 1.0f, "settings are kept in range");
    check(!b.getParameterDisplay(RumbleSound::ClipType).empty(), "every setting has words");
}

void softerHitsPlayQuieter() {
    auto peakOf = [](const std::vector<float>& out) {
        float peak = 0.0f;
        for (float v : out) {
            peak = std::max(peak, std::fabs(v));
        }
        return peak;
    };
    for (float amount : {0.0f, 0.5f, 1.0f}) {
        RumbleInstance rumble;
        rumble.setParameter(RumbleInstance::kVelocity, amount);
        ready(rumble, 48000.0, 256);
        const float full = peakOf(play(rumble, {{0, true, 36, 127}}, 48000 * 2, 256));
        const float half = peakOf(play(rumble, {{0, true, 36, 64}}, 48000 * 2, 256));
        const float soft = peakOf(play(rumble, {{0, true, 36, 1}}, 48000 * 2, 256));
        check(std::fabs(full - 0.85f) < 1.0e-4f, "a full-velocity hit is the levelled sound at any Velocity setting");
        const double halfDb = 20.0 * std::log10(half / full);
        const double softDb = 20.0 * std::log10(soft / full);
        const double expectHalf = -amount * 30.0 * (1.0 - 64.0 / 127.0);
        const double expectSoft = -amount * 30.0 * (1.0 - 1.0 / 127.0);
        check(std::fabs(halfDb - expectHalf) < 0.01 && std::fabs(softDb - expectSoft) < 0.01,
              "Velocity " + std::to_string(amount) + ": velocity 64 is " + std::to_string(halfDb) + " dB, velocity 1 is " +
                  std::to_string(softDb) + " dB");
    }
    RumbleInstance saved;
    saved.setParameter(RumbleInstance::kVelocity, 0.8f);
    RumbleInstance loaded;
    loaded.loadState(saved.saveState());
    check(std::fabs(loaded.getParameter(RumbleInstance::kVelocity) - 0.8f) < 1.0e-6f, "Velocity is saved with the sound");
}

void inactiveIsSilent() {
    RumbleInstance rumble;
    rumble.initialize(48000.0, 64);
    std::vector<float> left(64, 0.3f), right(64, -0.3f);
    float* outputs[2] = {left.data(), right.data()};
    MidiBuffer midi;
    midi.addNoteOn(1, 36, 100, 0);
    rumble.process(nullptr, outputs, 0, 2, 64, &midi, nullptr);
    check(std::all_of(left.begin(), left.end(), [](float v) { return v == 0.0f; }), "an inactive Rumble writes silence");
}

void costsLittleToPlay() {
    RumbleInstance rumble;
    ready(rumble, 48000.0, 512);
    std::vector<Note> notes;
    for (uint64_t f = 0; f < 48000 * 10; f += 6000) {
        notes.push_back({f, true, static_cast<uint8_t>(29 + (f / 6000) % 8)});
        notes.push_back({f + 3000, false, static_cast<uint8_t>(29 + (f / 6000) % 8)});
    }
    const auto start = std::chrono::steady_clock::now();
    play(rumble, notes, 48000 * 10, 512);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "10 s of notes played in " << seconds * 1000.0 << " ms (" << seconds * 10.0 << " % of one core)\n";
    check(seconds < 2.0, "playing costs well under real time");
}
} // namespace

int main() {
    playsTheApprovedVoiceExactly();
    everyNoteIsLevelled();
    aKeyOutsideTheRangeIsLevelledWhenPlayed();
    aNewNoteCutsTheOldOneCleanly();
    slideOnlyWhenNotesOverlapAndSlideIsOn();
    soundsSaveAndLoad();
    softerHitsPlayQuieter();
    inactiveIsSilent();
    costsLittleToPlay();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "RumbleInstanceTest passed\n";
    return 0;
}
