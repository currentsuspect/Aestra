// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The seam between MuseService and its verb families.
//
// MuseService.cpp was one 2200-line if-chain. Tests/Guards/file_size_baseline.txt
// pinned it at its exact line count, so it could never grow again — which is the
// guard doing its job: a hub that every new verb has to be threaded through is a
// hub nobody can review. The verb bodies now live one family per translation
// unit, and this header is what they share.
//
// The move is mechanical. Each family handler re-declares the three envelope
// helpers handleRequest() used to declare — makeOk / finish / makeError — with
// the names and signatures the moved code already used, and binds `id`, `verb`,
// `request`, `m_trackManager`, `m_engine` and `m_projectLoadReport` to locals of
// the same names. A verb body therefore still reads exactly as it did before,
// which is what makes "this refactor changed no behaviour" a claim you can
// check rather than take on trust.
//
// What moved here rather than into a family file: the helpers more than one
// family calls, plus the verb classification, because handleRequest and the
// batch verb must agree on it. There is one copy of each, as before.

#include "Commands/CommandResult.h"
#include "Commands/MuseGrammar.h"
#include "Models/UnitManager.h"

#include "AestraJSON.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace Aestra {
namespace Audio {

class AudioEngine;
class TrackManager;

/**
 * @brief The seam the families and the dispatcher share.
 *
 * Only the NEW surface lives in here. The helpers further down stay directly in
 * Aestra::Audio, unqualified, because every moved verb body calls them by their
 * bare names — `makeError(id, ...)`, `isQueryVerb(verb)` — and a lookup that has
 * to walk out of MuseInternal to find them still works, whereas renaming the
 * calls to match a tidier namespace would have meant editing all of them.
 */
namespace MuseInternal {

/**
 * @brief Everything a family handler is allowed to see of a request.
 *
 * A narrow struct rather than a MuseService& on purpose: the families have no
 * route to the service's other private state, so that is structural instead of
 * a matter of whoever remembers not to.
 */
struct RequestContext {
    double id = 0.0;
    std::string verb;
    JSON* request = nullptr;
    TrackManager* trackManager = nullptr;
    AudioEngine* engine = nullptr;
    const std::optional<JSON>* projectLoadReport = nullptr;
};

/**
 * @brief The ok envelope and the executionMs stamp, paired.
 *
 * Each family builds one on entry, so executionMs measures that family's work.
 * Before the split a single clock started just before the dispatch chain, which
 * for a verb handled late in the chain also counted the verbs already tried and
 * rejected. Both readings are "how long serving this request took"; this one
 * stops crediting a routing graph for the session queries that missed.
 */
class ResponseEnvelope {
public:
    ResponseEnvelope(double id, std::string verb)
        : m_id(id), m_verb(std::move(verb)), m_start(std::chrono::steady_clock::now()) {}

    JSON ok() const {
        JSON response = JSON::object();
        response.set("id", JSON(m_id));
        response.set("status", JSON("ok"));
        response.set("verb", JSON(m_verb));
        return response;
    }

