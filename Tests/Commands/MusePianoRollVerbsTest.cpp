// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MusePianoRollVerbsTest — V82-M3 (FD-53): the piano roll through Muse, each verb one undo step.
//   1. add_notes writes many notes at once and one undo removes them all; a bad entry rejects the whole batch;
//   2. set_note_length changes one note's length;
//   3. delete_notes removes the notes that start in a range (optionally one unit, or a pitch span);
//   4. duplicate_notes copies a range into the next block of the same length, as the piano-roll ruler does (FD-52),
//      and grows the pattern by whole bars when the copy runs past its end.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"

#include <algorithm>
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

namespace {
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << what << "\n";
    if (!cond)
        ++g_failures;
}

int g_id = 1;
JSON call(MuseService& s, const std::string& verb, const std::string& args) {
    const std::string req =
        "{\"id\": " + std::to_string(g_id++) + ", \"verb\": \"" + verb + "\", \"args\": {" + args + "}}";
    return JSON::parse(s.handleRequest(req));
}
std::string status(JSON& r) { return r.has("status") ? r["status"].asString() : "<missing>"; }

struct Note {
    int pitch;
    double start, duration;
    double unit;
};
std::vector<Note> notesOf(MuseService& s, const std::string& pattern) {
    JSON r = call(s, "get_pattern", "\"pattern\": " + pattern);
    std::vector<Note> out;
    for (size_t i = 0; i < r["result"]["notes"].size(); ++i) {
        auto n = r["result"]["notes"][i];
        out.push_back({static_cast<int>(n["pitch"].asNumber()), n["start"].asNumber(), n["duration"].asNumber(),
                       n["unit"].asNumber()});
    }
    std::sort(out.begin(), out.end(), [](const Note& a, const Note& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    return out;
}
double lengthOf(MuseService& s, const std::string& pattern) {
    JSON r = call(s, "list_patterns", "");
    for (size_t i = 0; i < r["result"]["patterns"].size(); ++i)
        if (std::to_string(static_cast<long long>(r["result"]["patterns"][i]["id"].asNumber())) == pattern)
            return r["result"]["patterns"][i]["lengthBeats"].asNumber();
    return -1.0;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-6; }
double lastDuration(const std::vector<Note>& n) { return n.empty() ? -1.0 : n.back().duration; }
} // namespace

int main() {
    auto tracks = std::make_shared<TrackManager>();
    tracks->getUnitManager().setPatternManager(&tracks->getPatternManager());
    AudioEngine engine;
    engine.setSampleRate(48000);
    MuseService::wireHeadlessEngine(tracks, engine);
    CommandRegistry::initialize();
    MuseService s(tracks.get(), &engine);
    auto& history = tracks->getCommandHistory();

    call(s, "add_unit", "\"name\": \"Keys\"");
    call(s, "add_unit", "\"name\": \"Bass\"");
    JSON units = call(s, "list_units", "");
    const std::string keys = std::to_string(static_cast<long long>(units["result"]["units"][0]["id"].asNumber()));
    const std::string bass = std::to_string(static_cast<long long>(units["result"]["units"][1]["id"].asNumber()));
    const std::string p =
        std::to_string(static_cast<long long>(units["result"]["units"][0]["defaultPatternId"].asNumber()));
    const double startLength = lengthOf(s, p);

    std::cout << "[1] add_notes\n";
    JSON r = call(s, "add_notes", "\"pattern\": " + p + ", \"unit\": " + keys +
                                      ", \"notes\": \"0,60,1; 1,64,0.5,0.9; 2,67,1; 3,72,0.25\"");
    check(status(r) == "ok", "add_notes ok");
    auto n = notesOf(s, p);
    check(n.size() == 4, "four notes written (" + std::to_string(n.size()) + ")");
    check(n.size() == 4 && n[1].pitch == 64 && near(n[1].duration, 0.5), "the second note is 64 for half a beat");
    history.undo();
    check(notesOf(s, p).empty(), "one undo removes the whole batch");
    history.redo();
    check(notesOf(s, p).size() == 4, "redo puts them back");

    r = call(s, "add_notes", "\"pattern\": " + p + ", \"unit\": " + keys + ", \"notes\": \"4,60,1; 5,200,1\"");
    check(status(r) != "ok", "a pitch out of range rejects the batch");
    r = call(s, "add_notes", "\"pattern\": " + p + ", \"unit\": " + keys + ", \"notes\": \"4,60\"");
    check(status(r) != "ok", "an entry without a duration rejects the batch");
    check(notesOf(s, p).size() == 4, "and nothing from a rejected batch lands");

    call(s, "add_notes", "\"pattern\": " + p + ", \"unit\": " + bass + ", \"notes\": \"0,36,2; 2,36,2\"");
    check(notesOf(s, p).size() == 6, "a second unit's notes share the pattern");

    std::cout << "[2] set_note_length\n";
    r = call(s, "set_note_length",
             "\"pattern\": " + p + ", \"unit\": " + keys + ", \"pitch\": 72, \"start\": 3, \"duration\": 1.5");
    check(status(r) == "ok", "set_note_length ok");
    n = notesOf(s, p);
    check(near(lastDuration(n), 1.5), "the note is 1.5 beats long");
    history.undo();
    check(near(lastDuration(notesOf(s, p)), 0.25), "undo restores 0.25");
    r = call(s, "set_note_length",
             "\"pattern\": " + p + ", \"unit\": " + keys + ", \"pitch\": 71, \"start\": 3, \"duration\": 1");
    check(status(r) != "ok", "a note that is not there is an error");

    std::cout << "[3] delete_notes\n";
    r = call(s, "delete_notes", "\"pattern\": " + p + ", \"from\": 1, \"to\": 3, \"unit\": " + keys);
    check(status(r) == "ok", "delete_notes ok");
    n = notesOf(s, p);
    // Left: keys 60@0, 72@3; bass 36@0, 36@2 (bass at 2 is another unit, so it stays).
    check(n.size() == 4, "the two keys notes starting in [1, 3) are gone, bass kept (" + std::to_string(n.size()) + ")");
    history.undo();
    check(notesOf(s, p).size() == 6, "undo brings them back");
    r = call(s, "delete_notes", "\"pattern\": " + p + ", \"from\": 0, \"to\": 4, \"pitch_low\": 60, \"pitch_high\": 64");
    check(status(r) == "ok" && notesOf(s, p).size() == 4, "a pitch span limits the delete to 60..64");
    history.undo();
    r = call(s, "delete_notes", "\"pattern\": " + p + ", \"from\": 40, \"to\": 48");
    check(status(r) != "ok", "an empty range is an error, not a silent no-op step");

    std::cout << "[4] duplicate_notes\n";
    r = call(s, "duplicate_notes", "\"pattern\": " + p + ", \"from\": 0, \"to\": 4");
    check(status(r) == "ok", "duplicate_notes ok");
    n = notesOf(s, p);
    check(n.size() == 12, "all six notes copied (" + std::to_string(n.size()) + ")");
    const bool shifted = std::count_if(n.begin(), n.end(), [](const Note& x) { return x.start >= 4.0 - 1e-9; }) == 6;
    check(shifted, "the copies start four beats on");
    r = call(s, "duplicate_notes", "\"pattern\": " + p + ", \"from\": 4, \"to\": 8, \"unit\": " + bass);
    check(status(r) == "ok" && notesOf(s, p).size() == 14, "a unit filter copies only that unit's notes");
    history.undo();
    history.undo();
    check(notesOf(s, p).size() == 6, "each duplicate is one undo step");

    // Grow: a pattern of startLength beats; copy the last bar past its end.
    const double last = startLength - 4.0;
    call(s, "add_notes", "\"pattern\": " + p + ", \"unit\": " + keys + ", \"notes\": \"" + std::to_string(last) + ",50,1\"");
    r = call(s, "duplicate_notes",
             "\"pattern\": " + p + ", \"from\": " + std::to_string(last) + ", \"to\": " + std::to_string(startLength));
    check(status(r) == "ok", "a copy past the end is allowed");
    check(near(lengthOf(s, p), startLength + 4.0), "and the pattern grows by one bar (" + std::to_string(lengthOf(s, p)) + ")");
    history.undo();
    check(near(lengthOf(s, p), startLength), "undo shrinks it back in the same step");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
