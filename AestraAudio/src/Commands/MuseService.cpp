// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/MuseService.h"

#include "Commands/CommandParser.h"
#include "Commands/CommandResult.h"
#include "Commands/MuseServiceInternal.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"

#include <cmath>
#include <chrono>
#include <exception>
#include <string>
#include <unordered_map>

namespace Aestra {
namespace Audio {

MuseService::MuseService(TrackManager* trackManager, AudioEngine* engine)
    : m_trackManager(trackManager), m_engine(engine) {}

void MuseService::wireHeadlessEngine(const std::shared_ptr<TrackManager>& trackManager,
                                     AudioEngine& engine) {
    engine.setTrackManager(trackManager);
    engine.setUnitManager(&trackManager->getUnitManager());
    engine.setPatternPlaybackEngine(&trackManager->getPatternPlaybackEngine());
    engine.setContinuousParams(trackManager->getContinuousParams());
    // Meters: the engine writes per-slot peaks/RMS/LUFS only when a snapshot
    // buffer is installed (the app does this in AestraApp).
    auto meterBuffer = std::make_shared<MeterSnapshotBuffer>();
    engine.setMeterSnapshots(meterBuffer);
    trackManager->setMeterSnapshots(meterBuffer);
    trackManager->buildAndShareSlotMap();
    if (auto slotMap = trackManager->getChannelSlotMapShared()) {
        engine.setChannelSlotMap(slotMap);
    }

    // Transport commands (play/stop/seek) travel from TrackManager to the
    // engine through this sink — the same relay AestraContent installs.
    TrackManager* tm = trackManager.get();
    AudioEngine* enginePtr = &engine;
    tm->setCommandSink([enginePtr, tm](const AudioQueueCommand& cmd) -> bool {
        // Edges deliver reliably (bounded wait, counted in edgeDroppedCount()
        // on failure); state keeps best-effort drop-newest semantics (#913).
        bool accepted = false;
        if (AudioCommandQueue::isEdgeCommand(cmd.type)) {
            accepted = enginePtr->commandQueue().pushReliable(cmd);
        } else {
            accepted = enginePtr->commandQueue().push(cmd);
        }
        if (accepted && cmd.type == AudioQueueCommandType::SetTransportState) {
            const double sampleRate =
                std::max(1.0, static_cast<double>(enginePtr->getSampleRate()));
            // onTransportStateApplied resolves the kTransportPreservePosition pause
            // sentinel and the seconds conversion in one place (#590).
            tm->onTransportStateApplied(cmd.value1 != 0.0f, cmd.samplePos, sampleRate);
        }
        return accepted;
    });
}

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

        // Host verbs are namespaced (settings.setAudioDevice) and validated by
        // the registry, which owns their argument schemas. Built-ins are
        // checked here. A host verb can never shadow a built-in: the registry
        // refuses the reserved audio. prefix and every built-in is unprefixed.
        const bool isHostVerb = m_hostVerbs.has(verb);

        // Classify the verb before any dependency checks so protocol status
        // never depends on how the service happens to be wired.
        if (!isHostVerb && !isKnownVerb(verb)) {
            return makeError(id, "parse_error", "unknown command: " + verb).toString();
        }

        // Most queries take no arguments; accepting any would silently drop
        // caller intent.
        if (!isHostVerb && isQueryVerb(verb) && !queryTakesArgs(verb) && request.has("args") &&
            request["args"].size() > 0) {
            return makeError(id, "validation_error", verb + " takes no arguments", verb).toString();
        }

        // The verbs that stay here: the host seam and the manifest. They are
        // short, they sit between the session and routing families in the old
        // order, and they are the only ones that need m_hostVerbs.
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

        // ------------------------------------------------------------------
        // Verb families.
        //
        // The chain that used to be one 2200-line if-statement, now one call
        // per family. Order is the old order: session state, then the host seam
        // and the manifest below, then routing diagnostics, library reads,
        // history, and finally the render actions. Each family returns nullopt
        // for a verb it does not own, which is what a fall-through past its
        // internal if-chain used to do.
        //
        // A family that answers owns the request outright: it returns the
        // finished response string, including its own executionMs stamp.
        // ------------------------------------------------------------------
        // Named museCtx, not ctx: the mutation fallback further down declares its
        // own `CommandContext ctx`, lifted verbatim from the original, and two
        // locals of that name in one scope is a redefinition.
        const MuseInternal::RequestContext museCtx{
            id, verb, &request, m_trackManager, m_engine, &m_projectLoadReport};
        const MuseInternal::ResponseEnvelope envelope(id, verb);

