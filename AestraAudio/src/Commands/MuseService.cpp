// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/MuseService.h"

#include "Commands/CommandParser.h"
#include "Commands/MuseGrammar.h"
#include "Commands/CommandResult.h"
#include "Commands/CommandTransaction.h"
#include "Core/AudioEngine.h"
#include "Core/PlaybackGraphController.h"
#include "IO/AudioExporter.h"
#include "IO/AudioFileValidator.h"
#include "Plugin/EffectChain.h"
#include "Plugin/PluginManager.h"

#include <filesystem>
#include "Models/TrackManager.h"
#include "Models/MeterSnapshot.h"
#include "Models/UnitManager.h"

#include <variant>

#include "AestraJSON.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <unordered_map>
#include <vector>

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
    tm->setCommandSink([enginePtr, tm](const AudioQueueCommand& cmd) {
        enginePtr->commandQueue().push(cmd);
        if (cmd.type == AudioQueueCommandType::SetTransportState) {
            const double sampleRate =
                std::max(1.0, static_cast<double>(enginePtr->getSampleRate()));
            // onTransportStateApplied resolves the kTransportPreservePosition pause
            // sentinel and the seconds conversion in one place (#590).
            tm->onTransportStateApplied(cmd.value1 != 0.0f, cmd.samplePos, sampleRate);
        }
    });
}

namespace {

// Query verbs MuseService answers directly (mutations live in MuseGrammar).
bool isQueryVerb(const std::string& verb) {
    return verb == "get_transport" || verb == "list_tracks" || verb == "list_clips" ||
           verb == "get_session_state" || verb == "list_units" || verb == "get_pattern" ||
           verb == "list_patterns" || verb == "list_plugins" || verb == "get_effects" ||
           verb == "list_samples" || verb == "get_meters" || verb == "get_schema" ||
           verb == "get_capabilities" || verb == "get_audio_health";
}

// The few queries that take arguments; every other query rejects them.
bool queryTakesArgs(const std::string& verb) {
    return verb == "get_pattern" || verb == "get_effects" || verb == "list_samples";
}

// Service actions: handled here like queries, but they do work (render a
// file, run a batch) rather than read state. render_pattern is not routed
// through CommandHistory — a bounce is not an undoable project edit; batch
// pushes one CommandTransaction so the whole group is a single undo step.
bool isActionVerb(const std::string& verb) {
    return verb == "render_pattern" || verb == "render_song" || verb == "batch" ||
           verb == "undo" || verb == "redo";
}

// JSON args -> flag map for the schema/registry path. Returns false with
// outError set when a value has a type the flag grammar cannot carry.
bool jsonArgsToFlags(JSON& args, std::unordered_map<std::string, std::string>& outFlags,
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

// JSON numbers are doubles; an id must be a non-negative integer that a
// double represents exactly, or the cast would silently target the wrong
// object.
bool numberToId(double value, uint64_t& out) {
    constexpr double kMaxExactJsonInteger = 9007199254740991.0; // 2^53 - 1
    if (!std::isfinite(value) || value < 0.0 || value != std::floor(value) ||
        value > kMaxExactJsonInteger) {
        return false;
    }
    out = static_cast<uint64_t>(value);
    return true;
}

// Interleaved stereo float32 WAV — the format the offline test renderer uses.
bool writeFloat32Wav(const std::string& path, const std::vector<float>& samples,
                     uint32_t sampleRate) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(float));
    const uint16_t channels = 2;
    const uint16_t bitsPerSample = 32;
    const uint16_t blockAlign = channels * bitsPerSample / 8;

    auto u32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    file.write("RIFF", 4);
    u32(36 + dataBytes);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    u32(16);
    u16(3); // IEEE float
    u16(channels);
    u32(sampleRate);
    u32(sampleRate * blockAlign);
    u16(blockAlign);
    u16(bitsPerSample);
    file.write("data", 4);
    u32(dataBytes);
    file.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
    file.close(); // a failed flush on close must not report success
    return !file.fail();
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
    if (isQueryVerb(verb) || isActionVerb(verb)) return true;
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
                    // 0 = not a pattern clip
                    c.set("pattern", JSON(static_cast<double>(clip.patternId.value)));
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
                u.set("gain", JSON(static_cast<double>(unit->gain)));
                u.set("muted", JSON(unit->isMuted));
                u.set("soloed", JSON(unit->isSolo));
                u.set("defaultPatternId",
                      JSON(static_cast<double>(unit->defaultPatternId.value)));
                u.set("mixerChannelId", JSON(static_cast<double>(unitManager.getUnitMixerChannel(unitId))));
                // Compatibility metadata retained for older clients. It no longer controls audio routing.
                u.set("timelineLane",
                      JSON(static_cast<double>(unitManager.getUnitTimelineLane(unitId))));
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

