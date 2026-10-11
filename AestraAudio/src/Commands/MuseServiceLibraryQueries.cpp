// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Library reads: meters, samples, plugins, effects, patterns.
//

//
// Part of the split that took MuseService.cpp from one 2200-line dispatch
// chain to one file per family. See Commands/MuseServiceInternal.h for why, and
// for the contract these handlers share.
//
// The body below is the text it was before the move, dedented by four spaces
// and otherwise untouched. If you change a verb's behaviour, change it here.

#include "Commands/MuseServiceInternal.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "IO/AudioFileValidator.h"
#include "Plugin/PluginManager.h"

#include "AestraJSON.h"
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

// Inside Aestra::Audio, as it was in MuseService.cpp: JSON is Aestra::JSON, so
// these helpers cannot sit at global scope and still name it unqualified.

namespace MuseInternal {

std::optional<std::string> handleLibraryVerbs(const RequestContext& ctx, const ResponseEnvelope& env) {
    // Bound to the names the moved bodies already use, so no verb body had to be
    // edited to make the move.
    const double id = ctx.id;
    const std::string& verb = ctx.verb;
    JSON& request = *ctx.request;
    TrackManager* const m_trackManager = ctx.trackManager;
    AudioEngine* const m_engine = ctx.engine;
    // A reference, not a pointer: get_project_load_report tests this for a value
    // and then dereferences it, so it has to carry std::optional's own semantics.
    // A pointer would be truthy whenever the address is non-null — including when
    // the optional it points at is empty — which is not the same question.
    const std::optional<JSON>& m_projectLoadReport = *ctx.projectLoadReport;

    const auto makeOk = [&env]() { return env.ok(); };
    const auto finish = [&env](JSON& response) { return env.finish(response); };

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
        for (size_t slot = 0, slotTotal = chain.slotCount(); slot < slotTotal; ++slot) {
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
    // No verb matched. In the single-file version this fell out of the if-chain
    // into the next family's checks and eventually the mutation path, which is
    // exactly what nullopt asks handleRequest to do.
    return std::nullopt;
}

} // namespace MuseInternal
} // namespace Audio
} // namespace Aestra
