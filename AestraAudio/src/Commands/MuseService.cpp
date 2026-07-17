// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/MuseService.h"

#include "Commands/CommandParser.h"
#include "Commands/CommandResult.h"
#include "Commands/CommandTransaction.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
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
            tm->onTransportStateApplied(cmd.value1 != 0.0f,
                                        static_cast<double>(cmd.samplePos) / sampleRate);
        }
    });
}

namespace {

// Query verbs MuseService answers directly (mutations live in MuseGrammar).
bool isQueryVerb(const std::string& verb) {
    return verb == "get_transport" || verb == "list_tracks" || verb == "list_clips" ||
           verb == "get_session_state" || verb == "list_units" || verb == "get_pattern";
}

// Service actions: handled here like queries, but they do work (render a
// file, run a batch) rather than read state. render_pattern is not routed
// through CommandHistory — a bounce is not an undoable project edit; batch
// pushes one CommandTransaction so the whole group is a single undo step.
bool isActionVerb(const std::string& verb) {
    return verb == "render_pattern" || verb == "batch";
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
            const auto rollback = [&executed]() {
                for (auto it = executed.rbegin(); it != executed.rend(); ++it) {
                    try {
                        (*it)->undo();
                    } catch (...) {
                        // Best effort: keep unwinding the rest of the prefix.
                    }
                }
            };

            for (size_t i = 0; i < count; ++i) {
                const std::string prefix = "commands[" + std::to_string(i) + "]: ";
                JSON& item = commands[i];
                if (!item.isObject() || !item.has("verb") || !item["verb"].isString()) {
                    rollback();
                    return makeError(id, "validation_error",
                                     prefix + "must be an object with a string verb", verb)
                        .toString();
                }
                const std::string subVerb = item["verb"].asString();
                if (isQueryVerb(subVerb) || isActionVerb(subVerb)) {
                    rollback();
                    return makeError(id, "validation_error",
                                     prefix + "only mutation verbs are allowed in a batch", verb)
                        .toString();
                }

                std::unordered_map<std::string, std::string> flags;
                if (item.has("args")) {
                    if (!item["args"].isObject()) {
                        rollback();
                        return makeError(id, "validation_error", prefix + "args must be an object",
                                         verb)
                            .toString();
                    }
                    std::string convertError;
                    if (!jsonArgsToFlags(item["args"], flags, convertError)) {
                        rollback();
                        return makeError(id, "validation_error", prefix + convertError, verb)
                            .toString();
                    }
                }

                CommandStatus buildStatus = CommandStatus::Success;
                std::string buildMessage;
                auto built = parser.buildValidated(subVerb, flags, buildStatus, buildMessage);
                if (!built) {
                    rollback();
                    return makeError(id, statusName(buildStatus), prefix + buildMessage, verb)
                        .toString();
                }

                std::shared_ptr<ICommand> cmd(std::move(built));
                try {
                    cmd->execute();
                } catch (const std::exception& e) {
                    rollback();
                    return makeError(id, "execution_error", prefix + e.what(), verb).toString();
                } catch (...) {
                    rollback();
                    return makeError(id, "execution_error", prefix + "command threw", verb)
                        .toString();
                }
                executed.push_back(cmd);
                transaction->add(cmd);
            }

            transaction->markExecuted();
            m_trackManager->getCommandHistory().pushExecuted(transaction);

            JSON result = JSON::object();
            result.set("count", JSON(static_cast<double>(count)));
            JSON response = makeOk();
            response.set("result", result);
            response.set("undoable", JSON(true));
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
                        trackManager.stop();
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