        if (verb == "get_audio_health") {
            if (!m_engine) {
                return makeError(id, "execution_error", "no audio engine", verb).toString();
            }

            // Consume only lock-free state the callback already publishes. A
            // diagnostic read must never make the realtime thread wait.
            const auto& telemetry = m_engine->telemetry();
            const uint64_t blocks = telemetry.getBlocksProcessed();
            const uint64_t timedCallbacks = telemetry.getTimedCallbackCount();
            const uint64_t xruns = telemetry.getXruns();
            const uint64_t underruns = telemetry.getUnderruns();
            const uint64_t overruns = telemetry.getOverruns();
            const uint64_t queueDrops = m_engine->commandQueue().droppedCount();
            const uint64_t rtAllocations = telemetry.getRtAllocationViolations();
            const uint64_t rtLocks = telemetry.getRtLockViolations();
            const uint64_t rtLogs = telemetry.getRtLogViolations();
            const uint64_t nanSamples = m_engine->getNaNCount();
            const uint64_t clippedSamples = m_engine->getClipCount();
            const bool recoveryActive = telemetry.isInRecoveryMode();
            const int32_t linuxPriorityErrno = telemetry.getLinuxRtPriorityErrno();

            JSON issues = JSON::array();
            const auto addIssue = [&](bool present, const char* code) {
                if (present) issues.push(JSON(code));
            };
            addIssue(xruns > 0, "xruns");
            addIssue(underruns > 0, "underruns");
            addIssue(overruns > 0, "callback_deadline_overruns");
            addIssue(queueDrops > 0, "command_queue_drops");
            addIssue(rtAllocations > 0, "rt_allocation_violations");
            addIssue(rtLocks > 0, "rt_lock_violations");
            addIssue(rtLogs > 0, "rt_log_violations");
            addIssue(nanSamples > 0, "nan_samples_sanitized");
            addIssue(clippedSamples > 0, "hard_clipped_samples");
            addIssue(recoveryActive, "underrun_recovery_active");
            addIssue(timedCallbacks > 0 && !telemetry.isThreadPriorityOptimal(),
                     "realtime_priority_incomplete");
            addIssue(linuxPriorityErrno != 0, "realtime_priority_error");

            const bool observed = blocks > 0 || timedCallbacks > 0;
            const bool degraded = issues.size() > 0;

            JSON timing = JSON::object();
            timing.set("blocksProcessed", JSON(static_cast<double>(blocks)));
            timing.set("timedCallbacks", JSON(static_cast<double>(timedCallbacks)));
            timing.set("lastCallbackMs",
                       JSON(static_cast<double>(telemetry.getLastCallbackNs()) / 1.0e6));
            timing.set("averageCallbackMs",
                       JSON(static_cast<double>(telemetry.getAverageCallbackNs()) / 1.0e6));
            timing.set("maxCallbackMs",
                       JSON(static_cast<double>(telemetry.getMaxCallbackNs()) / 1.0e6));
            timing.set("callbackBudgetMs",
                       JSON(static_cast<double>(telemetry.getCallbackBudgetNs()) / 1.0e6));
            timing.set("bufferFrames", JSON(static_cast<double>(telemetry.getLastBufferFrames())));
            timing.set("sampleRate", JSON(static_cast<double>(telemetry.getLastSampleRate())));
            timing.set("deadlineOverruns", JSON(static_cast<double>(overruns)));

            JSON realtime = JSON::object();
            realtime.set("xruns", JSON(static_cast<double>(xruns)));
            realtime.set("underruns", JSON(static_cast<double>(underruns)));
            realtime.set("consecutiveUnderruns",
                         JSON(static_cast<double>(telemetry.getConsecutiveUnderruns())));
            realtime.set("recoveryActive", JSON(recoveryActive));
            realtime.set("recoveryActivations",
                         JSON(static_cast<double>(telemetry.getRecoveryModeActivations())));
            realtime.set("allocationViolations", JSON(static_cast<double>(rtAllocations)));
            realtime.set("lockViolations", JSON(static_cast<double>(rtLocks)));
            realtime.set("logViolations", JSON(static_cast<double>(rtLogs)));
            realtime.set("threadPriorityStatus",
                         JSON(static_cast<double>(telemetry.getThreadPriorityStatus())));
            realtime.set("threadPriorityOptimal", JSON(telemetry.isThreadPriorityOptimal()));
            realtime.set("linuxPriorityErrno", JSON(static_cast<double>(linuxPriorityErrno)));

            JSON commandQueue = JSON::object();
            commandQueue.set("depth",
                             JSON(static_cast<double>(m_engine->commandQueue().approxDepth())));
            commandQueue.set("maxDepth",
                             JSON(static_cast<double>(m_engine->commandQueue().maxDepth())));
            commandQueue.set("capacity", JSON(static_cast<double>(AudioCommandQueue::capacity())));
            commandQueue.set("dropped", JSON(static_cast<double>(queueDrops)));

            const uint64_t srcBlocks = telemetry.getSrcActiveBlocks();
            JSON resampling = JSON::object();
            resampling.set("activeBlocks", JSON(static_cast<double>(srcBlocks)));
            resampling.set("activePercent",
                           JSON(blocks > 0 ? 100.0 * static_cast<double>(srcBlocks) /
                                                 static_cast<double>(blocks)
                                           : 0.0));

            JSON signal = JSON::object();
            signal.set("nanSamplesSanitized", JSON(static_cast<double>(nanSamples)));
            signal.set("hardClippedSamples", JSON(static_cast<double>(clippedSamples)));

            JSON result = JSON::object();
            result.set("status", JSON(degraded ? "degraded" : observed ? "healthy" : "unobserved"));
            result.set("observed", JSON(observed));
            result.set("counterScope", JSON("engine_lifetime"));
            result.set("issues", issues);
            result.set("timing", timing);
            result.set("realtime", realtime);
            result.set("commandQueue", commandQueue);
            result.set("resampling", resampling);
            result.set("signal", signal);

            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "get_meters") {
            if (!m_trackManager) {
                return makeError(id, "execution_error", "no track manager", verb).toString();
            }
            auto meters = m_trackManager->getMeterSnapshots();
            if (!meters) {
                return makeError(id, "execution_error", "no meter buffer installed", verb)
                    .toString();
            }
            auto slotMap = m_trackManager->getChannelSlotMapShared();

            const auto linearToDb = [](float linear) {
                return linear > 0.0f ? 20.0 * std::log10(static_cast<double>(linear)) : -144.0;
            };
            const auto readoutToJson = [&](const MeterSnapshotBuffer::MeterReadout& m) {
                JSON entry = JSON::object();
                entry.set("peakDbL", JSON(linearToDb(m.peakL)));
                entry.set("peakDbR", JSON(linearToDb(m.peakR)));
                entry.set("rmsDbL", JSON(linearToDb(m.rmsL)));
                entry.set("rmsDbR", JSON(linearToDb(m.rmsR)));
                entry.set("lufs", JSON(static_cast<double>(m.lufs)));
                entry.set("clip", JSON(m.clipL || m.clipR));
                return entry;
            };

            JSON result = JSON::object();
            result.set("master",
                       readoutToJson(meters->readMeter(
                           static_cast<int>(ChannelSlotMap::MASTER_SLOT_INDEX))));
            JSON tracks = JSON::array();
            const size_t count = m_trackManager->getChannelCount();
            for (size_t i = 0; i < count; ++i) {
                const MixerChannel* channel = m_trackManager->getChannel(i);
                if (!channel || !slotMap) continue;
                const uint32_t slot = slotMap->getSlotIndex(channel->getChannelId());
                if (slot == ChannelSlotMap::INVALID_SLOT) continue;
                JSON entry = readoutToJson(meters->readMeter(static_cast<int>(slot)));
                entry.set("index", JSON(static_cast<double>(i)));
                entry.set("id", JSON(static_cast<double>(channel->getChannelId())));
                tracks.push(entry);
            }
            result.set("tracks", tracks);
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "list_samples") {
            if (!request.has("args") || !request["args"].isObject()) {
                return makeError(id, "validation_error",
                                 "list_samples requires args: {\"dir\": <path>}", verb)
                    .toString();
            }
            JSON& args = request["args"];
            for (auto& entry : args.asObject()) {
                if (entry.first != "dir") {
                    return makeError(id, "validation_error",
                                     "unknown arg for list_samples: " + entry.first, verb)
                        .toString();
                }
            }
            if (!args.has("dir") || !args["dir"].isString() || args["dir"].asString().empty()) {
                return makeError(id, "validation_error", "arg 'dir' must be a non-empty string",
                                 verb)
                    .toString();
            }

            const std::filesystem::path root(args["dir"].asString());
            std::error_code ec;
            if (!std::filesystem::is_directory(root, ec)) {
                return makeError(id, "execution_error",
                                 "not a directory: " + args["dir"].asString(), verb)
                    .toString();
            }

            constexpr size_t kMaxEntries = 500;
            constexpr int kMaxDepth = 3;   // subdirectory nesting below root
            constexpr size_t kScanBudget = 20000; // entries examined, audio or not
            bool truncated = false;
            std::vector<std::filesystem::path> found;
            size_t scanned = 0;

            // Deterministic bounded traversal: depth-first with per-directory
            // sorted entries, so a truncated listing is always the same
            // stable prefix across calls and platforms. Directory read
            // failures are errors, not silently partial results.
            std::vector<std::pair<std::filesystem::path, int>> pending;
            pending.emplace_back(root, 0);
            while (!pending.empty() && !truncated) {
                const auto [dir, depth] = pending.back();
                pending.pop_back();

                std::vector<std::filesystem::directory_entry> entries;
                std::error_code dirEc;
                std::filesystem::directory_iterator dirIt(
                    dir, std::filesystem::directory_options::skip_permission_denied, dirEc);
                const std::filesystem::directory_iterator dirEnd;
                for (; !dirEc && dirIt != dirEnd; dirIt.increment(dirEc)) {
                    entries.push_back(*dirIt);
                }
                if (dirEc) {
                    return makeError(id, "execution_error",
                                     "error reading directory " + dir.string() + ": " +
                                         dirEc.message(),
                                     verb)
                        .toString();
                }
                std::sort(entries.begin(), entries.end(),
                          [](const auto& a, const auto& b) { return a.path() < b.path(); });

                std::vector<std::filesystem::path> subdirs;
                for (const auto& entry : entries) {
                    if (++scanned > kScanBudget) {
                        truncated = true;
                        break;
                    }
                    std::error_code typeEc;
                    if (entry.is_directory(typeEc)) {
                        if (depth < kMaxDepth) subdirs.push_back(entry.path());
                        continue;
                    }
                    if (typeEc) continue;
                    std::error_code fileEc;
                    if (!entry.is_regular_file(fileEc) || fileEc) continue;
                    if (!AudioFileValidator::isValidAudioFile(entry.path().string())) continue;
                    if (found.size() >= kMaxEntries) {
                        truncated = true;
                        break;
                    }
                    found.push_back(entry.path());
                }
                // Reverse push so the stack pops subdirectories in sorted order.
                for (auto it = subdirs.rbegin(); it != subdirs.rend(); ++it) {
                    pending.emplace_back(*it, depth + 1);
                }
            }
            std::sort(found.begin(), found.end());

            JSON samples = JSON::array();
            for (const auto& path : found) {
                JSON entry = JSON::object();
                entry.set("path", JSON(path.string()));
                entry.set("name", JSON(path.filename().string()));
                std::error_code sizeEc;
                const auto size = std::filesystem::file_size(path, sizeEc);
                // Absent field = size unknown; never a fake zero.
                if (!sizeEc) entry.set("sizeBytes", JSON(static_cast<double>(size)));
                samples.push(entry);
            }
            JSON result = JSON::object();
            result.set("samples", samples);
            result.set("truncated", JSON(truncated));
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "list_plugins") {
            JSON plugins = JSON::array();
            for (const auto& info : PluginManager::getInstance().getEffectPlugins()) {
                JSON entry = JSON::object();
                entry.set("id", JSON(info.id));
                entry.set("name", JSON(info.name));
                entry.set("category", JSON(info.category));
                plugins.push(entry);
            }
            JSON result = JSON::object();
            result.set("effects", plugins);
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "get_effects") {
            if (!m_trackManager) {
                return makeError(id, "execution_error", "no track manager", verb).toString();
            }
            if (!request.has("args") || !request["args"].isObject()) {
                return makeError(id, "validation_error",
                                 "get_effects requires args: {\"track\": <index>}", verb)
                    .toString();
            }
            JSON& args = request["args"];
            for (auto& entry : args.asObject()) {
                if (entry.first != "track") {
                    return makeError(id, "validation_error",
                                     "unknown arg for get_effects: " + entry.first, verb)
                        .toString();
                }
            }
            uint64_t trackIndex = 0;
            if (!args.has("track") || !args["track"].isNumber() ||
                !numberToId(args["track"].asNumber(), trackIndex)) {
                return makeError(id, "validation_error",
                                 "arg 'track' must be a non-negative integer", verb)
                    .toString();
            }
            MixerChannel* channel = m_trackManager->getChannel(static_cast<size_t>(trackIndex));
            if (!channel) {
                return makeError(id, "execution_error",
                                 "no such track: " + std::to_string(trackIndex), verb)
                    .toString();
            }

            auto& chain = channel->getEffectChain();
            JSON slots = JSON::array();
            for (size_t slot = 0; slot < EffectChain::MAX_SLOTS; ++slot) {
                auto plugin = chain.getPlugin(slot);
                if (!plugin) continue;
                JSON entry = JSON::object();
                entry.set("slot", JSON(static_cast<double>(slot)));
                entry.set("id", JSON(plugin->getInfo().id));
                entry.set("name", JSON(plugin->getInfo().name));
                entry.set("bypassed", JSON(chain.isSlotBypassed(slot)));
                JSON params = JSON::array();
                for (const auto& param : plugin->getParameters()) {
                    JSON paramJson = JSON::object();
                    paramJson.set("id", JSON(static_cast<double>(param.id)));
                    paramJson.set("name", JSON(param.name));
                    // Plugin output is untrusted: a NaN/Inf value would break
                    // the JSON contract — fail loudly instead.
                    const float value = plugin->getParameter(param.id);
                    if (!std::isfinite(value)) {
                        return makeError(id, "execution_error",
                                         "plugin " + plugin->getInfo().name +
                                             " returned a non-finite value for parameter '" +
                                             param.name + "'",
                                         verb)
                            .toString();
                    }
                    paramJson.set("value", JSON(static_cast<double>(value)));
                    paramJson.set("display", JSON(plugin->getParameterDisplay(param.id)));
                    if (!param.unit.empty()) paramJson.set("unit", JSON(param.unit));
                    params.push(paramJson);
                }
                entry.set("params", params);
                slots.push(entry);
            }
            JSON result = JSON::object();
            result.set("effects", slots);
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "list_patterns") {
            if (!m_trackManager) {
                return makeError(id, "execution_error", "no track manager", verb).toString();
            }
            JSON patterns = JSON::array();
            for (const auto& pattern : m_trackManager->getPatternManager().getAllPatterns()) {
                if (!pattern) continue;
                JSON entry = JSON::object();
                entry.set("id", JSON(static_cast<double>(pattern->id.value)));
                entry.set("name", JSON(pattern->name));
                entry.set("lengthBeats", JSON(pattern->lengthBeats));
                const bool isMidi = pattern->isMidi();
                const bool isAudio = pattern->isAudio();
                entry.set("type", JSON(isMidi ? "midi" : (isAudio ? "audio" : "other")));
                if (isAudio) {
                    entry.set("mixerChannelId", JSON(static_cast<double>(pattern->getMixerChannelId())));
                }
                entry.set("noteCount",
                          JSON(isMidi ? static_cast<double>(
                                            std::get<MidiPayload>(pattern->payload).notes.size())
                                      : 0.0));
                patterns.push(entry);
            }
            JSON result = JSON::object();
            result.set("patterns", patterns);
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
            uint64_t patternValue = 0;
            if (!args.has("pattern") || !args["pattern"].isNumber() ||
                !numberToId(args["pattern"].asNumber(), patternValue)) {
                return makeError(id, "validation_error",
                                 "arg 'pattern' must be a non-negative integer", verb)
                    .toString();
            }

            const PatternID patternId{patternValue};
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
            result.set("type", JSON(isMidi ? "midi" : (pattern->isAudio() ? "audio" : "other")));
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
            } else if (pattern->isAudio()) {
                result.set("mixerChannelId", JSON(static_cast<double>(pattern->getMixerChannelId())));
            }
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        // ------------------------------------------------------------------
        // undo / redo — drive the same history the UI's Ctrl+Z drives.
        //
        // Every mutation verb and every batch already lands there as one step;
        // without these the surface could build that history but never walk it,
        // so the only way back from a mistake was to hand-write the inverse
        // edit. Against the live app these move the user's undo stack, which is
        // the point: agent edits and hand edits are the same edits.
        // ------------------------------------------------------------------
        if (verb == "undo" || verb == "redo") {
            if (!m_trackManager) {
                return makeError(id, "execution_error", "no track manager", verb).toString();
            }
            if (request.has("args") && request["args"].isObject() &&
                !request["args"].asObject().empty()) {
                return makeError(id, "validation_error", verb + " takes no args", verb).toString();
            }

            CommandHistory& history = m_trackManager->getCommandHistory();
            const bool undoing = (verb == "undo");

            // An empty history is a refusal, not a silent success: a caller that
            // gets ok back would reasonably believe an edit was reverted.
            if (undoing ? !history.canUndo() : !history.canRedo()) {
                return makeError(id, "execution_error",
                                 undoing ? "nothing to undo" : "nothing to redo", verb)
                    .toString();
            }
            if (!(undoing ? history.undo() : history.redo())) {
                return makeError(id, "execution_error",
                                 std::string(undoing ? "undo" : "redo") + " failed", verb)
                    .toString();
            }

            // Report the new ends of the stack so a caller can walk it without
            // guessing when to stop.
            JSON result = JSON::object();
            result.set("canUndo", JSON(history.canUndo()));
            result.set("canRedo", JSON(history.canRedo()));
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "batch") {
            if (!m_trackManager) {
                return makeError(id, "execution_error", "no track manager", verb).toString();
            }
            if (!request.has("args") || !request["args"].isObject()) {
                return makeError(id, "validation_error",
                                 "batch requires args: {\"commands\": [{\"verb\": ..., \"args\": "
                                 "...}, ...]}",
                                 verb)
                    .toString();
            }
            JSON& args = request["args"];
            for (auto& entry : args.asObject()) {
                if (entry.first != "commands") {
                    return makeError(id, "validation_error",
                                     "unknown arg for batch: " + entry.first, verb)
                        .toString();
                }
            }
            if (!args.has("commands") || !args["commands"].isArray()) {
                return makeError(id, "validation_error", "arg 'commands' must be an array", verb)
                    .toString();
            }
            JSON& commands = args["commands"];
            const size_t count = commands.size();
            constexpr size_t kMaxBatchCommands = 64;
            if (count == 0) {
                return makeError(id, "validation_error", "batch must contain at least one command",
                                 verb)
                    .toString();
            }
            if (count > kMaxBatchCommands) {
                return makeError(id, "validation_error",
                                 "batch too large: " + std::to_string(count) + " commands (max " +
                                     std::to_string(kMaxBatchCommands) + ")",
                                 verb)
                    .toString();
            }

            // All-or-nothing, stepwise: each member is validated, built, and
            // executed against the state its predecessors produced — so a
            // batch can set the pan of a track it just added. On any failure
            // the executed prefix is undone in reverse and nothing is
            // recorded. On success the whole group lands in history as one
            // already-executed CommandTransaction: a single undo step.
            CommandParser parser;
            auto transaction = std::make_shared<CommandTransaction>("Muse Batch");
            std::vector<std::shared_ptr<ICommand>> executed;
            executed.reserve(count);
            // Unwind the executed prefix in reverse and produce the final
            // error response. A rollback failure must not hide behind the
            // member error: the response then reports execution_error and
            // says the session may be inconsistent.
            const auto failBatch = [&](const char* errorStatus,
                                       const std::string& message) -> std::string {
                bool rollbackClean = true;
                for (auto it = executed.rbegin(); it != executed.rend(); ++it) {
                    try {
                        (*it)->undo();
                    } catch (...) {
                        // Keep unwinding the rest of the prefix, but remember.
                        rollbackClean = false;
                    }
                }
                if (!rollbackClean) {
                    return makeError(id, "execution_error",
                                     message +
                                         " (rollback also failed; session state may be "
                                         "inconsistent)",
                                     verb)
                        .toString();
                }
                return makeError(id, errorStatus, message, verb).toString();
            };

            for (size_t i = 0; i < count; ++i) {
                const std::string prefix = "commands[" + std::to_string(i) + "]: ";
                JSON& item = commands[i];
                if (!item.isObject() || !item.has("verb") || !item["verb"].isString()) {
                    return failBatch("validation_error",
                                     prefix + "must be an object with a string verb");
                }
                // Refuse unknown member keys by name, the way every other args
                // object here does. Accepting them silently is how a member
                // written with "flags" instead of "args" runs with NO args and
                // still reports ok — an add_track that quietly takes its default
                // name rather than the one asked for.
                for (auto& memberEntry : item.asObject()) {
                    if (memberEntry.first != "verb" && memberEntry.first != "args") {
                        return failBatch("validation_error",
                                         prefix + "unknown key: " + memberEntry.first +
                                             " (a member is {\"verb\": ..., \"args\": ...})");
                    }
                }
                const std::string subVerb = item["verb"].asString();
                if (isQueryVerb(subVerb) || isActionVerb(subVerb)) {
                    return failBatch("validation_error",
                                     prefix + "only mutation verbs are allowed in a batch");
                }

                std::unordered_map<std::string, std::string> flags;
                if (item.has("args")) {
                    if (!item["args"].isObject()) {
                        return failBatch("validation_error", prefix + "args must be an object");
                    }
                    std::string convertError;
                    if (!jsonArgsToFlags(item["args"], flags, convertError)) {
                        return failBatch("validation_error", prefix + convertError);
                    }
                }

                CommandStatus buildStatus = CommandStatus::Success;
                std::string buildMessage;
                const CommandContext ctx{m_engine, m_trackManager};
                auto built = parser.buildValidated(subVerb, flags, buildStatus, buildMessage, ctx);
                if (!built) {
                    return failBatch(statusName(buildStatus), prefix + buildMessage);
                }

                std::shared_ptr<ICommand> cmd(std::move(built));
                try {
                    cmd->execute();
                } catch (const std::exception& e) {
                    return failBatch("execution_error", prefix + e.what());
                } catch (...) {
                    return failBatch("execution_error", prefix + "command threw");
                }
                executed.push_back(cmd);
                transaction->add(cmd);
            }

            transaction->markExecuted();
            if (!m_trackManager->getCommandHistory().pushExecuted(transaction)) {
                // Executed but not recorded (e.g. a deferred transaction is
                // active on this history). "ok, undoable" would be a lie —
                // keep all-or-nothing honest by rolling the batch back.
                return failBatch("execution_error",
                                 "batch could not be recorded in history; rolled back");
            }

            JSON result = JSON::object();
            result.set("count", JSON(static_cast<double>(count)));
            JSON response = makeOk();
            response.set("result", result);
            response.set("undoable", JSON(true));
            return finish(response);
        }

