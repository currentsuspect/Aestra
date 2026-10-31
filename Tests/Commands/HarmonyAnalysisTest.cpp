// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// HarmonyAnalysisTest — V83-HS1 (FD-63): the harmony service detects the key and the chords of MIDI.
//   1. known musical answers: a triad, a minor progression, an inversion, a seventh chord;
//   2. smoothing: a passing tone and a stray note do not make a chord, real half-bar changes do;
//   3. user overrides win and are marked; chordAt finds the chord at a beat and answers N.C. outside;
//   4. the Muse verb get_harmony returns the same chords for a session built through Muse.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseGrammar.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Harmony/HarmonyAnalysis.h"
#include "Harmony/HarmonyGather.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Aestra::JSON;
using Aestra::Audio::AudioEngine;
using Aestra::Audio::CommandRegistry;
using Aestra::Audio::MuseService;
using Aestra::Audio::TrackManager;
using namespace Aestra::Audio::Harmony;

namespace {
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << what << "\n";
    if (!cond)
        ++g_failures;
}

using Notes = std::vector<HarmonyNote>;
void add(Notes& n, double start, double dur, std::initializer_list<int> pitches, float vel = 0.8f) {
    for (int p : pitches)
        n.push_back({start, dur, p, vel});
}
std::string names(const HarmonyTimeline& t) {
    std::string out;
    for (const auto& c : t.chords)
        out += (out.empty() ? "" : " ") + c.name;
    return out;
}
std::string romans(const HarmonyTimeline& t) {
    std::string out;
    for (const auto& c : t.chords)
        out += (out.empty() ? "" : " ") + c.roman;
    return out;
}
// Dm - Bb - F - C, one bar each, a bass note under a triad.
Notes progression() {
    Notes n;
    add(n, 0, 4, {50, 62, 65, 69});
    add(n, 4, 4, {46, 58, 62, 65});
    add(n, 8, 4, {53, 65, 69, 72});
    add(n, 12, 4, {48, 60, 64, 67});
    return n;
}

int g_id = 1;
JSON call(MuseService& s, const std::string& verb, const std::string& args) {
    const std::string req =
        "{\"id\": " + std::to_string(g_id++) + ", \"verb\": \"" + verb + "\", \"args\": {" + args + "}}";
    return JSON::parse(s.handleRequest(req));
}
std::string status(JSON& r) { return r.has("status") ? r["status"].asString() : "<missing>"; }
std::string verbNames(JSON& r) {
    std::string out;
    auto& chords = r["result"]["chords"];
    for (size_t i = 0; i < chords.size(); ++i)
        out += (out.empty() ? "" : " ") + chords[i]["name"].asString();
    return out;
}
} // namespace

