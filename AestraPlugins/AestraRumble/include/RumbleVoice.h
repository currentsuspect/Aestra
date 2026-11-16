// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The Rumble 808 voice the founder approved (FD-80, SPEC-025 R2).
//
// This is the browser sketch's voice, carried over line for line, and held to it by RumbleVoiceTest against
// golden data generated from the sketch itself (tools/rumble/sketch_golden.js). If a number here changes,
// the sound changes: regenerate nothing, and treat a failing golden test as a changed instrument.
//
// One note is one finished sound: it rings for its own length whatever the key does, like a one-shot 808.
// Every sound is levelled to the same peak, so sounds compare by character and never by volume.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Aestra {
namespace Plugins {

// The fifteen settings of a Rumble sound, each 0 to 1, in the sketch's order.
struct RumbleSound {
    enum Id : uint8_t {
        Punch = 0, // how far the pitch falls at the start (up to 30 semitones), and a harder front
        Tail,      // how long it rings: 0.25 s to 3.5 s
        Weight,    // clean sine sub mixed under the shaped tone
        Tone,      // where the low-pass sits: 90 Hz to 5.4 kHz
        Dirt,      // drive into the shaper
        Slide,     // glide time between overlapping notes; 0 is off
        DropTime,  // how long the pitch drop takes (shortened so the note always lands; see dropSeconds)
        Hold,      // full level before the decay starts: 20 to 200 ms
        Curve,     // shape of the decay: rounder (low) or straighter (high)
        Second,    // second harmonic
        Third,     // third harmonic
        ClipType,  // soft (below 0.34), hard (below 0.67) or fold
        Click,     // noise click at the front
        ClickLength,
        ClickTone,
        Count
    };
    std::array<double, Count> v{};
    uint32_t seed = 1; // the click's noise; fixed per sound so a sound always sounds the same

    double& operator[](size_t id) { return v[id]; }
    double operator[](size_t id) const { return v[id]; }
};

class RumbleVoice {
public:
    static constexpr double kLevelledPeak = 0.85;
    static constexpr double kLongestSeconds = 4.2;

    // Real units, shared with the editor so the picture and the sound agree.
    static double tailSeconds(double tail);
    static double toneHz(double tone);
    static double holdSeconds(double hold);
    static double dropSemitones(double punch);
    static double dropSeconds(const RumbleSound& sound); // capped so the pitch settles before the sound is gone
    static double lengthSeconds(const RumbleSound& sound);
    static uint32_t lengthSamples(const RumbleSound& sound, double sampleRate);

    // The loudest raw sample of a note before levelling. Renders only the start of the sound, where the peak
    // is, except for fold clipping, which is rendered whole (RumbleVoiceTest checks both against full
    // renders). Up to a few milliseconds of work: never on the audio thread.
    static double rawPeak(const RumbleSound& sound, int note, double sampleRate);

    // The whole finished sound, levelled and with its end faded, exactly as the sketch makes it.
    static std::vector<float> render(const RumbleSound& sound, int note, double sampleRate);

    // Streaming, for the instrument. start() takes the level found by rawPeak so nothing heavy runs here.
    void prepare(double sampleRate);
    // level scales the whole note (velocity); 1.0 is the approved sound exactly.
    void start(const RumbleSound& sound, int note, double rawPeak, double level = 1.0);
    void slideTo(int note); // a new overlapping note with Slide on: the ringing sound bends to it
    void stop();            // a fast fade, for a note that cuts this one off
    bool active() const { return m_active; }
    float next();

private:
    double rawSample();

    double m_sampleRate = 48000.0;
    RumbleSound m_sound;
    bool m_active = false;
    uint32_t m_index = 0;
    uint32_t m_length = 0;
    uint32_t m_fadeLength = 1;
    double m_gain = 1.0;
    // Precomputed per note.
    double m_f0 = 55.0, m_tail = 1.0, m_hold = 0.1, m_drop = 0.0, m_dropTime = 0.05, m_curve = 1.0, m_drive = 1.0;
    double m_cut = 1000.0, m_clickAlpha = 0.0, m_clickDecay = 0.01;
    // Running state.
    double m_time = 0.0, m_phase = 0.0, m_low1 = 0.0, m_low2 = 0.0, m_clickLow = 0.0;
    uint32_t m_noise = 1;
    // Slide plays the rest of the sound faster or slower, as the sketch's sample playback did.
    double m_rate = 1.0, m_rateTarget = 1.0, m_rateCoeff = 0.0;
    int m_note = 36;
    // Cut-off fade.
    double m_cutFade = 1.0, m_cutFadeStep = 0.0;
};

} // namespace Plugins
} // namespace Aestra