        if (verb == "render_song") {
            if (!m_trackManager || !m_engine) {
                return makeError(id, "execution_error",
                                 "render_song needs a track manager and an audio engine", verb)
                    .toString();
            }
            if (!request.has("args") || !request["args"].isObject()) {
                return makeError(id, "validation_error",
                                 "render_song requires args: {\"file\": <path>}", verb)
                    .toString();
            }
            JSON& args = request["args"];
            for (auto& entry : args.asObject()) {
                if (entry.first != "file" && entry.first != "tail") {
                    return makeError(id, "validation_error",
                                     "unknown arg for render_song: " + entry.first, verb)
                        .toString();
                }
            }
            if (!args.has("file") || !args["file"].isString() || args["file"].asString().empty()) {
                return makeError(id, "validation_error", "arg 'file' must be a non-empty string",
                                 verb)
                    .toString();
            }
            double tailSeconds = 1.0;
            if (args.has("tail")) {
                if (!args["tail"].isNumber()) {
                    return makeError(id, "validation_error", "arg 'tail' must be a number", verb)
                        .toString();
                }
                tailSeconds = args["tail"].asNumber();
                if (!(tailSeconds >= 0.0 && tailSeconds <= 30.0)) {
                    return makeError(id, "validation_error", "arg 'tail' must be 0..30 seconds",
                                     verb)
                        .toString();
                }
            }

            // Channels may have been added since the engine was wired; the
            // exporter render path resolves tracks through the slot map.
            m_trackManager->buildAndShareSlotMap();
            if (auto slotMap = m_trackManager->getChannelSlotMapShared()) {
                m_engine->setChannelSlotMap(slotMap);
            }

            // The timeline render path routes tracks to master through the
            // published audio graph; in the app AestraApp::run() drains
            // rebuilds continuously, headless we drain here.
            PlaybackGraphController graphController;
            graphController.setTrackManager(m_trackManager);
            graphController.setAudioEngine(m_engine);
            graphController.requestRebuild(GraphDirtyReason::TimelineChanged);
            graphController.drainIfDirty(static_cast<double>(m_engine->getSampleRate()));

            AudioExporter::Result exportResult;
            {
                // TrackManager::play() is what schedules timeline MIDI clip
                // instances into the pattern engine — the exporter only
                // drives the engine transport. Guarantee stop + pattern-mode
                // restore on every exit.
                struct TimelineGuard {
                    TrackManager& trackManager;
                    AudioEngine& engine;
                    ~TimelineGuard() {
                        trackManager.stop();
                        std::vector<float> settle(1024, 0.0f);
                        for (int i = 0; i < 2; ++i) {
                            engine.processBlock(settle.data(), nullptr, 512, 0.0);
                            engine.performNonRealtimeMaintenance();
                        }
                    }
                } guard{*m_trackManager, *m_engine};

                // Leave any Arsenal pattern-mode state behind: TrackManager's
                // play() only schedules timeline MIDI clip instances when its
                // own pattern-mode flag is clear.
                m_trackManager->stopArsenalPlayback(false);
                m_engine->setPatternPlaybackMode(false, 4.0);
                m_trackManager->setPosition(0.0);
                m_trackManager->play();

                AudioExporter exporter(*m_engine, *m_trackManager);
                AudioExporter::Config config;
                config.outputPath = args["file"].asString();
                config.scope = AudioExporter::RenderScope::FullSong;
                config.sampleRate = m_engine->getSampleRate();
                config.bitDepth = AudioExporter::BitDepth::Float_32;
                config.numChannels = 2;
                config.tailSeconds = tailSeconds;
                exportResult = exporter.render(config);
            }

            if (!exportResult.success) {
                return makeError(id, "execution_error", exportResult.errorMessage, verb)
                    .toString();
            }

            JSON result = JSON::object();
            result.set("file", JSON(exportResult.outputPath));
            result.set("durationSeconds", JSON(exportResult.durationSeconds));
            result.set("frames", JSON(static_cast<double>(exportResult.framesRendered)));
            result.set("sampleRate", JSON(static_cast<double>(m_engine->getSampleRate())));
            result.set("peakDb", JSON(exportResult.peakDb));
            JSON response = makeOk();
            response.set("result", result);
            return finish(response);
        }

