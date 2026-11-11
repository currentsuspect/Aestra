// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MuseTrackAddressingTest — #1042: list_tracks gave each track an `id` that track verbs do not accept, so an agent
// echoing it edited the wrong track. Each entry now carries `track`, the exact value track verbs take.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseGrammar.h"
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
    return JSON::parse(s.handleRequest("{\"id\": " + std::to_string(g_id++) + ", \"verb\": \"" + verb +
                                       "\", \"args\": {" + args + "}}"));
}
} // namespace

int main() {
    auto tracks = std::make_shared<TrackManager>();
    AudioEngine engine;
    engine.setSampleRate(48000);
    MuseService::wireHeadlessEngine(tracks, engine);
    CommandRegistry::initialize();
    MuseService s(tracks.get(), &engine);

    for (const char* name : {"ALPHA", "BRAVO", "CHARLIE"})
        call(s, "add_track", std::string("\"name\": \"") + name + "\"");

    // An agent reads ALPHA's handle and passes it straight back.
    JSON list = call(s, "list_tracks", "");
    JSON alpha;
    for (size_t i = 0; i < list["result"]["tracks"].size(); ++i)
        if (list["result"]["tracks"][i]["name"].asString() == "ALPHA")
            alpha = list["result"]["tracks"][i];
    check(alpha.has("track"), "list_tracks gives each track a `track` handle");
    const std::string handle = alpha.has("track") ? std::to_string(static_cast<long long>(alpha["track"].asNumber())) : "-1";
    call(s, "set_volume", "\"track\": " + handle + ", \"value\": 0.25");

    list = call(s, "list_tracks", "");
    bool alphaChanged = false, othersUntouched = true;
    for (size_t i = 0; i < list["result"]["tracks"].size(); ++i) {
        JSON t = list["result"]["tracks"][i];
        const bool isAlpha = t["name"].asString() == "ALPHA";
        const bool changed = std::abs(t["volume"].asNumber() - 0.25) < 1e-6;
        if (isAlpha) alphaChanged = changed;
        else if (changed) othersUntouched = false;
    }
    check(alphaChanged && othersUntouched, "echoing `track` into set_volume edits ALPHA and nothing else");

    // The schema says which field to pass back, and that plain `play` is not the UI's Play button (#1000).
    const std::string schema = Aestra::Audio::MuseGrammar::schemaToJsonString();
    check(schema.find("transport.play") != std::string::npos, "the play verb points agents at transport.play");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