int main() {
    std::cout << "[1] known answers\n";
    {
        Notes n;
        add(n, 0, 4, {60, 64, 67});
        HarmonyTimeline t = analyzeHarmony(n, 4.0);
        check(names(t) == "C", "a C major triad held a bar is C (" + names(t) + ")");
        check(t.key.valid() && t.key.tonic == 0 && t.key.mode == KeyMode::Major, "key C major (" + t.key.name() + ")");
        check(t.key.confidence > 0.0 && t.key.confidence <= 1.0, "key confidence is in (0, 1]");
        check(t.chords.size() == 1 && t.chords[0].startBeat == 0.0 && t.chords[0].endBeat == 4.0 &&
                  t.chords[0].roman == "I" && !t.chords[0].userSet,
              "span 0..4, roman I, not user set");
    }
    {
        HarmonyTimeline t = analyzeHarmony(progression(), 4.0);
        check(names(t) == "Dm Bb F C", "Dm-Bb-F-C comes back in order (" + names(t) + ")");
        check(t.key.tonic == 2 && t.key.mode == KeyMode::Minor, "key D minor (" + t.key.name() + ")");
        check(romans(t) == "i VI III VII", "romans i VI III VII (" + romans(t) + ")");
    }
    {
        Notes n;
        add(n, 0, 4, {52, 60, 64, 67});
        HarmonyTimeline t = analyzeHarmony(n, 4.0);
        check(names(t) == "C/E", "C major with E in the bass is C/E (" + names(t) + ")");
        check(t.chords.size() == 1 && t.chords[0].root == 0 && t.chords[0].bass == 4, "root C, bass E");
    }
    {
        Notes n;
        add(n, 0, 4, {55, 59, 62, 65});
        HarmonyTimeline t = analyzeHarmony(n, 4.0);
        check(names(t) == "G7", "G B D F is G7 (" + names(t) + ")");
    }
    {
        Notes n;
        add(n, 0, 4, {45, 57, 60, 64, 67}); // A C E G over A
        HarmonyTimeline t = analyzeHarmony(n, 4.0);
        check(names(t) == "Am7", "A C E G over A is Am7 (" + names(t) + ")");
        Notes six;
        add(six, 0, 4, {48, 60, 64, 67, 69}); // C E G A over C
        t = analyzeHarmony(six, 4.0);
        check(names(t) == "C6", "C E G A over C is C6, the bass decides against Am7 (" + names(t) + ")");
    }

    std::cout << "[2] smoothing\n";
    {
        Notes n;
        add(n, 0, 4, {48, 60, 64, 67});
        add(n, 1.5, 0.5, {74}, 0.7f); // a passing D on a weak eighth
        HarmonyTimeline t = analyzeHarmony(n, 4.0);
        check(names(t) == "C", "a passing tone inside a bar of C does not make a chord (" + names(t) + ")");
    }
    {
        Notes n;
        add(n, 0, 2, {48, 60, 64, 67});
        add(n, 2, 2, {43, 59, 62, 67});
        add(n, 4, 2, {48, 60, 64, 67});
        add(n, 6, 2, {43, 59, 62, 67});
        HarmonyTimeline t = analyzeHarmony(n, 4.0);
        check(names(t) == "C G C G", "alternating two chords every half bar is reported (" + names(t) + ")");
    }
    {
        Notes n;
        add(n, 0, 4, {48, 60, 64, 67});
        add(n, 4, 4, {53, 65, 69, 72});
        add(n, 2, 1, {66}, 0.6f); // one stray F#, a quarter note, in bar 1
        HarmonyTimeline t = analyzeHarmony(n, 4.0);
        check(names(t) == "C F", "a single stray note does not flicker the chord (" + names(t) + ")");
    }
    {
        Notes n;
        add(n, 0, 4, {48, 60, 64, 67});
        add(n, 8, 4, {48, 60, 64, 67}); // a bar of silence between
        HarmonyTimeline t = analyzeHarmony(n, 4.0);
        check(t.chords.size() == 1 && t.chords[0].endBeat == 12.0, "silence continues the previous chord");
        check(analyzeHarmony({}, 4.0).chords.empty(), "no notes -> an empty timeline");
        check(!analyzeHarmony({}, 4.0).key.valid(), "and no key");
    }

    std::cout << "[3] overrides and chordAt\n";
    {
        HarmonyTimeline t = analyzeHarmony(progression(), 4.0, {{4.0, 8.0, "Gm7"}});
        check(names(t) == "Dm Gm7 F C", "an override replaces detection over its span (" + names(t) + ")");
        check(t.chords.size() == 4 && t.chords[1].userSet && !t.chords[0].userSet && !t.chords[2].userSet,
              "only the overridden span is userSet");
        check(t.chords[1].startBeat == 4.0 && t.chords[1].endBeat == 8.0, "and it covers exactly 4..8");
    }
    {
        HarmonyTimeline t = analyzeHarmony(progression(), 4.0, {{5.0, 7.0, "Am"}});
        check(names(t) == "Dm Bb Am Bb F C" && t.chords[2].userSet && t.chords[2].startBeat == 5.0 &&
                  t.chords[3].startBeat == 7.0,
              "an override inside a span splits it (" + names(t) + ")");
    }
    {
        HarmonyTimeline t = analyzeHarmony(progression(), 4.0, {{4.0, 8.0, "F/A"}});
        check(t.chords[1].name == "F/A" && t.chords[1].root == 5 && t.chords[1].bass == 9, "an override can be a slash chord");
    }
    {
        HarmonyTimeline t = analyzeHarmony(progression(), 4.0);
        check(t.chordAt(0.0).name == "Dm", "chordAt(0) is Dm");
        check(t.chordAt(3.999).name == "Dm" && t.chordAt(4.0).name == "Bb", "chordAt is right either side of the bar line");
        check(t.chordAt(15.999).name == "C" && t.chordAt(16.0).isNoChord(), "and N.C. at the end of the last span");
        check(t.chordAt(-1.0).isNoChord() && t.chordAt(-1.0).name == "N.C.", "N.C. before the first chord");
        Notes late;
        add(late, 8, 4, {48, 60, 64, 67});
        HarmonyTimeline l = analyzeHarmony(late, 4.0);
        check(l.chordAt(0.0).isNoChord() && l.chordAt(8.0).name == "C", "N.C. until music starts at bar 3");
    }
    {
        int root = -1, bass = -1;
        ChordQuality q = ChordQuality::Major;
        check(parseChordName("Bbmaj7", root, q, bass) && root == 10 && q == ChordQuality::Major7 && bass == -1,
              "parseChordName reads Bbmaj7");
        check(parseChordName("F#m7b5/C", root, q, bass) && root == 6 && q == ChordQuality::HalfDiminished7 && bass == 0,
              "and F#m7b5/C");
        check(!parseChordName("Hello", root, q, bass), "and refuses words");
    }

    std::cout << "[4] the Muse verb\n";
    {
        auto tracks = std::make_shared<TrackManager>();
        tracks->getUnitManager().setPatternManager(&tracks->getPatternManager());
        AudioEngine engine;
        engine.setSampleRate(48000);
        MuseService::wireHeadlessEngine(tracks, engine);
        CommandRegistry::initialize();
        MuseService s(tracks.get(), &engine);

        call(s, "add_unit", "\"name\": \"Keys\"");
        JSON units = call(s, "list_units", "");
        const std::string keys = std::to_string(static_cast<long long>(units["result"]["units"][0]["id"].asNumber()));
        const std::string p =
            std::to_string(static_cast<long long>(units["result"]["units"][0]["defaultPatternId"].asNumber()));

        JSON r = call(s, "get_harmony", "");
        check(status(r) == "ok" && r["result"]["chords"].size() == 0, "an empty session has no chords");

        // Dm - Bb - F - C, written into the pattern
        r = call(s, "add_notes", "\"pattern\": " + p + ", \"unit\": " + keys +
                                     ", \"notes\": \"0,50,4; 0,62,4; 0,65,4; 0,69,4;"
                                     " 4,46,4; 4,58,4; 4,62,4; 4,65,4;"
                                     " 8,53,4; 8,65,4; 8,69,4; 8,72,4;"
                                     " 12,48,4; 12,60,4; 12,64,4; 12,67,4\"");
        check(status(r) == "ok", "add_notes ok");

        r = call(s, "get_harmony", "\"pattern\": " + p);
        check(status(r) == "ok" && verbNames(r) == "Dm Bb F C", "a pattern alone: Dm Bb F C (" + verbNames(r) + ")");
        check(status(r) == "ok" && r["result"]["key"]["name"].asString() == "D minor", "and the key is D minor");
        check(status(r) == "ok" && r["result"]["chords"][1]["roman"].asString() == "VI" &&
                  r["result"]["chords"][1]["startBeat"].asNumber() == 4.0 &&
                  r["result"]["chords"][1]["endBeat"].asNumber() == 8.0,
              "span fields: roman VI, 4..8");

        r = call(s, "get_harmony", "");
        check(status(r) == "ok" && r["result"]["chords"].size() == 0, "an unarranged pattern is not in the arrangement");

        call(s, "arrange_pattern", "\"pattern\": " + p + ", \"track\": 0, \"start\": 8");
        r = call(s, "get_harmony", "");
        check(status(r) == "ok" && verbNames(r) == "Dm Bb F C", "arranged at beat 8 the chords are the same (" + verbNames(r) + ")");
        check(status(r) == "ok" && r["result"]["chords"][0]["startBeat"].asNumber() == 8.0,
              "and sit at the arranged position (beat 8)");

        r = call(s, "get_harmony", "\"from\": 12, \"to\": 20");
        check(status(r) == "ok" && verbNames(r) == "Bb F", "from/to limits it to that range (" + verbNames(r) + ")");

        r = call(s, "get_harmony", "\"pattern\": 999999");
        check(status(r) == "execution_error", "an unknown pattern is an execution_error");
        r = call(s, "get_harmony", "\"bogus\": 1");
        check(status(r) == "validation_error", "an unknown arg is a validation_error");

        JSON schema = JSON::parse(Aestra::Audio::MuseGrammar::schemaToJsonString());
        bool declared = false;
        auto& queries = schema["queries"];
        for (size_t i = 0; i < queries.size(); ++i)
            declared = declared || queries[i]["verb"].asString() == "get_harmony";
        check(declared, "get_harmony is in the schema");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
