// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Factory presets for Aestra's own plugins (FD-72: 8 to 10 per plugin). Each preset names its parameters the way the
// plugin's ParamSpec table does, so a table here reads like the plugin's knobs and survives an id being renumbered.
// A parameter left out keeps the plugin's default. Values are written in the units the knob shows (dB, Hz, ms, note
// divisions) through the small converters below, which invert each parameter's display mapping; FactoryPresetsTest
// checks every name resolves and every value lands in range.
//
// Tags are words a track name might share ("vocal", "drums", "808", "master"): the browser puts presets whose tags
// match the track first.

#include "Presets/Preset.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Presets {

namespace {

using Values = std::initializer_list<std::pair<const char*, float>>;
using Tags = std::initializer_list<const char*>;

Preset make(const char* name, const char* category, const char* description, Values values, Tags tags = {}) {
    Preset p;
    p.name = name;
    p.category = category;
    p.description = description;
    p.source = PresetSource::Factory;
    for (const auto& [param, value] : values)
        p.params.push_back({PresetParam::kNoId, param, std::clamp(value, 0.0f, 1.0f)});
    for (const char* t : tags) p.tags.push_back(t);
    return p;
}

// ---- converters: display units -> normalized -------------------------------------------------------------------

float lin(float v, float lo, float hi) { return (v - lo) / (hi - lo); }
float logf01(float v, float lo, float hi) { return std::log(v / lo) / std::log(hi / lo); }
float bipolarDb(float db, float range) { return 0.5f + db / (2.0f * range); } // -range..+range around 0.5
float pct(float percent) { return percent / 100.0f; }
float bipolarPct(float percent) { return 0.5f + percent / 200.0f; }
float step(int index, int count) { return static_cast<float>(index) / static_cast<float>(count - 1); }

// Note divisions. The delay and the LFO list them in different orders (the delay's is older); each has its own.
enum class Div { Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond, HalfDot, QuarterDot, EighthDot, SixteenthDot,
                 QuarterTrip, EighthTrip, SixteenthTrip };
float delayDiv(Div d) {
    static const Div order[] = {Div::Whole,      Div::Half,      Div::Quarter,     Div::Sixteenth, Div::Eighth,
                                Div::ThirtySecond, Div::HalfDot, Div::QuarterDot,  Div::EighthDot, Div::SixteenthDot,
                                Div::QuarterTrip, Div::EighthTrip, Div::SixteenthTrip};
    for (int i = 0; i < 13; ++i)
        if (order[i] == d) return step(i, 13);
    return 0.0f;
}
float lfoDiv(Div d) { return step(static_cast<int>(d), 13); }

// ---- per plugin ---------------------------------------------------------------------------------------------------

std::vector<Preset> comp() {
    auto thr = [](float db) { return lin(db, -60.0f, 0.0f); };
    auto ratio = [](float r) { return (r - 1.0f) / 20.0f; };
    auto att = [](float ms) { return lin(ms, 0.0f, 100.0f); };
    auto rel = [](float ms) { return lin(ms, 10.0f, 1000.0f); };
    auto gain24 = [](float db) { return lin(db, 0.0f, 24.0f); }; // makeup, knee
    const float clean = 0.0f, classic = 0.5f, optical = 1.0f;
    return {
        make("Vocal Leveler", "Vocals", "Smooth optical levelling that keeps a vocal steady",
             {{"Threshold", thr(-24)}, {"Ratio", ratio(3)}, {"Attack", att(10)}, {"Release", rel(150)},
              {"Knee", gain24(7)}, {"Makeup Gain", gain24(5)}, {"Mode", optical}},
             {"vocal", "vox", "voice"}),
        make("Vocal Upfront", "Vocals", "Faster and firmer: pushes the vocal to the front",
             {{"Threshold", thr(-20)}, {"Ratio", ratio(5)}, {"Attack", att(3)}, {"Release", rel(80)},
              {"Knee", gain24(2)}, {"Makeup Gain", gain24(7)}, {"Mode", classic}},
             {"vocal", "vox", "rap"}),
        make("Drum Bus Glue", "Drums", "Gentle glue that lets the hits through",
             {{"Threshold", thr(-18)}, {"Ratio", ratio(2)}, {"Attack", att(30)}, {"Release", rel(100)},
              {"Knee", gain24(5)}, {"Makeup Gain", gain24(2)}, {"Mode", classic}},
             {"drums", "drum", "bus"}),
        make("Punchy Kick", "Drums", "Slow attack keeps the click, fast release keeps it tight",
             {{"Threshold", thr(-18)}, {"Ratio", ratio(5)}, {"Attack", att(25)}, {"Release", rel(60)},
              {"Makeup Gain", gain24(2)}, {"Mode", clean}},
             {"kick", "drums"}),
        make("Snare Snap", "Drums", "Firm control with the crack left on top",
             {{"Threshold", thr(-24)}, {"Ratio", ratio(7)}, {"Attack", att(15)}, {"Release", rel(80)},
              {"Makeup Gain", gain24(5)}, {"Mode", clean}},
             {"snare", "clap", "drums"}),
        make("Parallel Smash", "Drums", "Crushed hard and blended under the dry drums",
             {{"Threshold", thr(-36)}, {"Ratio", ratio(16)}, {"Attack", att(1)}, {"Release", rel(120)},
              {"Makeup Gain", gain24(12)}, {"Mix", pct(40)}, {"Mode", classic}},
             {"drums", "parallel"}),
        make("Bass Control", "Bass", "Evens out the notes so every one is heard",
             {{"Threshold", thr(-20)}, {"Ratio", ratio(4)}, {"Attack", att(20)}, {"Release", rel(200)},
              {"Makeup Gain", gain24(4)}, {"Mode", optical}},
             {"bass"}),
        make("808 Tamer", "Bass", "Holds the boom without losing the punch",
             {{"Threshold", thr(-12)}, {"Ratio", ratio(3)}, {"Attack", att(40)}, {"Release", rel(300)},
              {"Makeup Gain", gain24(2)}, {"Mode", clean}},
             {"808", "bass", "sub"}),
        make("Acoustic Natural", "Instruments", "Light, natural control for guitars and keys",
             {{"Threshold", thr(-18)}, {"Ratio", ratio(2)}, {"Attack", att(20)}, {"Release", rel(250)},
              {"Knee", gain24(7)}, {"Makeup Gain", gain24(2)}, {"Mode", optical}},
             {"guitar", "keys", "piano", "acoustic"}),
        make("Mix Bus Gentle", "Mix", "A dB or two of glue on the whole mix",
             {{"Threshold", thr(-12)}, {"Ratio", ratio(2)}, {"Attack", att(30)}, {"Release", rel(300)},
              {"Knee", gain24(10)}, {"Makeup Gain", gain24(1)}, {"Detector HPF", 0.2f}, {"Mode", optical}},
             {"master", "mix", "bus"}),
    };
}

std::vector<Preset> delay() {
    auto ms = [](float t) { return lin(t, 10.0f, 2000.0f); };
    auto fb = [](float percent) { return percent / 95.0f; };
    auto lowCut = [](float hz) { return logf01(hz, 20.0f, 2000.0f); };
    auto rate = [](float hz) { return lin(hz, 0.1f, 10.0f); };
    const float sync = 1.0f, pingPong = 1.0f;
    return {
        make("Slapback", "Short", "One quick repeat, the classic rockabilly vocal",
             {{"Sync", 0.0f}, {"Time", ms(110)}, {"Feedback", 0.0f}, {"Damping", pct(30)}, {"Low Cut", lowCut(126)},
              {"Mix", pct(25)}},
             {"vocal", "guitar"}),
        make("Wide Doubler", "Short", "A very short, modulated repeat that widens a mono part",
             {{"Sync", 0.0f}, {"Time", ms(30)}, {"Feedback", 0.0f}, {"Stereo", 1.0f}, {"Mod Depth", pct(20)},
              {"Mod Rate", rate(1.1f)}, {"Mix", pct(35)}},
             {"vocal", "wide", "guitar"}),
        make("Quarter Echo", "Synced", "One echo a beat, darkened so it sits behind",
             {{"Sync", sync}, {"Division", delayDiv(Div::Quarter)}, {"Feedback", fb(29)}, {"Damping", pct(40)},
              {"Low Cut", lowCut(200)}, {"Mix", pct(25)}},
             {"vocal", "lead"}),
        make("Dotted Eighth", "Synced", "The rolling dotted-eighth echo for guitars and keys",
             {{"Sync", sync}, {"Division", delayDiv(Div::EighthDot)}, {"Feedback", fb(38)}, {"Damping", pct(35)},
              {"Low Cut", lowCut(200)}, {"Mix", pct(22)}},
             {"guitar", "keys", "synth"}),
        make("Ping-Pong Eighths", "Synced", "Eighth-note echoes bouncing left and right",
             {{"Sync", sync}, {"Division", delayDiv(Div::Eighth)}, {"Stereo Mode", pingPong}, {"Feedback", fb(38)},
              {"Damping", pct(30)}, {"Mix", pct(25)}},
             {"wide", "synth", "keys"}),
        make("Triplet Bounce", "Synced", "Eighth-note triplets that swing against the beat",
             {{"Sync", sync}, {"Division", delayDiv(Div::EighthTrip)}, {"Feedback", fb(29)}, {"Damping", pct(30)},
              {"Mix", pct(20)}},
             {"drums", "hats", "perc"}),
        make("Vocal Throw", "FX", "Long, filtered repeats for the end of a phrase",
             {{"Sync", sync}, {"Division", delayDiv(Div::Quarter)}, {"Feedback", fb(48)}, {"Damping", pct(50)},
              {"Low Cut", lowCut(317)}, {"Mix", pct(35)}},
             {"vocal", "vox", "throw"}),
        make("Tape Echo", "Character", "Warbling, darkening repeats like an old tape machine",
             {{"Sync", sync}, {"Division", delayDiv(Div::QuarterDot)}, {"Feedback", fb(48)}, {"Damping", pct(60)},
              {"Mod Depth", pct(25)}, {"Mod Rate", rate(1.1f)}, {"Low Cut", lowCut(126)}, {"Mix", pct(30)}},
             {"tape", "lofi", "vintage"}),
        make("Dub Space", "Character", "Long feedback that keeps ringing out",
             {{"Sync", sync}, {"Division", delayDiv(Div::HalfDot)}, {"Feedback", fb(67)}, {"Damping", pct(55)},
              {"Low Cut", lowCut(200)}, {"Mix", pct(30)}},
             {"dub", "fx"}),
        make("Lo-Fi Haze", "Character", "Murky, wobbling echoes for lo-fi beats",
             {{"Sync", sync}, {"Division", delayDiv(Div::Quarter)}, {"Feedback", fb(57)}, {"Damping", pct(80)},
              {"Mod Depth", pct(40)}, {"Mod Rate", rate(0.5f)}, {"Low Cut", lowCut(502)}, {"Mix", pct(25)}},
             {"lofi", "keys", "chill"}),
    };
}

std::vector<Preset> drift() {
    auto semis = [](float st) { return bipolarDb(st, 12.0f); };
    auto cents = [](float ct) { return 0.5f + ct / 200.0f; };
    auto motion = [](float ct) { return std::sqrt(std::max(0.0f, ct) / 25.0f); }; // display rises with the square
    auto mrate = [](float hz) { return logf01(hz, 0.05f, 2.0f); };
    auto grainMs = [](float g) { return 0.1f + (36.0f - g) / 31.0f * 0.9f; };  // 36 ms at 0.1 .. 5 ms at 1.0
    return {
        make("Wide Double", "Width", "A slightly detuned, moving copy spread wide",
             {{"Fine Tune", cents(8)}, {"Stereo Spread", pct(60)}, {"Motion", motion(4)}, {"Motion Rate", mrate(0.22f)},
              {"Mix", pct(40)}},
             {"vocal", "wide", "double"}),
        make("Chorus Drift", "Width", "Slow pitch motion for a soft chorus",
             {{"Motion", motion(12)}, {"Motion Rate", mrate(0.66f)}, {"Stereo Spread", pct(70)}, {"Mix", pct(50)}},
             {"keys", "guitar", "synth"}),
        make("Detune Pad", "Width", "Wide, detuned shimmer for pads and chords",
             {{"Fine Tune", cents(15)}, {"Stereo Spread", pct(100)}, {"Motion", motion(6)}, {"Motion Rate", mrate(0.15f)},
              {"Mix", pct(50)}},
             {"pad", "keys", "chords"}),
        make("Octave Down", "Pitch", "Adds the octave below for weight",
             {{"Pitch", semis(-12)}, {"Grain", 0.0f}, {"Mix", pct(50)}},
             {"bass", "808", "synth"}),
        make("Octave Up Shimmer", "Pitch", "A soft octave above, spread and textured",
             {{"Pitch", semis(12)}, {"Stereo Spread", pct(50)}, {"Texture", pct(20)}, {"Mix", pct(30)}},
             {"keys", "pad", "shimmer"}),
        make("Fifth Stack", "Pitch", "A fifth above stacked under the part",
             {{"Pitch", semis(7)}, {"Mix", pct(35)}},
             {"synth", "lead"}),
        make("Fourth Below", "Pitch", "A low harmony a fourth under the vocal",
             {{"Pitch", semis(-5)}, {"Stereo Spread", pct(40)}, {"Mix", pct(30)}},
             {"vocal", "harmony"}),
        make("Tape Wobble", "Character", "Slow wow and flutter, fully wet",
             {{"Motion", motion(9)}, {"Motion Rate", mrate(0.32f)}, {"Texture", pct(30)}, {"Mix", pct(100)}},
             {"lofi", "tape", "vintage"}),
        make("Grainy Texture", "Character", "Short grains that roughen the sound",
             {{"Grain", grainMs(16)}, {"Texture", pct(60)}, {"Mix", pct(40)}},
             {"texture", "fx"}),
        make("Chipmunk", "FX", "An octave up, all wet",
             {{"Pitch", semis(12)}, {"Grain", grainMs(12)}, {"Mix", pct(100)}},
             {"fx", "vocal"}),
    };
}

std::vector<Preset> eq() {
    auto hp = [](float hz) { return logf01(hz, 20.0f, 500.0f); };
    auto ls = [](float hz) { return logf01(hz, 40.0f, 1000.0f); };
    auto b1 = [](float hz) { return logf01(hz, 80.0f, 8000.0f); };
    auto b2 = [](float hz) { return logf01(hz, 200.0f, 16000.0f); };
    auto hs = [](float hz) { return logf01(hz, 2000.0f, 20000.0f); };
    auto lp = [](float hz) { return logf01(hz, 1000.0f, 20000.0f); };
    auto g = [](float db) { return bipolarDb(db, 18.0f); };
    auto q = [](float v) { return lin(v, 0.1f, 10.0f); };
    auto slope = [](int dbPerOct) {
        static const int s[] = {6, 12, 24, 36, 48, 72, 96};
        for (int i = 0; i < 7; ++i)
            if (s[i] == dbPerOct) return step(i, 7);
        return 0.0f;
    };
    const float on = 1.0f;
    return {
        make("Vocal Clarity", "Vocals", "Cleans the low end, lifts presence and air",
             {{"High-Pass Enable", on}, {"High-Pass Frequency", hp(90)}, {"High-Pass Slope", slope(24)},
              {"Bell 1 Enable", on}, {"Bell 1 Frequency", b1(300)}, {"Bell 1 Gain", g(-3)}, {"Bell 1 Q", q(1.0f)},
              {"Bell 2 Enable", on}, {"Bell 2 Frequency", b2(3000)}, {"Bell 2 Gain", g(2.5f)}, {"Bell 2 Q", q(1.0f)},
              {"High Shelf Enable", on}, {"High Shelf Frequency", hs(10000)}, {"High Shelf Gain", g(3)}},
             {"vocal", "vox", "voice"}),
        make("Vocal De-Mud", "Vocals", "Takes the boxy low mids out of a vocal",
             {{"High-Pass Enable", on}, {"High-Pass Frequency", hp(100)}, {"High-Pass Slope", slope(24)},
              {"Bell 1 Enable", on}, {"Bell 1 Frequency", b1(250)}, {"Bell 1 Gain", g(-4)}, {"Bell 1 Q", q(1.4f)}},
             {"vocal", "vox"}),
        make("Kick Punch", "Drums", "More thump, less cardboard, a little beater click",
             {{"Low Shelf Enable", on}, {"Low Shelf Frequency", ls(60)}, {"Low Shelf Gain", g(3)},
              {"Bell 1 Enable", on}, {"Bell 1 Frequency", b1(350)}, {"Bell 1 Gain", g(-4)}, {"Bell 1 Q", q(1.5f)},
              {"Bell 2 Enable", on}, {"Bell 2 Frequency", b2(4000)}, {"Bell 2 Gain", g(3)}, {"Bell 2 Q", q(1.2f)}},
             {"kick", "drums"}),
        make("Snare Body & Crack", "Drums", "Fattens the body and sharpens the crack",
             {{"High-Pass Enable", on}, {"High-Pass Frequency", hp(80)}, {"High-Pass Slope", slope(12)},
              {"Bell 1 Enable", on}, {"Bell 1 Frequency", b1(200)}, {"Bell 1 Gain", g(3)}, {"Bell 1 Q", q(1.2f)},
              {"Bell 2 Enable", on}, {"Bell 2 Frequency", b2(5000)}, {"Bell 2 Gain", g(3)}, {"Bell 2 Q", q(1.0f)}},
             {"snare", "clap", "drums"}),
        make("Hi-Hat Tame", "Drums", "Removes low spill and softens the harsh top",
             {{"High-Pass Enable", on}, {"High-Pass Frequency", hp(300)}, {"High-Pass Slope", slope(12)},
              {"Bell 2 Enable", on}, {"Bell 2 Frequency", b2(6700)}, {"Bell 2 Gain", g(-3)}, {"Bell 2 Q", q(2.0f)}},
             {"hats", "hat", "cymbal", "drums"}),
        make("808 Clean Low End", "Bass", "Clears rumble and mud so the 808 hits clean",
             {{"High-Pass Enable", on}, {"High-Pass Frequency", hp(30)}, {"High-Pass Slope", slope(24)},
              {"Bell 1 Enable", on}, {"Bell 1 Frequency", b1(250)}, {"Bell 1 Gain", g(-3)}, {"Bell 1 Q", q(1.0f)}},
             {"808", "bass", "sub"}),
        make("Bright Air", "Tone", "Opens up the top with a gentle shelf",
             {{"High Shelf Enable", on}, {"High Shelf Frequency", hs(12000)}, {"High Shelf Gain", g(4)}},
             {"air", "vocal", "master"}),
        make("Warm Low End", "Tone", "A little more weight, a little less fizz",
             {{"Low Shelf Enable", on}, {"Low Shelf Frequency", ls(120)}, {"Low Shelf Gain", g(3)},
              {"High Shelf Enable", on}, {"High Shelf Frequency", hs(8000)}, {"High Shelf Gain", g(-2)}},
             {"warm", "keys", "master"}),
        make("Telephone", "FX", "Narrow band, like a voice down a phone line",
             {{"High-Pass Enable", on}, {"High-Pass Frequency", hp(300)}, {"High-Pass Slope", slope(24)},
              {"Low-Pass Enable", on}, {"Low-Pass Frequency", lp(3000)}, {"Low-Pass Slope", slope(24)},
              {"Bell 2 Enable", on}, {"Bell 2 Frequency", b2(1500)}, {"Bell 2 Gain", g(4)}, {"Bell 2 Q", q(1.0f)}},
             {"fx", "vocal", "lofi"}),
        make("Mix Bus Polish", "Mix", "Small, broad moves for the whole mix",
             {{"High-Pass Enable", on}, {"High-Pass Frequency", hp(25)}, {"High-Pass Slope", slope(12)},
              {"Low Shelf Enable", on}, {"Low Shelf Frequency", ls(100)}, {"Low Shelf Gain", g(1.5f)},
              {"Bell 1 Enable", on}, {"Bell 1 Frequency", b1(400)}, {"Bell 1 Gain", g(-1)}, {"Bell 1 Q", q(0.8f)},
              {"High Shelf Enable", on}, {"High Shelf Frequency", hs(10000)}, {"High Shelf Gain", g(2)}},
             {"master", "mix", "bus"}),
    };
}

std::vector<Preset> filter() {
    auto cutoff = [](float hz) { return logf01(hz, 20.0f, 20000.0f); };
    auto res = [](float qv) { return logf01(qv, 0.5f, 10.0f); };
    auto drive = [](float db) { return lin(db, 0.0f, 18.0f); };
    auto att = [](float ms) { return logf01(ms, 0.1f, 100.0f); };
    auto rel = [](float ms) { return logf01(ms, 5.0f, 1000.0f); };
    const float lowPass = 0.0f, bandPass = 0.5f, highPass = 1.0f;
    return {
        make("Low-Pass Warm", "Tone", "Rounds off the top for a softer sound",
             {{"Type", lowPass}, {"Cutoff", cutoff(2500)}, {"Resonance", res(0.7f)}},
             {"warm", "keys", "pad"}),
        make("Dark Pad", "Tone", "Pulls a pad back behind everything",
             {{"Type", lowPass}, {"Cutoff", cutoff(1200)}, {"Resonance", res(1.2f)}},
             {"pad", "keys", "chords"}),
        make("Muffled Next Room", "Transitions", "The music from behind a wall",
             {{"Type", lowPass}, {"Cutoff", cutoff(400)}, {"Resonance", res(0.7f)}},
             {"fx", "transition", "intro"}),
        make("Thin Intro", "Transitions", "Only the top end, for intros and breakdowns",
             {{"Type", highPass}, {"Cutoff", cutoff(600)}, {"Resonance", res(0.9f)}},
             {"fx", "transition", "intro"}),
        make("Resonant Sweep Start", "Transitions", "A resonant low-pass ready to automate open",
             {{"Type", lowPass}, {"Cutoff", cutoff(300)}, {"Resonance", res(5.5f)}, {"Drive", drive(3.6f)}},
             {"build", "transition", "synth"}),
        make("Hi-Pass Clean-Up", "Utility", "Takes out low rumble and nothing else",
             {{"Type", highPass}, {"Cutoff", cutoff(120)}, {"Resonance", res(0.7f)}},
             {"utility", "vocal", "guitar"}),
        make("Telephone Band", "FX", "A narrow, driven band like a cheap speaker",
             {{"Type", bandPass}, {"Cutoff", cutoff(1200)}, {"Resonance", res(2.0f)}, {"Drive", drive(5.4f)}},
             {"fx", "vocal", "lofi"}),
        make("Acid Squelch", "Envelope", "Resonant low-pass that opens on every note",
             {{"Type", lowPass}, {"Cutoff", cutoff(600)}, {"Resonance", res(7.4f)}, {"Envelope", bipolarPct(60)},
              {"Attack", att(1)}, {"Release", rel(120)}, {"Drive", drive(9)}},
             {"bass", "synth", "acid"}),
        make("Auto-Wah", "Envelope", "A band that follows how hard you play",
             {{"Type", bandPass}, {"Cutoff", cutoff(500)}, {"Resonance", res(4)}, {"Envelope", bipolarPct(80)},
              {"Attack", att(3)}, {"Release", rel(200)}},
             {"guitar", "funk", "keys"}),
        make("Envelope Duck", "Envelope", "Closes on loud hits and opens in the gaps",
             {{"Type", lowPass}, {"Cutoff", cutoff(5000)}, {"Envelope", bipolarPct(-60)}, {"Attack", att(0.5f)},
              {"Release", rel(200)}},
             {"drums", "fx"}),
    };
}

std::vector<Preset> lfo() {
    const float volume = 0.0f, pan = 0.5f, cutoff = 1.0f;
    auto wave = [](int i) { return step(i, 6); }; // Sine, Triangle, Saw Down, Saw Up, Square, S&H
    auto smooth = [](float ms) { return lin(ms, 0.0f, 200.0f); };
    auto rate = [](float hz) { return logf01(hz, 0.02f, 20.0f); };
    const float sync = 1.0f;
    return {
        make("Quarter Tremolo", "Volume", "A rounded pulse on every beat",
             {{"Target", volume}, {"Wave", wave(1)}, {"Sync", sync}, {"Division", lfoDiv(Div::Quarter)},
              {"Depth", pct(60)}},
             {"keys", "guitar", "pad"}),
        make("Sidechain Pump", "Volume", "The ducking pump on every beat, no routing needed",
             {{"Target", volume}, {"Wave", wave(3)}, {"Sync", sync}, {"Division", lfoDiv(Div::Quarter)},
              {"Depth", pct(70)}, {"Smooth", smooth(10)}},
             {"pump", "pad", "synth", "bass"}),
        make("Eighth Chop", "Volume", "Hard on-off gating in eighth notes",
             {{"Target", volume}, {"Wave", wave(4)}, {"Sync", sync}, {"Division", lfoDiv(Div::Eighth)},
              {"Depth", pct(100)}, {"Smooth", smooth(5)}},
             {"pad", "vocal", "chop"}),
        make("Trance Gate", "Volume", "Sixteenth-note gating",
             {{"Target", volume}, {"Wave", wave(4)}, {"Sync", sync}, {"Division", lfoDiv(Div::Sixteenth)},
              {"Depth", pct(100)}, {"Smooth", smooth(2)}},
             {"pad", "synth", "trance"}),
        make("Gentle Breath", "Volume", "A slow, barely-there swell",
             {{"Target", volume}, {"Wave", wave(0)}, {"Sync", 0.0f}, {"Rate", rate(0.32f)}, {"Depth", pct(20)}},
             {"pad", "ambient"}),
        make("Slow Auto-Pan", "Pan", "Drifts side to side over a bar",
             {{"Target", pan}, {"Wave", wave(0)}, {"Sync", sync}, {"Division", lfoDiv(Div::Whole)},
              {"Depth", pct(60)}},
             {"wide", "pad", "keys"}),
        make("Fast Auto-Pan", "Pan", "Eighth-note panning for movement",
             {{"Target", pan}, {"Wave", wave(1)}, {"Sync", sync}, {"Division", lfoDiv(Div::Eighth)},
              {"Depth", pct(80)}},
             {"hats", "perc", "wide"}),
        make("Filter Wobble", "Filter", "Eighth-note wobble on the cutoff",
             {{"Target", cutoff}, {"Wave", wave(0)}, {"Sync", sync}, {"Division", lfoDiv(Div::Eighth)},
              {"Depth", pct(70)}},
             {"bass", "synth", "wobble"}),
        make("Triplet Wobble", "Filter", "Heavy wobble in eighth-note triplets",
             {{"Target", cutoff}, {"Wave", wave(0)}, {"Sync", sync}, {"Division", lfoDiv(Div::EighthTrip)},
              {"Depth", pct(90)}},
             {"bass", "dubstep", "wobble"}),
        make("Random Steps", "Filter", "A new cutoff every sixteenth",
             {{"Target", cutoff}, {"Wave", wave(5)}, {"Sync", sync}, {"Division", lfoDiv(Div::Sixteenth)},
              {"Depth", pct(50)}, {"Smooth", smooth(20)}},
             {"synth", "fx"}),
    };
}

std::vector<Preset> limiter() {
    auto ceiling = [](float db) { return lin(db, -24.0f, 0.0f); };
    auto rel = [](float ms) { return lin(ms, 10.0f, 500.0f); };
    const float autoRelease = 0.0f, manual = 1.0f;
    return {
        make("Transparent Master", "Master", "Catches peaks at -0.3 dB and stays out of the way",
             {{"Ceiling", ceiling(-0.3f)}, {"Release Mode", autoRelease}}, {"master", "mix"}),
        make("Streaming -1 dB", "Master", "A -1 dB ceiling for streaming services",
             {{"Ceiling", ceiling(-1.0f)}, {"Release Mode", autoRelease}}, {"master", "mix", "streaming"}),
        make("Loud & Punchy", "Master", "Fast release for a louder, punchier master",
             {{"Ceiling", ceiling(-0.5f)}, {"Release Mode", manual}, {"Release", rel(30)}}, {"master", "loud"}),
        make("Smooth Glue", "Master", "Slow release that holds the mix together",
             {{"Ceiling", ceiling(-1.0f)}, {"Release Mode", manual}, {"Release", rel(300)}}, {"master", "mix"}),
        make("Safety Guard", "Utility", "Just below full scale, only touches overs",
             {{"Ceiling", ceiling(-0.1f)}, {"Release Mode", manual}, {"Release", rel(50)}}, {"utility", "bus"}),
        make("Drum Bus Peaks", "Buses", "Shaves drum transients to make room",
             {{"Ceiling", ceiling(-3.0f)}, {"Release Mode", manual}, {"Release", rel(20)}}, {"drums", "bus"}),
        make("Vocal Peak Catcher", "Buses", "Stops the odd loud word jumping out",
             {{"Ceiling", ceiling(-2.0f)}, {"Release Mode", manual}, {"Release", rel(80)}}, {"vocal", "vox"}),
        make("Headroom -6 dB", "Utility", "Keeps a bus 6 dB under full scale",
             {{"Ceiling", ceiling(-6.0f)}, {"Release Mode", autoRelease}}, {"utility", "bus"}),
    };
}

std::vector<Preset> ott() {
    auto time = [](float x) { return 0.5f + std::log10(x) / 2.0f; };
    auto io = [](float db) { return bipolarDb(db, 24.0f); };
    auto band = [](float db) { return bipolarDb(db, 12.0f); };
    return {
        make("Classic Full", "Character", "Full-depth upward and downward squash",
             {{"Depth", pct(100)}, {"Output Gain", io(-4.8f)}}, {"synth", "fx"}),
        make("Subtle Glue", "Mix", "A quarter of the effect: density without the squash",
             {{"Depth", pct(25)}, {"Time", time(1)}}, {"mix", "bus"}),
        make("Master Lift", "Mix", "Barely there: lifts the quiet details of a mix",
             {{"Depth", pct(15)}}, {"master", "mix"}),
        make("Vocal Presence", "Vocals", "Brings up detail and breath in a vocal",
             {{"Depth", pct(35)}, {"High Gain", band(2)}, {"Low Gain", band(-2)}}, {"vocal", "vox"}),
        make("Drum Smash", "Drums", "Fast, heavy multiband smash for drum buses",
             {{"Depth", pct(70)}, {"Time", time(0.4f)}, {"Output Gain", io(-4.8f)}}, {"drums", "bus"}),
        make("Hi-Hat Shine", "Drums", "Crisp, even hats with the low end pulled out",
             {{"Depth", pct(50)}, {"High Gain", band(2.4f)}, {"Low Gain", band(-7.2f)}}, {"hats", "perc"}),
        make("808 Tighten", "Bass", "Evens out an 808's tail and adds bite",
             {{"Depth", pct(40)}, {"Low Gain", band(2.4f)}, {"High Gain", band(-2.4f)}}, {"808", "bass"}),
        make("Bass Body", "Bass", "Fuller low end with the fizz tucked away",
             {{"Depth", pct(45)}, {"Low Gain", band(4.8f)}, {"High Gain", band(-4.8f)}}, {"bass"}),
        make("Synth Sparkle", "Character", "Brighter, denser synths",
             {{"Depth", pct(60)}, {"High Gain", band(4.8f)}, {"Output Gain", io(-2.4f)}}, {"synth", "lead"}),
        make("Pad Air", "Character", "Slow, airy compression for pads",
             {{"Depth", pct(50)}, {"Time", time(2.5f)}, {"High Gain", band(2.4f)}}, {"pad", "keys"}),
    };
}

std::vector<Preset> sat() {
    auto drive = [](float db) { return lin(db, 0.0f, 36.0f); };
    auto tone = [](float hz) { return logf01(hz, 1000.0f, 20000.0f); };
    auto out = [](float db) { return bipolarDb(db, 12.0f); };
    const float tape = 0.0f, tube = 0.5f, hard = 1.0f;
    return {
        make("Tape Warmth", "Warmth", "Gentle tape saturation that rounds the edges",
             {{"Mode", tape}, {"Drive", drive(6)}, {"Tone", tone(12000)}, {"Output", out(-1.2f)}}, {"warm", "tape"}),
        make("Tube Glow", "Warmth", "Rich tube harmonics without obvious distortion",
             {{"Mode", tube}, {"Drive", drive(9)}, {"Tone", tone(10000)}, {"Output", out(-1.8f)}}, {"warm", "keys"}),
        make("Master Glue", "Warmth", "A touch of tape on the whole mix",
             {{"Mode", tape}, {"Drive", drive(3.6f)}, {"Tone", tone(20000)}, {"Output", out(-0.5f)}},
             {"master", "mix"}),
        make("Keys Saturate", "Warmth", "Thickens keys and chords",
             {{"Mode", tube}, {"Drive", drive(5.4f)}, {"Tone", tone(9000)}}, {"keys", "piano", "chords"}),
        make("Vocal Edge", "Vocals", "Blended tube grit that helps a vocal cut",
             {{"Mode", tube}, {"Drive", drive(7.2f)}, {"Tone", tone(14800)}, {"Mix", pct(60)}}, {"vocal", "vox", "rap"}),
        make("808 Grit", "Bass", "Hard clipping blended in so the 808 reads on small speakers",
             {{"Mode", hard}, {"Drive", drive(14.4f)}, {"Tone", tone(6000)}, {"Mix", pct(50)}, {"Output", out(-2.4f)}},
             {"808", "bass"}),
        make("Bass Weight", "Bass", "Tube harmonics that make a bass feel bigger",
             {{"Mode", tube}, {"Drive", drive(12)}, {"Tone", tone(4500)}, {"Mix", pct(60)}}, {"bass"}),
        make("Drum Crunch", "Drums", "Tape crunch for drums and loops",
             {{"Mode", tape}, {"Drive", drive(18)}, {"Tone", tone(8000)}, {"Mix", pct(70)}, {"Output", out(-4.8f)}},
             {"drums", "loop"}),
        make("Parallel Fuzz", "Character", "Heavy fuzz mixed low underneath",
             {{"Mode", hard}, {"Drive", drive(32)}, {"Tone", tone(6000)}, {"Mix", pct(25)}, {"Output", out(-4.8f)}},
             {"fx", "guitar", "synth"}),
        make("Lo-Fi Crush", "Character", "Dark, crushed and fully wet",
             {{"Mode", hard}, {"Drive", drive(25)}, {"Tone", tone(3300)}, {"Output", out(-6)}}, {"lofi", "fx"}),
    };
}

std::vector<Preset> transient() {
    auto amt = [](float percent) { return bipolarPct(percent); };
    return {
        make("Snappy Drums", "Drums", "More attack on every hit",
             {{"Attack", amt(60)}, {"Sustain", amt(0)}}, {"drums", "loop"}),
        make("Punchy Kick", "Drums", "Sharper front, shorter tail",
             {{"Attack", amt(40)}, {"Sustain", amt(-20)}}, {"kick", "drums"}),
        make("Snare Crack", "Drums", "Crack forward, ring back",
             {{"Attack", amt(80)}, {"Sustain", amt(-20)}}, {"snare", "clap", "drums"}),
        make("Tight Room", "Drums", "Cuts the room ring out of drums",
             {{"Sustain", amt(-60)}}, {"drums", "room"}),
        make("Hats Tame", "Drums", "Softens spiky hats",
             {{"Attack", amt(-30)}}, {"hats", "hat", "perc"}),
        make("808 Click", "Bass", "More click at the start and a longer body",
             {{"Attack", amt(50)}, {"Sustain", amt(20)}}, {"808", "bass"}),
        make("Guitar Pick", "Instruments", "Brings the pick attack forward, blended",
             {{"Attack", amt(40)}, {"Mix", pct(70)}}, {"guitar", "pluck"}),
        make("Soft Attack", "Instruments", "Rounds off sharp starts",
             {{"Attack", amt(-40)}}, {"keys", "pluck", "vocal"}),
        make("Long Tail", "Space", "Brings up the decay and room",
             {{"Sustain", amt(60)}}, {"ambient", "pad", "drums"}),
        make("Ambient Swell", "Space", "Removes the attack and lets the tail bloom",
             {{"Attack", amt(-80)}, {"Sustain", amt(40)}}, {"ambient", "pad", "fx"}),
    };
}

std::vector<Preset> verb() {
    // Ten of the Verb editor's own voiced presets (its picture grid), with the same values: one list for both.
    auto row = [](const char* name, const char* category, const char* description, int mode, float size, float decay,
                  float damping, float diffusion, float modRate, float modDepth, float width, float mix, Tags tags) {
        return make(name, category, description,
                    {{"Mode", step(mode, 9)}, {"Size", size}, {"Decay", decay}, {"Damping", damping},
                     {"Diffusion", diffusion}, {"Mod Rate", modRate}, {"Mod Depth", modDepth}, {"Width", width},
                     {"Mix", mix}, {"Attack", 0.0f}, {"Shape", 0.5f}, {"Pre Sync", 0.0f}, {"Mod Char", 0.0f}},
                    tags);
    };
    return {
        row("Vocal Booth", "Rooms", "Tight, controlled space for dry vocals", 0, 0.22f, 0.18f, 0.62f, 0.50f, 0.18f,
            0.03f, 0.44f, 0.24f, {"vocal", "vox", "rap"}),
        row("Studio Room", "Rooms", "Natural small room with early reflections", 0, 0.30f, 0.32f, 0.58f, 0.55f, 0.22f,
            0.04f, 0.50f, 0.28f, {"vocal", "guitar", "keys"}),
        row("Dense Studio", "Rooms", "Thick small room, great for drums", 0, 0.38f, 0.42f, 0.70f, 0.60f, 0.28f, 0.06f,
            0.56f, 0.32f, {"drums", "snare", "clap"}),
        row("Tight Chamber", "Chambers", "Small, tight chamber for subtle space", 4, 0.40f, 0.32f, 0.56f, 0.72f, 0.30f,
            0.07f, 0.62f, 0.28f, {"vocal", "keys", "perc"}),
        row("Classic Plate", "Plates", "Smooth plate, the vocal standard", 2, 0.50f, 0.44f, 0.44f, 0.78f, 0.32f,
            0.09f, 0.74f, 0.30f, {"vocal", "vox", "snare"}),
        row("Dark Plate", "Plates", "Dark, moody plate with a long decay", 2, 0.62f, 0.56f, 0.48f, 0.80f, 0.30f,
            0.08f, 0.76f, 0.30f, {"vocal", "keys", "pad"}),
        row("Medium Hall", "Halls", "Well-balanced hall for most sources", 1, 0.58f, 0.46f, 0.52f, 0.78f, 0.36f,
            0.07f, 0.66f, 0.30f, {"keys", "strings", "vocal"}),
        row("Grand Hall", "Halls", "Massive hall with a long, lush tail", 1, 0.76f, 0.58f, 0.51f, 0.86f, 0.42f, 0.09f,
            0.74f, 0.36f, {"strings", "pad", "keys"}),
        row("Soft Air", "Ambience", "Gentle ambient wash for padding and atmosphere", 6, 0.18f, 0.12f, 0.38f, 0.40f,
            0.14f, 0.02f, 0.36f, 0.16f, {"ambient", "pad", "vocal"}),
        row("Cinematic Space", "Ambience", "Vast space for film-style textures", 7, 0.82f, 0.66f, 0.44f, 0.90f, 0.32f,
            0.11f, 0.84f, 0.38f, {"pad", "ambient", "fx"}),
    };
}

void put(std::map<std::string, std::vector<Preset>>& table, const char* pluginId, std::vector<Preset> presets) {
    for (auto& p : presets) p.pluginId = pluginId;
    table[pluginId] = std::move(presets);
}

} // namespace

void addBuiltInFactoryPresets(std::map<std::string, std::vector<Preset>>& table) {
    put(table, "com.Aestrastudios.comp", comp());
    put(table, "com.Aestrastudios.delay", delay());
    put(table, "com.Aestrastudios.drift", drift());
    put(table, "com.Aestrastudios.eq", eq());
    put(table, "com.Aestrastudios.filter", filter());
    put(table, "com.Aestrastudios.lfo", lfo());
    put(table, "com.Aestrastudios.limiter", limiter());
    put(table, "com.Aestrastudios.ott", ott());
    put(table, "com.Aestrastudios.sat", sat());
    put(table, "com.Aestrastudios.transient", transient());
    put(table, "com.Aestrastudios.verb", verb());
}

} // namespace Presets
} // namespace Audio
} // namespace Aestra
