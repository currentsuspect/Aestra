// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// GlobalTransposePlaybackTest — V83-KEY2 (FD-76): the project key and the global transpose.
//   1. the model: a key change takes the shortest route and never drifts from the written notes;
//   2. playback: key-following units sound transposed, drum units and switched-off units sound as written,
//      and the notes in the pattern are not edited;
//   3. a transpose change while a note sounds releases it, does not re-fire it, and leaves nothing hanging;
//   4. the key, the transpose and the switch survive save and load; a damaged section loads as nothing.

#include "Models/PatternManager.h"
#include "Models/PatternSource.h"
#include "Models/UnitManager.h"
#include "Music/ProjectHarmony.h"
#include "Playback/PatternPlaybackEngine.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

constexpr int kSampleRate = 48000;
constexpr uint64_t kBeat = 24000; // 120 BPM
constexpr uint64_t kLoopLength = kBeat * 4;
constexpr int kLookahead = static_cast<int>(kBeat * 2);
constexpr int kBlockSize = 512;

int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << what << "\n";
    if (!cond)
        ++g_failures;
}

struct Event {
    uint64_t frame;
    bool on;
    uint8_t pitch;
    bool lead; // which unit's buffer it arrived in
};

// A lead (an instrument unit: follows the key by default) and a kick (a drum sampler: does not).
// Lead: 60 from beat 0 for 1.5 beats, 64 at beat 2 for one beat. Kick: 36 on every beat.
class Harness {
public:
    Harness() : m_engine(&m_clock, &m_patternMgr, &m_unitMgr) {
        m_lead = m_unitMgr.createUnit("lead", UnitType::Instrument);
        m_kick = m_unitMgr.createUnit("kick", UnitType::Sampler);
        MidiPayload payload;
        const auto add = [&payload](UnitID unit, int pitch, double beat, double duration) {
            MidiNote note;
            note.pitch = pitch;
            note.startBeat = beat;
            note.durationBeats = duration;
            note.velocity = 100.0f / 127.0f;
            note.unitId = unit;
            payload.notes.push_back(note);
        };
        add(m_lead, 60, 0.0, 1.5);
        add(m_lead, 64, 2.0, 1.0);
        for (double beat : {0.0, 1.0, 2.0, 3.0})
            add(m_kick, 36, beat, 0.25);
        m_patternId = m_patternMgr.createMidiPattern("t", 4.0, payload);
        m_engine.schedulePatternInstance(m_patternId, 0.0, 1);
    }

    std::vector<Event> run(uint64_t from, uint64_t to) {
        std::vector<Event> out;
        MidiBuffer leadBuffer, kickBuffer;
        std::vector<PatternPlaybackEngine::UnitMidiRoute> routes{{m_lead, &leadBuffer}, {m_kick, &kickBuffer}};
        for (uint64_t f = from; f < to; f += kBlockSize) {
            const int size = static_cast<int>(std::min<uint64_t>(kBlockSize, to - f));
            m_engine.refillWindow(f, kSampleRate, kLookahead, kLoopLength);
            leadBuffer.clear();
            kickBuffer.clear();
            m_engine.processAudio(f, size, routes.data(), routes.size());
            for (MidiBuffer* buffer : {&leadBuffer, &kickBuffer}) {
                for (size_t i = 0; i < buffer->getEventCount(); ++i) {
                    const auto& ev = buffer->getEvent(i);
                    const uint8_t status = static_cast<uint8_t>(ev.data[0] & 0xF0);
                    const bool on = status == 0x90 && ev.data[2] != 0;
                    const bool off = status == 0x80 || (status == 0x90 && ev.data[2] == 0);
                    if (on || off)
                        out.push_back({f + ev.sampleOffset, on, ev.data[1], buffer == &leadBuffer});
                }
            }
        }
        return out;
    }

    UnitManager& units() { return m_unitMgr; }
    UnitID lead() const { return m_lead; }
    UnitID kick() const { return m_kick; }
    int writtenLeadPitch() {
        const auto* pattern = m_patternMgr.getPattern(m_patternId);
        return std::get<MidiPayload>(pattern->payload).notes.front().pitch;
    }

private:
    TimelineClock m_clock{120.0};
    PatternManager m_patternMgr;
    UnitManager m_unitMgr;
    PatternPlaybackEngine m_engine;
    UnitID m_lead{};
    UnitID m_kick{};
    PatternID m_patternId{};
};

