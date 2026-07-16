// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MuseService Tests — the structured JSON surface agents drive Aestra through.
// Covers: request/response protocol, query verbs (eyes), mutation verbs
// routed through CommandHistory (hands + undo), stable IDs, and malformed
// input never crashing or corrupting the session.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"

#include <iostream>
#include <memory>
#include <string>

using Aestra::Audio::CommandRegistry;
using Aestra::Audio::MuseService;
using Aestra::Audio::TrackManager;
using Aestra::JSON;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        std::cout << "FAIL: " << label << "\n";
        ++g_failures;
    }
}

JSON call(MuseService& service, const std::string& request) {
    const std::string responseText = service.handleRequest(request);
    return JSON::parse(responseText);
}

std::string status(JSON& response) {
    return response.has("status") ? response["status"].asString() : "<missing>";
}

} // namespace

int main() {
    auto trackManager = std::make_shared<TrackManager>();
    CommandRegistry::initialize(trackManager.get());
    MuseService service(trackManager.get(), nullptr);

    // --- Protocol: malformed input never crashes, always structured error ---
    {
        JSON r = call(service, "not json at all");
        check(status(r) == "parse_error", "garbage input -> parse_error");

        r = call(service, "[1,2,3]");
        check(status(r) == "parse_error", "non-object request -> parse_error");

        r = call(service, "{\"id\": 7}");
        check(status(r) == "parse_error" && r["id"].asNumber() == 7.0,
              "missing verb -> parse_error, id echoed");

        r = call(service, "{\"id\": 8, \"verb\": \"warp_reality\"}");
        check(status(r) == "parse_error" && r["message"].asString().find("unknown") != std::string::npos,
              "unknown verb -> parse_error with message");
    }

    // --- Eyes: queries on an empty session ---
    {
        JSON r = call(service, "{\"id\": 1, \"verb\": \"list_tracks\"}");
        check(status(r) == "ok", "list_tracks ok on empty session");
        check(r["result"]["tracks"].size() == 0, "empty session has zero tracks");

        r = call(service, "{\"id\": 2, \"verb\": \"get_session_state\"}");
        check(status(r) == "ok", "get_session_state ok");
        check(r["result"]["transport"]["playing"].isBool(), "session state reports transport");
    }

    // --- Hands: build a session through mutations ---
    {
        JSON r = call(service, "{\"id\": 10, \"verb\": \"add_track\", \"args\": {\"name\": \"Drums\"}}");
        check(status(r) == "ok", "add_track Drums ok");
        r = call(service, "{\"id\": 11, \"verb\": \"add_track\", \"args\": {\"name\": \"Bass\"}}");
        check(status(r) == "ok", "add_track Bass ok");

        r = call(service, "{\"id\": 12, \"verb\": \"list_tracks\"}");
        check(r["result"]["tracks"].size() == 2, "two tracks listed");
        check(r["result"]["tracks"][0]["name"].asString() == "Drums", "track 0 named Drums");
        check(r["result"]["tracks"][1]["name"].asString() == "Bass", "track 1 named Bass");
        check(r["result"]["tracks"][0]["id"].isNumber() &&
                  r["result"]["tracks"][0]["id"].asNumber() !=
                      r["result"]["tracks"][1]["id"].asNumber(),
              "tracks carry distinct stable ids");
    }

    // --- Typed args: numbers and bools cross the JSON->flag boundary ---
    {
        JSON r = call(service,
                      "{\"id\": 20, \"verb\": \"set_volume\", \"args\": {\"track\": 0, \"value\": 0.5}}");
        check(status(r) == "ok", "set_volume with numeric args ok");

        r = call(service, "{\"id\": 21, \"verb\": \"list_tracks\"}");
        const double vol = r["result"]["tracks"][0]["volume"].asNumber();
        check(vol > 0.49 && vol < 0.51, "volume readback reflects mutation");

        r = call(service,
                 "{\"id\": 22, \"verb\": \"mute_track\", \"args\": {\"track\": 1, \"state\": true}}");
        check(status(r) == "ok", "mute_track with bool arg ok");
        r = call(service, "{\"id\": 23, \"verb\": \"list_tracks\"}");
        check(r["result"]["tracks"][1]["muted"].asBool(), "mute readback reflects mutation");
    }

    // --- Validation: schema ranges still guard the JSON path ---
    {
        JSON r = call(service,
                      "{\"id\": 30, \"verb\": \"set_volume\", \"args\": {\"track\": 0, \"value\": 4.0}}");
        check(status(r) == "validation_error", "out-of-range volume -> validation_error");

        r = call(service, "{\"id\": 31, \"verb\": \"set_volume\", \"args\": {\"track\": 0}}");
        check(status(r) == "validation_error", "missing required flag -> validation_error");

        r = call(service, "{\"id\": 32, \"verb\": \"list_tracks\"}");
        const double vol = r["result"]["tracks"][0]["volume"].asNumber();
        check(vol > 0.49 && vol < 0.51, "failed mutations leave state untouched");
    }

    // --- Revision loop: undo through the same history the UI uses ---
    {
        auto& history = trackManager->getCommandHistory();
        check(history.canUndo(), "mutations were recorded as undoable history");
        history.undo(); // un-mute Bass
        JSON r = call(service, "{\"id\": 40, \"verb\": \"list_tracks\"}");
        check(!r["result"]["tracks"][1]["muted"].asBool(), "undo reverses the muse mutation");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