    std::string finish(JSON& response) const {
        const auto end = std::chrono::steady_clock::now();
        response.set("executionMs",
                     JSON(std::chrono::duration<double, std::milli>(end - m_start).count()));
        return response.toString();
    }

private:
    double m_id;
    std::string m_verb;
    std::chrono::steady_clock::time_point m_start;
};

} // namespace MuseInternal

// --- Shared helpers. Bodies are the ones MuseService.cpp carried. -------------
//
// Deliberately in Aestra::Audio, not MuseInternal: see the note on the namespace
// above. These are the names the moved verb bodies already use.

inline const char* statusName(CommandStatus status) {
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
inline std::string numberToFlagString(double value) {
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

inline JSON makeError(double id, const char* status, const std::string& message,
                      const std::string& verb = "") {
    JSON response = JSON::object();
    response.set("id", JSON(id));
    response.set("status", JSON(status));
    if (!verb.empty()) response.set("verb", JSON(verb));
    response.set("message", JSON(message));
    return response;
}

// JSON numbers are doubles; an id must be a non-negative integer that a
// double represents exactly, or the cast would silently target the wrong
// object.
inline bool numberToId(double value, uint64_t& out) {
    constexpr double kMaxExactJsonInteger = 9007199254740991.0; // 2^53 - 1
    if (!std::isfinite(value) || value < 0.0 || value != std::floor(value) ||
        value > kMaxExactJsonInteger) {
        return false;
    }
    out = static_cast<uint64_t>(value);
    return true;
}

// JSON args -> flag map for the schema/registry path. Returns false with
// outError set when a value has a type the flag grammar cannot carry.
inline bool jsonArgsToFlags(JSON& args, std::unordered_map<std::string, std::string>& outFlags,
                            std::string& outError) {
    // Non-const asObject() returns a copy of the map (the const overload
    // returns an empty static — a known footgun).
    for (auto& entry : args.asObject()) {
        const std::string& key = entry.first;
        JSON& value = entry.second;
        if (value.isString()) {
            outFlags[key] = value.asString();
        } else if (value.isNumber()) {
            outFlags[key] = numberToFlagString(value.asNumber());
        } else if (value.isBool()) {
            outFlags[key] = value.asBool() ? "true" : "false";
        } else {
            outError = "arg '" + key + "' must be a string, number, or bool";
            return false;
        }
    }
    return true;
}

// Called by the session query that lists units and by the routing graph, which
// labels nodes by unit type. Two families, so it lives here rather than in
// either.
inline const char* unitTypeName(UnitType type) {
    switch (type) {
    case UnitType::Sampler: return "sampler";
    case UnitType::PitchedSampler: return "808";
    case UnitType::Instrument: return "instrument";
    case UnitType::Audio: return "audio";
    }
    return "unknown";
}

// --- Verb classification -----------------------------------------------------
//
// handleRequest uses these to reject an unknown verb and to refuse arguments on
// a query that takes none; the batch verb uses them to check each sub-verb
// before running it. One definition, so the two can never drift.

// Query verbs MuseService answers directly (mutations live in MuseGrammar).
inline bool isQueryVerb(const std::string& verb) {
    return verb == "get_transport" || verb == "list_tracks" || verb == "list_clips" || verb == "get_session_state" ||
           verb == "list_units" || verb == "get_pattern" || verb == "list_patterns" || verb == "list_plugins" ||
           verb == "get_effects" || verb == "list_samples" || verb == "get_meters" || verb == "get_schema" ||
           verb == "get_capabilities" || verb == "get_audio_health" || verb == "get_project_load_report" ||
           verb == "get_routing_graph" || verb == "get_latency_report";
}

// The few queries that take arguments; every other query rejects them.
inline bool queryTakesArgs(const std::string& verb) {
    return verb == "get_pattern" || verb == "get_effects" || verb == "list_samples";
}

// Service actions: handled here like queries, but they do work (render a
// file, run a batch) rather than read state. render_pattern is not routed
// through CommandHistory — a bounce is not an undoable project edit; batch
// pushes one CommandTransaction so the whole group is a single undo step.
inline bool isActionVerb(const std::string& verb) {
    return verb == "render_pattern" || verb == "render_song" || verb == "batch" ||
           verb == "undo" || verb == "redo";
}

inline bool isKnownVerb(const std::string& verb) {
    if (isQueryVerb(verb) || isActionVerb(verb)) return true;
    for (const auto& cmd : MuseGrammar::allCommands()) {
        if (cmd.verb == verb) return true;
    }
    return false;
}

// --- The families ------------------------------------------------------------
//
// Each returns nullopt when the verb is not its own. handleRequest tries them in
// order and then falls through to the mutation path, which is exactly what the
// old sequential if-chain did.

namespace MuseInternal {

std::optional<std::string> handleSessionVerbs(const RequestContext& ctx, const ResponseEnvelope& env);
std::optional<std::string> handleRoutingVerbs(const RequestContext& ctx, const ResponseEnvelope& env);
std::optional<std::string> handleLibraryVerbs(const RequestContext& ctx, const ResponseEnvelope& env);
std::optional<std::string> handleHistoryVerbs(const RequestContext& ctx, const ResponseEnvelope& env);
std::optional<std::string> handleRenderVerbs(const RequestContext& ctx, const ResponseEnvelope& env);

} // namespace MuseInternal

} // namespace Audio
} // namespace Aestra
