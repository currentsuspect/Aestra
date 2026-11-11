// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MuseStepVerbsTest — the rest of V82-M2 (FD-53): per-step expression and the pattern menu, through Muse.
//   1. set_note sets one note's chance and ratchet; get_pattern reads them back; one undo restores them;
//   2. set_notes sets velocity, chance and ratchet on every note starting in a range (a drum row's steps);
//   3. rename_pattern renames, and one undo restores the name;
//   4. delete_pattern deletes, refuses a pattern that plays on the timeline or the only pattern, and undo restores.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

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
std::string idText(double v) { return std::to_string(static_cast<long long>(v)); }
bool approxEq(double a, double b) { return std::abs(a - b) < 1e-4; }

// The note at @p start (pitch 36) as get_pattern reports it, or an empty JSON.
JSON noteAt(MuseService& s, const std::string& pattern, double start) {
    JSON r = call(s, "get_pattern", "\"pattern\": " + pattern);
    for (size_t i = 0; r.has("result") && i < r["result"]["notes"].size(); ++i)
        if (approxEq(r["result"]["notes"][i]["start"].asNumber(), start))
            return r["result"]["notes"][i];
    return JSON();
}
double field(JSON n, const char* key) { return n.has(key) ? n[key].asNumber() : -1.0; }

std::string patternName(MuseService& s, const std::string& pattern) {
    JSON r = call(s, "list_patterns", "");
    for (size_t i = 0; i < r["result"]["patterns"].size(); ++i)
        if (idText(r["result"]["patterns"][i]["id"].asNumber()) == pattern)
            return r["result"]["patterns"][i]["name"].asString();
    return "<gone>";
}
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

    call(s, "add_unit", "\"name\": \"Hats\"");
    call(s, "add_unit", "\"name\": \"Kick\"");
    JSON units = call(s, "list_units", "");
    const std::string hats = idText(units["result"]["units"][0]["id"].asNumber());
    const std::string p = idText(units["result"]["units"][0]["defaultPatternId"].asNumber());
    const std::string kickPattern = idText(units["result"]["units"][1]["defaultPatternId"].asNumber());
    // Eight 16th hats, all at velocity 0.8.
    call(s, "add_notes", "\"pattern\": " + p + ", \"unit\": " + hats +
                             ", \"notes\": \"0,36,0.25; 0.25,36,0.25; 0.5,36,0.25; 0.75,36,0.25; 1,36,0.25; "
                             "1.25,36,0.25; 1.5,36,0.25; 1.75,36,0.25\"");

    std::cout << "[1] set_note chance and ratchet\n";
    JSON r = call(s, "set_note", "\"pattern\": " + p + ", \"unit\": " + hats +
                                     ", \"pitch\": 36, \"start\": 0.5, \"chance\": 0.5, \"ratchet\": 3");
    check(status(r) == "ok", "set_note with chance and ratchet ok");
    JSON n = noteAt(s, p, 0.5);
    check(approxEq(field(n, "chance"), 0.5), "get_pattern reads chance 0.5 (" + std::to_string(field(n, "chance")) + ")");
    check(approxEq(field(n, "ratchet"), 3), "and ratchet 3 (" + std::to_string(field(n, "ratchet")) + ")");
    check(approxEq(field(n, "velocity"), 0.8), "velocity untouched");
    history.undo();
    n = noteAt(s, p, 0.5);
    check(approxEq(field(n, "chance"), 1.0) && approxEq(field(n, "ratchet"), 1), "one undo restores chance 1, ratchet 1");
    r = call(s, "set_note", "\"pattern\": " + p + ", \"unit\": " + hats + ", \"pitch\": 36, \"start\": 0.5, \"ratchet\": 9");
    check(status(r) != "ok", "a ratchet above 4 is refused");

    std::cout << "[2] set_notes over a range\n";
    r = call(s, "set_notes", "\"pattern\": " + p + ", \"from\": 1, \"to\": 2, \"unit\": " + hats +
                                 ", \"velocity\": 0.4, \"chance\": 0.75");
    check(status(r) == "ok", "set_notes ok");
    check(approxEq(field(noteAt(s, p, 1.25), "velocity"), 0.4) && approxEq(field(noteAt(s, p, 1.75), "chance"), 0.75),
          "the four steps in beat 2 are softer and at 75%");
    check(approxEq(field(noteAt(s, p, 0.75), "velocity"), 0.8) && approxEq(field(noteAt(s, p, 0.75), "chance"), 1.0),
          "the steps before it are untouched");
    history.undo();
    check(approxEq(field(noteAt(s, p, 1.25), "velocity"), 0.8), "one undo restores the whole range");
    r = call(s, "set_notes", "\"pattern\": " + p + ", \"from\": 1, \"to\": 2");
    check(status(r) != "ok", "set_notes with nothing to set is refused");

    std::cout << "[3] rename_pattern\n";
    r = call(s, "rename_pattern", "\"pattern\": " + p + ", \"name\": \"Hat groove\"");
    check(status(r) == "ok" && patternName(s, p) == "Hat groove", "renamed to Hat groove");
    history.undo();
    check(patternName(s, p) != "Hat groove", "undo restores the old name");

    std::cout << "[4] delete_pattern\n";
    r = call(s, "arrange_pattern", "\"pattern\": " + kickPattern + ", \"track\": 0, \"start\": 0");
    check(status(r) == "ok", "the kick pattern is placed on the timeline");
    r = call(s, "delete_pattern", "\"pattern\": " + kickPattern);
    check(status(r) != "ok", "a pattern that plays on the timeline is refused");
    r = call(s, "delete_pattern", "\"pattern\": " + p);
    check(status(r) == "ok" && patternName(s, p) == "<gone>", "an unused pattern is deleted");
    history.undo();
    check(patternName(s, p) != "<gone>" && field(noteAt(s, p, 0.5), "pitch") == 36.0, "undo brings it back with its notes");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