        if (auto handled = MuseInternal::handleSessionVerbs(museCtx, envelope)) return *handled;

        // ------------------------------------------------------------------
        // Host verbs: capabilities the application registered into the seam.
        // MuseService does not know what any of them do — it validates against
        // the declared schema, checks that this process can honour the verb's
        // thread affinity, and calls the handler.
        // ------------------------------------------------------------------
        if (isHostVerb) {
            JSON noArgs = JSON::object();
            const JSON& hostArgs = request.has("args") ? request["args"] : noArgs;
            const HostVerbResult hostResult =
                m_hostVerbs.invoke(verb, hostArgs, m_hostUiThreadAvailable);

            if (!hostResult.ok) {
                // The registry's own refusals are argument/affinity problems;
                // anything else is the host declining to do the thing. Keep the
                // machine-readable code in the response so callers never have to
                // pattern-match prose to tell them apart.
                const bool validation = hostResult.errorCode == "invalid_args" ||
                                        hostResult.errorCode == "missing_arg" ||
                                        hostResult.errorCode == "unknown_verb";
                JSON response = makeError(id, validation ? "validation_error" : "execution_error",
                                          hostResult.message, verb);
                response.set("errorCode", JSON(hostResult.errorCode));
                return finish(response);
            }

            JSON response = makeOk();
            response.set("result", hostResult.result);
            return finish(response);
        }

        if (verb == "get_capabilities") {
            // "What can this host do?" — so an agent discovers the surface
            // instead of hardcoding assumptions about which build it is talking
            // to. A headless session legitimately answers with an empty list.
            JSON verbs = JSON::array();
            for (const auto& spec : m_hostVerbs.capabilities()) {
                JSON entry = JSON::object();
                entry.set("verb", JSON(spec.name));
                entry.set("domain", JSON(std::string(HostVerbRegistry::domainName(spec.domain))));
                entry.set("description", JSON(spec.description));
                entry.set("mutates", JSON(spec.mutates));
                entry.set("requiresHostUi",
                          JSON(spec.affinity == HostThreadAffinity::HostUiThread));
                JSON args = JSON::array();
                for (const auto& arg : spec.args) {
                    JSON a = JSON::object();
                    a.set("name", JSON(arg.name));
                    a.set("type", JSON(std::string(HostVerbRegistry::argTypeName(arg.type))));
                    a.set("required", JSON(arg.required));
                    if (!arg.description.empty()) a.set("description", JSON(arg.description));
                    if (!std::isnan(arg.minValue)) a.set("min", JSON(arg.minValue));
                    if (!std::isnan(arg.maxValue)) a.set("max", JSON(arg.maxValue));
                    args.push(a);
                }
                entry.set("args", args);
                verbs.push(entry);
            }
            JSON result = JSON::object();
            result.set("hostVerbs", verbs);
            result.set("hostUiAvailable", JSON(m_hostUiThreadAvailable));
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "get_schema") {
            // The tool manifest, over the wire: a socket client can bootstrap
            // without access to the binary's --schema flag.
            JSON result = JSON::parse(MuseGrammar::schemaToJsonString());
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }
        if (auto handled = MuseInternal::handleRoutingVerbs(museCtx, envelope)) return *handled;
        if (auto handled = MuseInternal::handleLibraryVerbs(museCtx, envelope)) return *handled;
        if (auto handled = MuseInternal::handleHistoryVerbs(museCtx, envelope)) return *handled;
        if (auto handled = MuseInternal::handleRenderVerbs(museCtx, envelope)) return *handled;

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
            std::string convertError;
            if (!jsonArgsToFlags(args, flags, convertError)) {
                return makeError(id, "parse_error", convertError, verb).toString();
            }
        }

        CommandParser parser;
        const CommandContext ctx{m_engine, m_trackManager};
        CommandResult cmdResult =
            parser.execute(verb, flags, m_trackManager->getCommandHistory(), ctx);

        JSON response = JSON::object();
        response.set("id", JSON(id));
        response.set("status", JSON(statusName(cmdResult.status)));
        response.set("verb", JSON(verb));
        response.set("message", JSON(cmdResult.message));
        response.set("undoable", JSON(cmdResult.undoable));
        if (cmdResult.hasCreatedId) {
            // Structured id of the object the command created (e.g. the new
            // pattern from clone_pattern) — agents must not parse the message.
            JSON result = JSON::object();
            result.set("createdId", JSON(static_cast<double>(cmdResult.createdId)));
            response.set("result", result);
        }
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