std::string onsOf(const std::vector<Event>& events, bool lead) {
    std::string out;
    for (const Event& e : events)
        if (e.on && e.lead == lead)
            out += (out.empty() ? "" : " ") + std::to_string(e.pitch);
    return out;
}
int count(const std::vector<Event>& events, bool on, uint8_t pitch, bool lead) {
    return static_cast<int>(std::count_if(events.begin(), events.end(), [&](const Event& e) {
        return e.on == on && e.pitch == pitch && e.lead == lead;
    }));
}
ProjectHarmony transposeOf(int semitones) {
    ProjectHarmony h;
    h.transposeSemitones = semitones;
    return h;
}

} // namespace

int main() {
    std::cout << "[1] the model\n";
    {
        check(shortestKeyRoute(0, 9) == -3, "C to A is three down, not nine up");
        check(shortestKeyRoute(0, 7) == -5 && shortestKeyRoute(0, 5) == 5, "C to G is five down, C to F five up");
        check(shortestKeyRoute(0, 6) == 6 && shortestKeyRoute(6, 0) == 6, "the tritone goes up");
        check(shortestKeyRoute(4, 4) == 0, "the same key is no move");

        ProjectHarmony h;
        h = withSoundingKey(h, 0, false);
        check(h.writtenTonic == 0 && h.transposeSemitones == 0 && h.soundingTonic() == 0,
              "the first key is a label: nothing moves");
        h = withSoundingKey(h, 9, true);
        check(h.transposeSemitones == -3 && h.soundingTonic() == 9 && h.minor && h.writtenTonic == 0,
              "C to A minor: transpose -3, written key still C");
        bool near = true;
        for (int tonic : {2, 7, 11, 4, 6, 1, 8, 3, 10, 5, 0, 9}) {
            h = withSoundingKey(h, tonic, false);
            near = near && h.soundingTonic() == tonic && h.transposeSemitones >= -5 && h.transposeSemitones <= 6;
        }
        check(near, "twelve key changes in a row never leave the written notes by more than a tritone");

        ProjectHarmony labelled = withSoundingKey(transposeOf(2), 4, false);
        check(labelled.writtenTonic == 2 && labelled.soundingTonic() == 4 && labelled.transposeSemitones == 2,
              "naming the key of an already transposed project keeps the transpose");

        check(transposedMidiNote(60, 3) == 63 && transposedMidiNote(60, -3) == 57, "a note moves by the transpose");
        check(transposedMidiNote(126, 5) == 119 && transposedMidiNote(2, -5) == 9,
              "a note pushed off the keyboard folds back an octave, same pitch class");
        check(clampTranspose(30) == 18 && clampTranspose(-30) == -18 && clampTranspose(24) == 24,
              "a transpose out of range drops whole octaves");

        check(noteInOtherMode(64, 0, true) == 63 && noteInOtherMode(69, 0, true) == 68 &&
                  noteInOtherMode(71, 0, true) == 70,
              "C major to C minor lowers the third, sixth and seventh");
        check(noteInOtherMode(60, 0, true) == 60 && noteInOtherMode(67, 0, true) == 67 &&
                  noteInOtherMode(62, 0, true) == 62,
              "and leaves the tonic, second and fifth");
        check(noteInOtherMode(noteInOtherMode(64, 0, true), 0, false) == 64, "minor and back is the note it was");
    }

    std::cout << "[2] playback\n";
    {
        Harness h;
        const auto events = h.run(0, kBeat * 4 - kBlockSize);
        check(onsOf(events, true) == "60 64" && onsOf(events, false) == "36 36 36 36",
              "no transpose: everything as written (" + onsOf(events, true) + " | " + onsOf(events, false) + ")");
    }
    {
        Harness h;
        h.units().setHarmony(transposeOf(3));
        const auto events = h.run(0, kBeat * 4 - kBlockSize);
        check(onsOf(events, true) == "63 67", "transpose +3: the lead sounds three up (" + onsOf(events, true) + ")");
        check(onsOf(events, false) == "36 36 36 36", "the kick does not follow by default");
        check(h.writtenLeadPitch() == 60, "the note in the pattern is still 60");
        check(count(events, false, 63, true) == 1 && count(events, false, 67, true) == 1 &&
                  count(events, false, 60, true) == 0,
              "each transposed note is released at its transposed pitch");
    }
    {
        Harness h;
        h.units().setHarmony(transposeOf(-5));
        h.units().setUnitFollowsKey(h.lead(), false);
        h.units().setUnitFollowsKey(h.kick(), true);
        const auto events = h.run(0, kBeat * 4 - kBlockSize);
        check(onsOf(events, true) == "60 64", "a lead switched off stays as written (" + onsOf(events, true) + ")");
        check(onsOf(events, false) == "31 31 31 31", "a kick switched on follows (" + onsOf(events, false) + ")");
        h.units().setUnitFollowsKey(h.lead(), std::nullopt);
        check(h.units().getUnit(h.lead())->followsKey(), "clearing the switch returns the type's default");
    }

    std::cout << "[3] a change while a note sounds\n";
    {
        Harness h;
        auto events = h.run(0, kBeat / 2); // the lead's 60 is sounding (it lasts 1.5 beats)
        check(count(events, true, 60, true) == 1 && count(events, false, 60, true) == 0, "60 is sounding");
        h.units().setHarmony(transposeOf(2));
        const auto after = h.run(kBeat / 2, kBeat * 4 - kBlockSize);
        const auto firstOff =
            std::find_if(after.begin(), after.end(), [](const Event& e) { return !e.on && e.lead && e.pitch == 60; });
        check(firstOff != after.end() && firstOff->frame < kBeat / 2 + 4 * kBlockSize,
              "the sounding 60 is released straight away, not at the end of its note");
        check(count(after, true, 62, true) == 0, "it is not struck again at the new pitch");
        check(count(after, true, 66, true) == 1 && count(after, false, 66, true) == 1,
              "the next note plays, and ends, at the new pitch");
        events.insert(events.end(), after.begin(), after.end());
        bool balanced = true;
        for (int pitch = 0; pitch < 128; ++pitch)
            balanced = balanced && count(events, true, static_cast<uint8_t>(pitch), true) <=
                                       count(events, false, static_cast<uint8_t>(pitch), true);
        check(balanced, "no lead note is left without a release");
        check(onsOf(events, false) == "36 36 36 36", "the kick never moved or doubled");
    }

    std::cout << "[4] save and load\n";
    {
        UnitManager fresh;
        fresh.createUnit("lead", UnitType::Instrument);
        const JSON plain = fresh.saveToJSON();
        check(!plain.has("harmony") && !plain["units"][0].has("followsKey"),
              "a project that never set a key writes neither section");

        UnitManager source;
        const UnitID lead = source.createUnit("lead", UnitType::Instrument);
        const UnitID kick = source.createUnit("kick", UnitType::Sampler);
        source.setHarmony(withSoundingKey(withSoundingKey(ProjectHarmony{}, 2, true), 9, true)); // D minor -> A minor
        source.setUnitFollowsKey(lead, false);
        const JSON saved = source.saveToJSON();

        UnitManager loaded;
        loaded.loadFromJSON(JSON::parse(saved.toString()));
        check(loaded.harmony() == source.harmony() && loaded.harmony().soundingTonic() == 9 && loaded.harmony().minor &&
                  loaded.harmony().transposeSemitones == -5,
              "the key and the transpose come back (D minor written, A minor heard)");
        check(loaded.getUnit(lead) && !loaded.getUnit(lead)->followsKey() &&
                  loaded.getUnit(lead)->followsKeyOverride.has_value(),
              "the lead's switch comes back off");
        check(loaded.getUnit(kick) && !loaded.getUnit(kick)->followsKeyOverride.has_value(),
              "a unit that was never switched comes back on its default");
        const auto playback = loaded.playbackHarmony();
        check(playback && playback->transposeSemitones == -5 && playback->followingUnits.empty(),
              "playback gets the loaded transpose and followers");

        JSON damaged = JSON::parse(saved.toString());
        JSON bad = JSON::object();
        bad.set("writtenTonic", JSON(40.0));
        bad.set("transpose", JSON(999.0));
        bad.set("minor", JSON(std::string("yes")));
        damaged.set("harmony", bad);
        UnitManager fromDamaged;
        fromDamaged.loadFromJSON(damaged);
        check(fromDamaged.harmony() == ProjectHarmony{}, "a damaged section loads as no key and no transpose");

        source.clear();
        check(source.harmony() == ProjectHarmony{}, "a cleared project has no key");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}