        if (verb == "render_pattern") {
            if (!m_trackManager || !m_engine) {
                return makeError(id, "execution_error",
                                 "render_pattern needs a track manager and an audio engine", verb)
                    .toString();
            }
            if (!request.has("args") || !request["args"].isObject()) {
                return makeError(
                           id, "validation_error",
                           "render_pattern requires args: {\"pattern\": <id>, \"file\": <path>}",
                           verb)
                    .toString();
            }
            JSON& args = request["args"];
            for (auto& entry : args.asObject()) {
                if (entry.first != "pattern" && entry.first != "file" && entry.first != "tail") {
                    return makeError(id, "validation_error",
                                     "unknown arg for render_pattern: " + entry.first, verb)
                        .toString();
                }
            }
            uint64_t patternValue = 0;
            if (!args.has("pattern") || !args["pattern"].isNumber() ||
                !numberToId(args["pattern"].asNumber(), patternValue)) {
                return makeError(id, "validation_error",
                                 "arg 'pattern' must be a non-negative integer", verb)
                    .toString();
            }
            if (!args.has("file") || !args["file"].isString() || args["file"].asString().empty()) {
                return makeError(id, "validation_error", "arg 'file' must be a non-empty string",
                                 verb)
                    .toString();
            }
            double tailSeconds = 1.0;
            if (args.has("tail")) {
                if (!args["tail"].isNumber()) {
                    return makeError(id, "validation_error", "arg 'tail' must be a number", verb)
                        .toString();
                }
                tailSeconds = args["tail"].asNumber();
                if (!(tailSeconds >= 0.0 && tailSeconds <= 30.0)) {
                    return makeError(id, "validation_error", "arg 'tail' must be 0..30 seconds",
                                     verb)
                        .toString();
                }
            }

            const PatternID patternId{static_cast<uint64_t>(args["pattern"].asNumber())};
            const PatternSource* pattern =
                m_trackManager->getPatternManager().getPattern(patternId);
            if (!pattern) {
                return makeError(id, "execution_error",
                                 "no such pattern: " + std::to_string(patternId.value), verb)
                    .toString();
            }
            if (!pattern->isMidi()) {
                return makeError(id, "execution_error", "pattern is not a MIDI pattern", verb)
                    .toString();
            }

            const double bpm = std::max(1.0, static_cast<double>(m_engine->getBPM()));
            // playPatternInArsenal resolves pattern length to at least 8 beats.
            const double lengthBeats = std::max(8.0, pattern->lengthBeats);
            const double durationSeconds = lengthBeats * 60.0 / bpm + tailSeconds;
            const uint32_t sampleRate = m_engine->getSampleRate();
            const uint64_t totalFrames =
                static_cast<uint64_t>(durationSeconds * static_cast<double>(sampleRate));
            constexpr uint32_t kBlockFrames = 512;

            // Bound the render before touching engine state: the whole take
            // is buffered in memory and the RIFF format caps a WAV at 4 GiB.
            constexpr double kMaxRenderSeconds = 600.0;
            const uint64_t dataBytes = totalFrames * 2ull * sizeof(float);
            if (durationSeconds > kMaxRenderSeconds ||
                36ull + dataBytes > 0xFFFFFFFFull) {
                return makeError(id, "validation_error",
                                 "render too long: " + std::to_string(durationSeconds) +
                                     "s (max " + std::to_string(kMaxRenderSeconds) + "s)",
                                 verb)
                    .toString();
            }

            // Pattern playback now shares the mixer graph with Timeline playback.
            // Headless callers do not have the app loop to publish recent unit or
            // channel changes, so prepare the current graph before pumping audio.
            m_trackManager->buildAndShareSlotMap();
            if (auto slotMap = m_trackManager->getChannelSlotMapShared()) {
                m_engine->setChannelSlotMap(slotMap);
            }
            PlaybackGraphController graphController;
            graphController.setTrackManager(m_trackManager);
            graphController.setAudioEngine(m_engine);
            graphController.requestRebuild(GraphDirtyReason::TimelineChanged);
            graphController.drainIfDirty(static_cast<double>(sampleRate));

            std::vector<float> rendered;
            rendered.reserve(static_cast<size_t>(totalFrames) * 2u);
            std::vector<float> block(static_cast<size_t>(kBlockFrames) * 2u, 0.0f);
            float peak = 0.0f;
            bool nonFinite = false;

            {
                // Everything after playback starts must be undone even if the
                // pump throws: stop the transport, drain the stop command with
                // settle blocks, and leave pattern mode (app default length).
                struct TransportGuard {
                    AudioEngine& engine;
                    TrackManager& trackManager;
                    std::vector<float>& block;
                    ~TransportGuard() {
                        // Full Arsenal teardown: stop, flush the scheduled
                        // instance, and leave pattern mode — a lingering
                        // pattern-mode flag or instance would bleed into the
                        // next timeline play/render.
                        trackManager.stopArsenalPlayback(false);
                        for (int i = 0; i < 2; ++i) {
                            std::memset(block.data(), 0, block.size() * sizeof(float));
                            engine.processBlock(block.data(), nullptr, kBlockFrames, 0.0);
                            engine.performNonRealtimeMaintenance();
                        }
                        engine.setPatternPlaybackMode(false, 4.0);
                    }
                } guard{*m_engine, *m_trackManager, block};

                // Offline bounce through the exact live engine path, the same
                // way the headless test renderer pumps it. Arsenal preview
                // routing is what the user hears when a pattern plays, so it
                // is what the agent gets back. Pattern playback needs both
                // sides armed: the scheduler (playPatternInArsenal) and the
                // engine's pattern mode (the app sets it wherever it starts
                // pattern playback).
                m_engine->setPatternPlaybackMode(true, lengthBeats);
                m_trackManager->playPatternInArsenal(patternId, 0.0);

                uint64_t framesRemaining = totalFrames;
                while (framesRemaining > 0 && !nonFinite) {
                    const uint32_t framesThisBlock =
                        static_cast<uint32_t>(std::min<uint64_t>(kBlockFrames, framesRemaining));
                    std::memset(block.data(), 0, block.size() * sizeof(float));
                    m_engine->processBlock(block.data(), nullptr, framesThisBlock, 0.0);
                    // The pattern scheduler's RT queue is refilled from the
                    // control thread; offline that's us (AudioExporter does
                    // the same per block).
                    m_engine->performNonRealtimeMaintenance();
                    for (uint32_t i = 0; i < framesThisBlock * 2u; ++i) {
                        // Plugin output is untrusted: NaN/Inf must fail the
                        // render, not land in the file as "ok".
                        if (!std::isfinite(block[i])) {
                            nonFinite = true;
                            break;
                        }
                        peak = std::max(peak, std::abs(block[i]));
                    }
                    if (nonFinite) break;
                    rendered.insert(rendered.end(), block.begin(),
                                    block.begin() + static_cast<size_t>(framesThisBlock) * 2u);
                    framesRemaining -= framesThisBlock;
                }
            }

            if (nonFinite) {
                return makeError(id, "execution_error",
                                 "engine produced non-finite audio; render aborted", verb)
                    .toString();
            }

            if (!writeFloat32Wav(args["file"].asString(), rendered, sampleRate)) {
                return makeError(id, "execution_error",
                                 "cannot write output file: " + args["file"].asString(), verb)
                    .toString();
            }

            const double peakDb = peak > 0.0f ? 20.0 * std::log10(static_cast<double>(peak))
                                              : -144.0;
            JSON result = JSON::object();
            result.set("file", JSON(args["file"].asString()));
            result.set("durationSeconds", JSON(durationSeconds));
            result.set("frames", JSON(static_cast<double>(totalFrames)));
            result.set("sampleRate", JSON(static_cast<double>(sampleRate)));
            result.set("peakDb", JSON(peakDb));
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
