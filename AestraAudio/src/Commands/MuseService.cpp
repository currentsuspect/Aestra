// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/MuseService.h"

#include "Commands/CommandParser.h"
#include "Commands/CommandResult.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"

#include <variant>

#include "AestraJSON.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <unordered_map>

namespace Aestra {
namespace Audio {

namespace {

const char* statusName(CommandStatus status) {
    switch (status) {
    case CommandStatus::Success: return "ok";
    case CommandStatus::ParseError: return "parse_error";
    case CommandStatus::ValidationError: return "validation_error";
    case CommandStatus::ExecutionError: return "execution_error";
    }
    return "execution_error";
}

// Render a JSON number the way the flag validators expect to re-parse it:
// integral values without a fractional tail ("142", not "142.000000").
std::string numberToFlagString(double value) {
    if (std::isfinite(value) && value == std::floor(value) &&
        std::abs(value) < 9.007199254740992e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", value);
        return buf;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", value);
    return buf;
}

JSON makeError(double id, const char* status, const std::string& message,
               const std::string& verb = "") {
    JSON response = JSON::object();
    response.set("id", JSON(id));
    response.set("status", JSON(status));
    if (!verb.empty()) response.set("verb", JSON(verb));
    response.set("message", JSON(message));
    return response;
}

JSON trackToJson(const MixerChannel& channel, size_t index) {
    JSON t = JSON::object();
    t.set("index", JSON(static_cast<double>(index)));
    t.set("id", JSON(static_cast<double>(channel.getChannelId())));
    t.set("name", JSON(channel.getName()));
    t.set("volume", JSON(static_cast<double>(channel.getVolume())));
    t.set("pan", JSON(static_cast<double>(channel.getPan())));
    t.set("muted", JSON(channel.isMuted()));
    t.set("soloed", JSON(channel.isSoloed()));
    return t;
}

} // namespace

MuseService::MuseService(TrackManager* trackManager, AudioEngine* engine)
    : m_trackManager(trackManager), m_engine(engine) {}

namespace {

// Query verbs MuseService answers directly (mutations live in MuseGrammar).
bool isQueryVerb(const std::string& verb) {
    return verb == "get_transport" || verb == "list_tracks" || verb == "list_clips" ||
           verb == "get_session_state" || verb == "list_units" || verb == "get_pattern";
}

const char* unitTypeName(UnitType type) {
    switch (type) {
    case UnitType::Sampler: return "sampler";
    case UnitType::PitchedSampler: return "808";
    case UnitType::Instrument: return "instrument";
    case UnitType::Audio: return "audio";
    }
    return "unknown";
}

bool isKnownVerb(const std::string& verb) {
    if (isQueryVerb(verb)) return true;
    for (const auto& cmd : MuseGrammar::allCommands()) {
        if (cmd.verb == verb) return true;
    }
    return false;
}

} // namespace

std::string MuseService::handleRequest(const std::string& requestJson) {
    double id = 0.0;
    std::string verb;

    // Parsing gets its own try so only malformed JSON reports parse_error;
    // runtime failures later map to execution_error.
    JSON request;
    try {
        request = JSON::parse(requestJson);
    } catch (const std::exception& e) {
        return makeError(id, "parse_error", std::string("invalid request: ") + e.what()).toString();
    } catch (...) {
        return makeError(id, "parse_error", "invalid request").toString();
    }

    try {
        if (!request.isObject()) {
            return makeError(id, "parse_error", "request must be a JSON object").toString();
        }

        if (request.has("id")) {
            if (!request["id"].isNumber()) {
                // A wrong-typed id would silently echo 0 and mis-correlate
                // responses; reject instead.
                return makeError(id, "parse_error", "id must be a number").toString();
            }
            id = request["id"].asNumber();
        }
        if (!request.has("verb") || !request["verb"].isString()) {
            return makeError(id, "parse_error", "missing string field: verb").toString();
        }
        verb = request["verb"].asString();

        // Classify the verb before any dependency checks so protocol status
        // never depends on how the service happens to be wired.
        if (!isKnownVerb(verb)) {
            return makeError(id, "parse_error", "unknown command: " + verb).toString();
        }

        // Queries take no arguments — except get_pattern, which takes exactly
        // one. Accepting anything else would silently drop caller intent.
        if (isQueryVerb(verb) && verb != "get_pattern" && request.has("args") &&
            request["args"].size() > 0) {
            return makeError(id, "validation_error", verb + " takes no arguments", verb).toString();
        }

        // ------------------------------------------------------------------
        // Query verbs: read-only, no history, stable IDs.
        // ------------------------------------------------------------------
        const auto startTime = std::chrono::steady_clock::now();
        const auto finish = [&](JSON& response) {
            const auto endTime = std::chrono::steady_clock::now();
            response.set("executionMs",
                         JSON(std::chrono::duration<double, std::milli>(endTime - startTime).count()));
            return response.toString();
        };
        const auto makeOk = [&]() {
            JSON response = JSON::object();
            response.set("id", JSON(id));
            response.set("status", JSON("ok"));
            response.set("verb", JSON(verb));
            return response;
        };

        if (verb == "get_transport") {
            JSON result = JSON::object();
            if (m_engine) {
                result.set("bpm", JSON(static_cast<double>(m_engine->getBPM())));
                result.set("playing", JSON(m_engine->isTransportPlaying()));
                result.set("positionSeconds", JSON(m_engine->getPositionSeconds()));
            } else if (m_trackManager) {
                result.set("bpm", JSON(m_trackManager->getTimelineClock().getCurrentTempo()));
                result.set("playing", JSON(m_trackManager->isPlaying()));
                result.set("positionSeconds", JSON(m_trackManager->getPosition()));
            } else {
                return makeError(id, "execution_error", "no engine or track manager", verb).toString();
            }
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "list_tracks") {
            if (!m_trackManager) {
                return makeError(id, "execution_error", "no track manager", verb).toString();
            }
            JSON tracks = JSON::array();
            const size_t count = m_trackManager->getChannelCount();
            for (size_t i = 0; i < count; ++i) {
                if (const MixerChannel* ch = m_trackManager->getChannel(i)) {
                    tracks.push(trackToJson(*ch, i));
                }
            }
            JSON result = JSON::object();
            result.set("tracks", tracks);
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "list_clips") {
            if (!m_trackManager) {
                return makeError(id, "execution_error", "no track manager", verb).toString();
            }
            auto& playlist = m_trackManager->getPlaylistModel();
            JSON lanes = JSON::array();
            const size_t laneCount = playlist.getLaneCount();
            for (size_t i = 0; i < laneCount; ++i) {
                PlaylistLaneID laneId = playlist.getLaneId(i);
                PlaylistLane* lane = playlist.getLane(laneId);
                if (!lane) continue;
                JSON laneJson = JSON::object();
                laneJson.set("index", JSON(static_cast<double>(i)));
                laneJson.set("id", JSON(laneId.toString())); // full UUID — lossless
                laneJson.set("name", JSON(lane->name));
                JSON clips = JSON::array();
                for (const auto& clip : lane->clips) {
                    JSON c = JSON::object();
                    c.set("id", JSON(clip.id.toString())); // full UUID — lossless
                    c.set("name", JSON(clip.name));
                    c.set("startBeat", JSON(clip.startBeat));
                    c.set("durationBeats", JSON(clip.durationBeats));
                    clips.push(c);
                }
                laneJson.set("clips", clips);
                lanes.push(laneJson);
            }
            JSON result = JSON::object();
            result.set("lanes", lanes);
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "get_session_state") {
            if (!m_trackManager) {
                return makeError(id, "execution_error", "no track manager", verb).toString();
            }
            JSON result = JSON::object();

            JSON transport = JSON::object();
            if (m_engine) {
                transport.set("bpm", JSON(static_cast<double>(m_engine->getBPM())));
                transport.set("playing", JSON(m_engine->isTransportPlaying()));
            } else {
                transport.set("bpm", JSON(m_trackManager->getTimelineClock().getCurrentTempo()));
                transport.set("playing", JSON(m_trackManager->isPlaying()));
            }
            result.set("transport", transport);

            JSON tracks = JSON::array();
            const size_t count = m_trackManager->getChannelCount();
            for (size_t i = 0; i < count; ++i) {
                if (const MixerChannel* ch = m_trackManager->getChannel(i)) {
                    tracks.push(trackToJson(*ch, i));
                }
            }
            result.set("tracks", tracks);
            result.set("laneCount",
                       JSON(static_cast<double>(m_trackManager->getPlaylistModel().getLaneCount())));
            result.set("unitCount",
                       JSON(static_cast<double>(m_trackManager->getUnitManager().getUnitCount())));
            result.set("canUndo", JSON(m_trackManager->getCommandHistory().canUndo()));

            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "list_units") {
            if (!m_trackManager) {
                return makeError(id, "execution_error", "no track manager", verb).toString();
            }
            auto& unitManager = m_trackManager->getUnitManager();
            JSON units = JSON::array();
            for (UnitID unitId : unitManager.getAllUnitIDs()) {
                const UnitInfo* unit = unitManager.getUnit(unitId);
                if (!unit) continue;
                JSON u = JSON::object();
                u.set("id", JSON(static_cast<double>(unit->id)));
                u.set("name", JSON(unit->name));
                u.set("type", JSON(unitTypeName(unit->type)));
                u.set("enabled", JSON(unit->isEnabled));
                u.set("muted", JSON(unit->isMuted));
                u.set("soloed", JSON(unit->isSolo));
                u.set("defaultPatternId",
                      JSON(static_cast<double>(unit->defaultPatternId.value)));
                u.set("samplePath", JSON(unit->audioClipPath));
                u.set("sampleDurationSeconds", JSON(unit->audioDurationSeconds));
                units.push(u);
            }
            JSON result = JSON::object();
            result.set("units", units);
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "get_pattern") {
            if (!m_trackManager) {
                return makeError(id, "execution_error", "no track manager", verb).toString();
            }
            // The one parameterized query: args must be exactly
            // {"pattern": <number>}.
            if (!request.has("args") || !request["args"].isObject()) {
                return makeError(id, "validation_error",
                                 "get_pattern requires args: {\"pattern\": <id>}", verb)
                    .toString();
            }
            JSON& args = request["args"];
            for (auto& entry : args.asObject()) {
                if (entry.first != "pattern") {
                    return makeError(id, "validation_error",
                                     "unknown arg for get_pattern: " + entry.first, verb)
                        .toString();
                }
            }
            if (!args.has("pattern") || !args["pattern"].isNumber()) {
                return makeError(id, "validation_error", "arg 'pattern' must be a number", verb)
                    .toString();
            }

            const PatternID patternId{static_cast<uint64_t>(args["pattern"].asNumber())};
            const PatternSource* pattern =
                m_trackManager->getPatternManager().getPattern(patternId);
            if (!pattern) {
                return makeError(id, "execution_error",
                                 "no such pattern: " + std::to_string(patternId.value), verb)
                    .toString();
            }

            JSON result = JSON::object();
            result.set("id", JSON(static_cast<double>(pattern->id.value)));
            result.set("name", JSON(pattern->name));
            result.set("lengthBeats", JSON(pattern->lengthBeats));
            const bool isMidi = pattern->isMidi();
            result.set("type", JSON(isMidi ? "midi" : "other"));
            if (isMidi) {
                JSON notes = JSON::array();
                for (const MidiNote& note : std::get<MidiPayload>(pattern->payload).notes) {
                    JSON n = JSON::object();
                    n.set("pitch", JSON(static_cast<double>(note.pitch)));
                    n.set("start", JSON(note.startBeat));
                    n.set("duration", JSON(note.durationBeats));
                    n.set("velocity", JSON(static_cast<double>(note.velocity)));
                    n.set("pan", JSON(static_cast<double>(note.pan)));
                    n.set("unit", JSON(static_cast<double>(note.unitId)));
                    notes.push(n);
                }
                result.set("notes", notes);
            }
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        // ------------------------------------------------------------------
        // Mutation verbs: JSON args -> flag map -> the same schema
        // validation, registry factories, and CommandHistory the text
        // parser uses.
        // ------------------------------------------------------------------
        if (!m_trackManager) {
            return makeError(id, "execution_error", "no track manager", verb).toString();
        }

        std::unordered_map<std::string, std::string> flags;
        if (request.has("args")) {
            JSON& args = request["args"];
            if (!args.isObject()) {
                return makeError(id, "parse_error", "args must be an object", verb).toString();
            }
            // Non-const asObject() returns a copy of the map (the const
            // overload returns an empty static — a known footgun).
            for (auto& entry : args.asObject()) {
                const std::string& key = entry.first;
                JSON& value = entry.second;
                if (value.isString()) {
                    flags[key] = value.asString();
                } else if (value.isNumber()) {
                    flags[key] = numberToFlagString(value.asNumber());
                } else if (value.isBool()) {
                    flags[key] = value.asBool() ? "true" : "false";
                } else {
                    return makeError(id, "parse_error",
                                     "arg '" + key + "' must be a string, number, or bool", verb)
                        .toString();
                }
            }
        }

        CommandParser parser;
        CommandResult cmdResult =
            parser.execute(verb, flags, m_trackManager->getCommandHistory());

        JSON response = JSON::object();
        response.set("id", JSON(id));
        response.set("status", JSON(statusName(cmdResult.status)));
        response.set("verb", JSON(verb));
        response.set("message", JSON(cmdResult.message));
        response.set("undoable", JSON(cmdResult.undoable));
        response.set("executionMs", JSON(cmdResult.executionMs));
        return response.toString();
    } catch (const std::exception& e) {
        return makeError(id, "execution_error", e.what(), verb).toString();
    } catch (...) {
        return makeError(id, "execution_error", "internal error", verb).toString();
    }
}

} // namespace Audio
} // namespace Aestra
