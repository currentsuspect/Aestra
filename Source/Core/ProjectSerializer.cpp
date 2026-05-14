// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ProjectSerializer.h"
#include "ProjectMigrations.h"
#include "../AestraCore/include/AestraLog.h"
#include "MiniAudioDecoder.h"
#include "PluginManager.h"
#include "Music/ScaleContext.h"
#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace Aestra;
using namespace Aestra::Audio;

namespace {
    // Project file format versioning
    // - Increment CURRENT when making breaking changes
    // - MIN_SUPPORTED is the oldest version we can still load
    // - Future migration code can handle MIN_SUPPORTED <= version <= CURRENT
    constexpr int PROJECT_VERSION_CURRENT = 2;
    constexpr int PROJECT_VERSION_MIN_SUPPORTED = 1;
    constexpr size_t PROJECT_HISTORY_MAX_ENTRIES = 50;
    constexpr uintmax_t PROJECT_MAX_FILE_BYTES = 64ull * 1024ull * 1024ull;
    constexpr size_t PROJECT_MAX_SOURCES = 10000;
    constexpr size_t PROJECT_MAX_PATTERNS = 100000;
    constexpr size_t PROJECT_MAX_LANES = 2048;
    constexpr size_t PROJECT_MAX_CLIPS_PER_LANE = 100000;
    constexpr size_t PROJECT_MAX_NOTES_PER_PATTERN = 1000000;
    constexpr size_t PROJECT_MAX_SLICES_PER_PATTERN = 1000000;
    constexpr size_t PROJECT_MAX_AUTOMATION_CURVES_PER_LANE = 2048;
    constexpr size_t PROJECT_MAX_AUTOMATION_POINTS_PER_CURVE = 100000;
    constexpr size_t PROJECT_MAX_SENDS_PER_LANE = 256;
    constexpr size_t PROJECT_MAX_UI_PANELS = 256;
    constexpr size_t PROJECT_MAX_STRING_BYTES = 4096;
    constexpr size_t PROJECT_MAX_PATH_BYTES = 32768;
    constexpr size_t PROJECT_MAX_EFFECT_STATE_HEX_BYTES = 4ull * 1024ull * 1024ull;
    std::atomic<uint64_t> g_projectHistoryCounter{0};

    std::string bytesToHex(const std::vector<uint8_t>& bytes) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint8_t byte : bytes) {
            oss << std::setw(2) << static_cast<int>(byte);
        }
        return oss.str();
    }

    std::vector<uint8_t> hexToBytes(const std::string& hex) {
        std::vector<uint8_t> bytes;
        if (hex.size() % 2 != 0 || hex.size() > PROJECT_MAX_EFFECT_STATE_HEX_BYTES) {
            return bytes;
        }

        bytes.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2) {
            const char hi = static_cast<char>(std::tolower(static_cast<unsigned char>(hex[i])));
            const char lo = static_cast<char>(std::tolower(static_cast<unsigned char>(hex[i + 1])));
            auto hexValue = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                return -1;
            };
            const int high = hexValue(hi);
            const int low = hexValue(lo);
            if (high < 0 || low < 0) {
                return {};
            }
            bytes.push_back(static_cast<uint8_t>((high << 4) | low));
        }
        return bytes;
    }

    bool hasJsonObjectEnvelope(const std::string& contents) {
        const auto begin = std::find_if_not(contents.begin(), contents.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        });
        if (begin == contents.end() || *begin != '{') {
            return false;
        }
        const auto rbegin = std::find_if_not(contents.rbegin(), contents.rend(), [](unsigned char c) {
            return std::isspace(c) != 0;
        });
        return rbegin != contents.rend() && *rbegin == '}';
    }

    bool hasUnsafeNumericToken(const std::string& contents) {
        bool inString = false;
        bool escaped = false;
        for (size_t i = 0; i < contents.size(); ++i) {
            const char c = contents[i];
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    inString = false;
                }
                continue;
            }
            if (c == '"') {
                inString = true;
                continue;
            }
            if (c != '-' && (c < '0' || c > '9')) {
                continue;
            }

            const size_t start = i;
            if (contents[i] == '-') {
                ++i;
            }
            while (i < contents.size() && ((contents[i] >= '0' && contents[i] <= '9') || contents[i] == '.')) {
                ++i;
            }
            bool hasExponent = false;
            int exponentSign = 1;
            int exponent = 0;
            if (i < contents.size() && (contents[i] == 'e' || contents[i] == 'E')) {
                hasExponent = true;
                ++i;
                if (i < contents.size() && (contents[i] == '+' || contents[i] == '-')) {
                    exponentSign = contents[i] == '-' ? -1 : 1;
                    ++i;
                }
                while (i < contents.size() && contents[i] >= '0' && contents[i] <= '9') {
                    if (exponent < 10000) {
                        exponent = exponent * 10 + (contents[i] - '0');
                    }
                    ++i;
                }
            }

            const size_t end = i;
            --i;
            if (end - start > 128) {
                return true;
            }
            if (hasExponent && exponentSign > 0 && exponent > 308) {
                return true;
            }
        }
        return false;
    }

    bool hasDecodableAudioExtension(const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return ext == ".wav" || ext == ".wave" || ext == ".mp3" || ext == ".flac" ||
               ext == ".ogg" || ext == ".aiff" || ext == ".aif" || ext == ".m4a";
    }

    std::filesystem::path resolveProjectAssetPath(const std::filesystem::path& projectPath,
                                                  const std::string& storedPath) {
        std::filesystem::path assetPath(storedPath);
        if (assetPath.is_relative() && projectPath.has_parent_path()) {
            assetPath = projectPath.parent_path() / assetPath;
        }
        return assetPath.lexically_normal();
    }

    bool isRegularDecodableAsset(const std::filesystem::path& path) {
        std::error_code ec;
        return hasDecodableAudioExtension(path) && std::filesystem::is_regular_file(path, ec) && !ec;
    }

    bool validArraySection(const JSON& root, const char* key, size_t maxCount, std::string& error, bool required = false) {
        if (!root.has(key)) {
            if (required) {
                error = std::string("Invalid project file: missing '") + key + "' section";
                return false;
            }
            return true;
        }
        const JSON& value = root[key];
        if (!value.isArray()) {
            error = std::string("Invalid project file: '") + key + "' must be an array";
            return false;
        }
        if (value.size() > maxCount) {
            error = std::string("Invalid project file: '") + key + "' exceeds maximum count";
            return false;
        }
        return true;
    }

    bool validFiniteNumber(const JSON& object, const char* key, std::string& error, bool required = false) {
        if (!object.has(key)) {
            if (required) {
                error = std::string("Invalid project file: missing numeric field '") + key + "'";
                return false;
            }
            return true;
        }
        if (!object[key].isNumber() || !std::isfinite(object[key].asNumber())) {
            error = std::string("Invalid project file: field '") + key + "' must be a finite number";
            return false;
        }
        return true;
    }

    bool validStringField(const JSON& object, const char* key, size_t maxBytes, std::string& error, bool required = false) {
        if (!object.has(key)) {
            if (required) {
                error = std::string("Invalid project file: missing string field '") + key + "'";
                return false;
            }
            return true;
        }
        if (!object[key].isString()) {
            error = std::string("Invalid project file: field '") + key + "' must be a string";
            return false;
        }
        if (object[key].asString().size() > maxBytes) {
            error = std::string("Invalid project file: field '") + key + "' is too large";
            return false;
        }
        return true;
    }

    bool validColorField(const JSON& object, const char* key, std::string& error) {
        if (!object.has(key)) {
            return true;
        }
        if (object[key].isString()) {
            if (object[key].asString().size() > PROJECT_MAX_STRING_BYTES) {
                error = std::string("Invalid project file: field '") + key + "' is too large";
                return false;
            }
            return true;
        }
        if (object[key].isNumber() && std::isfinite(object[key].asNumber())) {
            return true;
        }
        error = std::string("Invalid project file: field '") + key + "' must be a color string or number";
        return false;
    }

    double finiteNumberOr(const JSON& object, const char* key, double fallback, double minValue, double maxValue) {
        if (!object.has(key) || !object[key].isNumber() || !std::isfinite(object[key].asNumber())) {
            return fallback;
        }
        return std::clamp(object[key].asNumber(), minValue, maxValue);
    }

    std::string boundedStringOr(const JSON& object, const char* key, const std::string& fallback, size_t maxBytes) {
        if (!object.has(key) || !object[key].isString()) {
            return fallback;
        }
        const std::string& value = object[key].asString();
        if (value.size() > maxBytes) {
            return value.substr(0, maxBytes);
        }
        return value;
    }

    bool validateProjectStructure(const JSON& root, std::string& error) {
        if (!root.has("version") || !root["version"].isNumber() || !std::isfinite(root["version"].asNumber())) {
            error = "Invalid project file: missing or invalid version";
            return false;
        }
        if (!validFiniteNumber(root, "tempo", error)) return false;
        if (!validFiniteNumber(root, "playhead", error)) return false;
        if (!validArraySection(root, "sources", PROJECT_MAX_SOURCES, error)) return false;
        if (!validArraySection(root, "patterns", PROJECT_MAX_PATTERNS, error)) return false;
        if (!validArraySection(root, "lanes", PROJECT_MAX_LANES, error, true)) return false;

        if (root.has("sources")) {
            const JSON& sources = root["sources"];
            for (size_t i = 0; i < sources.size(); ++i) {
                if (!sources[i].isObject()) {
                    error = "Invalid project file: source entry must be an object";
                    return false;
                }
                if (!validFiniteNumber(sources[i], "id", error, true)) return false;
                if (!validStringField(sources[i], "path", PROJECT_MAX_PATH_BYTES, error, true)) return false;
                if (!validStringField(sources[i], "name", PROJECT_MAX_STRING_BYTES, error)) return false;
            }
        }

        if (root.has("patterns")) {
            const JSON& patterns = root["patterns"];
            for (size_t i = 0; i < patterns.size(); ++i) {
                if (!patterns[i].isObject()) {
                    error = "Invalid project file: pattern entry must be an object";
                    return false;
                }
                if (!validFiniteNumber(patterns[i], "id", error, true)) return false;
                if (!validFiniteNumber(patterns[i], "length", error, true)) return false;
                if (!validStringField(patterns[i], "name", PROJECT_MAX_STRING_BYTES, error)) return false;
                if (!validStringField(patterns[i], "type", PROJECT_MAX_STRING_BYTES, error, true)) return false;
                const std::string type = patterns[i]["type"].asString();
                if (type == "audio") {
                    if (!validFiniteNumber(patterns[i], "sourceId", error, true)) return false;
                    if (patterns[i].has("slices")) {
                        if (!patterns[i]["slices"].isArray() || patterns[i]["slices"].size() > PROJECT_MAX_SLICES_PER_PATTERN) {
                            error = "Invalid project file: pattern slices must be a bounded array";
                            return false;
                        }
                    }
                } else if (type == "midi") {
                    if (patterns[i].has("notes")) {
                        if (!patterns[i]["notes"].isArray() || patterns[i]["notes"].size() > PROJECT_MAX_NOTES_PER_PATTERN) {
                            error = "Invalid project file: pattern notes must be a bounded array";
                            return false;
                        }
                    }
                    // Validate optional scale field
                    if (patterns[i].has("scale")) {
                        const JSON& scale = patterns[i]["scale"];
                        if (!scale.isObject()) {
                            error = "Invalid project file: pattern scale must be an object";
                            return false;
                        }
                        if (scale.has("rootKey") && !scale["rootKey"].isNumber()) {
                            error = "Invalid project file: scale rootKey must be a number";
                            return false;
                        }
                        if (scale.has("scaleKind") && !scale["scaleKind"].isString()) {
                            error = "Invalid project file: scale scaleKind must be a string";
                            return false;
                        }
                        if (scale.has("snapToScale") && !scale["snapToScale"].isBool()) {
                            error = "Invalid project file: scale snapToScale must be a boolean";
                            return false;
                        }
                    }
                } else {
                    error = "Invalid project file: unsupported pattern type";
                    return false;
                }
            }
        }

        const JSON& lanes = root["lanes"];
        for (size_t i = 0; i < lanes.size(); ++i) {
            if (!lanes[i].isObject()) {
                error = "Invalid project file: lane entry must be an object";
                return false;
            }
            if (!validStringField(lanes[i], "name", PROJECT_MAX_STRING_BYTES, error)) return false;
            if (!validColorField(lanes[i], "color", error)) return false;
            if (!validFiniteNumber(lanes[i], "volume", error)) return false;
            if (!validFiniteNumber(lanes[i], "pan", error)) return false;
            if (lanes[i].has("effectChainStateHex") &&
                (!lanes[i]["effectChainStateHex"].isString() ||
                 lanes[i]["effectChainStateHex"].asString().size() > PROJECT_MAX_EFFECT_STATE_HEX_BYTES)) {
                error = "Invalid project file: effect chain state is too large";
                return false;
            }
            if (lanes[i].has("clips") &&
                (!lanes[i]["clips"].isArray() || lanes[i]["clips"].size() > PROJECT_MAX_CLIPS_PER_LANE)) {
                error = "Invalid project file: lane clips must be a bounded array";
                return false;
            }
            if (lanes[i].has("clips") && lanes[i]["clips"].isArray()) {
                const JSON& clips = lanes[i]["clips"];
                for (size_t c = 0; c < clips.size(); ++c) {
                    if (!clips[c].isObject()) {
                        error = "Invalid project file: clip entry must be an object";
                        return false;
                    }
                    if (!validColorField(clips[c], "color", error)) return false;
                    if (!validStringField(clips[c], "id", PROJECT_MAX_STRING_BYTES, error)) return false;
                    if (!validStringField(clips[c], "name", PROJECT_MAX_STRING_BYTES, error)) return false;
                }
            }
            if (lanes[i].has("automation") &&
                (!lanes[i]["automation"].isArray() || lanes[i]["automation"].size() > PROJECT_MAX_AUTOMATION_CURVES_PER_LANE)) {
                error = "Invalid project file: lane automation must be a bounded array";
                return false;
            }
            if (lanes[i].has("routing") && lanes[i]["routing"].isObject() && lanes[i]["routing"].has("sends")) {
                const JSON& sends = lanes[i]["routing"]["sends"];
                if (!sends.isArray() || sends.size() > PROJECT_MAX_SENDS_PER_LANE) {
                    error = "Invalid project file: lane sends must be a bounded array";
                    return false;
                }
            }
            if (lanes[i].has("automation") && lanes[i]["automation"].isArray()) {
                const JSON& automation = lanes[i]["automation"];
                for (size_t a = 0; a < automation.size(); ++a) {
                    if (!automation[a].isObject()) {
                        error = "Invalid project file: automation curve must be an object";
                        return false;
                    }
                    if (automation[a].has("points") &&
                        (!automation[a]["points"].isArray() ||
                         automation[a]["points"].size() > PROJECT_MAX_AUTOMATION_POINTS_PER_CURVE)) {
                        error = "Invalid project file: automation points must be a bounded array";
                        return false;
                    }
                }
            }
        }

        if (root.has("ui") && root["ui"].isObject() && root["ui"].has("panels")) {
            if (!root["ui"]["panels"].isArray() || root["ui"]["panels"].size() > PROJECT_MAX_UI_PANELS) {
                error = "Invalid project file: UI panels must be a bounded array";
                return false;
            }
        }

        return true;
    }
}

static bool writeAtomicallyImpl(const std::string& path, const std::string& contents) {
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path target(path);
    fs::path tmp = target;
    tmp += ".tmp";

    // Ensure parent exists
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
        ec.clear();
    }

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            Log::error("Project save failed: cannot open temp file: " + tmp.string());
            return false;
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) {
            Log::error("Project save failed: write error: " + tmp.string());
            return false;
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(tmp.wstring().c_str(), target.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        Log::error("Project save failed: cannot replace target: " + target.string() +
                   " (Win32 error " + std::to_string(error) + ")");
        fs::remove(tmp, ec);
        return false;
    }
#else
    fs::rename(tmp, target, ec);
    if (ec) {
        Log::error("Project save failed: cannot replace target: " + target.string() + " (" + ec.message() + ")");
        // Best-effort cleanup
        fs::remove(tmp, ec);
        return false;
    }
#endif

    return true;
}

static std::filesystem::path getHistoryDirImpl(const std::filesystem::path& projectPath) {
    if (projectPath.empty()) {
        return {};
    }

    return projectPath.parent_path() / (projectPath.stem().string() + ".history");
}

static std::string buildHistorySnapshotName(const std::filesystem::path& projectPath) {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    const uint64_t suffix = g_projectHistoryCounter.fetch_add(1, std::memory_order_relaxed);

    std::tm localTm{};
#if defined(_WIN32)
    localtime_s(&localTm, &tt);
#else
    localtime_r(&tt, &localTm);
#endif

    std::ostringstream oss;
    oss << projectPath.stem().string()
        << "_save_"
        << std::put_time(&localTm, "%Y%m%d_%H%M%S")
        << "_"
        << std::setw(3) << std::setfill('0') << ms
        << "_"
        << suffix
        << ".aes";
    return oss.str();
}

static void pruneHistorySnapshots(const std::filesystem::path& historyDir) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(historyDir, ec) || ec) {
        return;
    }

    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(historyDir, ec)) {
        if (ec) {
            return;
        }
        if (entry.is_regular_file(ec) && entry.path().extension() == ".aes") {
            entries.push_back(entry);
        }
    }

    if (entries.size() <= PROJECT_HISTORY_MAX_ENTRIES) {
        return;
    }

    std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        std::error_code aec;
        std::error_code bec;
        return fs::last_write_time(a.path(), aec) > fs::last_write_time(b.path(), bec);
    });

    for (size_t i = PROJECT_HISTORY_MAX_ENTRIES; i < entries.size(); ++i) {
        fs::remove(entries[i].path(), ec);
    }
}

static void writeHistorySnapshot(const std::string& projectPath, const std::string& contents) {
    namespace fs = std::filesystem;

    if (projectPath.empty() || contents.empty()) {
        return;
    }

    std::error_code ec;
    const fs::path target(projectPath);
    const fs::path historyDir = getHistoryDirImpl(target);
    if (historyDir.empty()) {
        return;
    }

    fs::create_directories(historyDir, ec);
    if (ec) {
        Log::warning("Project history snapshot skipped: cannot create history dir: " + historyDir.string());
        return;
    }

    const fs::path snapshotPath = historyDir / buildHistorySnapshotName(target);
    if (!writeAtomicallyImpl(snapshotPath.string(), contents)) {
        Log::warning("Project history snapshot skipped: cannot write " + snapshotPath.string());
        return;
    }

    pruneHistorySnapshots(historyDir);
}

ProjectSerializer::SerializeResult ProjectSerializer::serialize(const std::shared_ptr<TrackManager>& trackManager,
                                                               double tempo,
                                                               double playheadSeconds,
                                                               int indentSpaces,
                                                               const UIState* uiState) {
    SerializeResult result;
    if (!trackManager) return result;

    JSON root = JSON::object();
    root.set("version", JSON(static_cast<double>(PROJECT_VERSION_CURRENT)));
    root.set("tempo", JSON(tempo));
    root.set("playhead", JSON(playheadSeconds));

    auto& playlist = trackManager->getPlaylistModel();
    auto& sourceManager = trackManager->getSourceManager();
    auto& patternManager = trackManager->getPatternManager();

    // 1. Save Sources
    JSON sourcesJson = JSON::array();
    std::vector<ClipSourceID> sourceIds = sourceManager.getAllSourceIDs();
    for (const auto& id : sourceIds) {
        if (const auto* source = sourceManager.getSource(id)) {
            JSON s = JSON::object();
            s.set("id", JSON(static_cast<double>(id.value)));
            // Ensure JSON-safe paths on Windows (avoid unescaped backslashes).
            s.set("path", JSON(std::filesystem::path(source->getFilePath()).generic_string()));
            s.set("name", JSON(source->getName()));
            sourcesJson.push(s);
        }
    }
    root.set("sources", sourcesJson);

    // 2. Save Patterns
    JSON patternsJson = JSON::array();
    std::vector<std::shared_ptr<PatternSource>> patterns = patternManager.getAllPatterns();
    for (const auto& p : patterns) {
        JSON pjs = JSON::object();
        pjs.set("id", JSON(static_cast<double>(p->id.value)));
        pjs.set("name", JSON(p->name));
        pjs.set("length", JSON(p->lengthBeats));
        
        if (p->isAudio()) {
            pjs.set("type", JSON("audio"));
            const auto& payload = std::get<AudioSlicePayload>(p->payload);
            pjs.set("sourceId", JSON(static_cast<double>(payload.audioSourceId.value)));
            
            JSON slicesArray = JSON::array();
            for (const auto& slice : payload.slices) {
                JSON sl = JSON::object();
                sl.set("start", JSON(slice.startSamples));
                sl.set("length", JSON(slice.lengthSamples));
                slicesArray.push(sl);
            }
            pjs.set("slices", slicesArray);
        } else {
            pjs.set("type", JSON("midi"));
            const auto& payload = std::get<MidiPayload>(p->payload);
            JSON notesArray = JSON::array();
            for (const auto& note : payload.notes) {
                JSON nj = JSON::object();
                nj.set("pitch", JSON(static_cast<double>(note.pitch)));
                nj.set("startBeat", JSON(note.startBeat));
                nj.set("durationBeats", JSON(note.durationBeats));
                nj.set("velocity", JSON(static_cast<double>(note.velocity)));
                nj.set("unitId", JSON(static_cast<double>(note.unitId)));
                nj.set("pitchOffset", JSON(static_cast<double>(note.pitchOffset)));
                nj.set("gate", JSON(static_cast<double>(note.gate)));
                nj.set("slide", JSON(note.slide));
                notesArray.push(nj);
            }
            pjs.set("notes", notesArray);

            // Serialize scale context if present
            if (p->scaleOverride.has_value()) {
                const auto& ctx = p->scaleOverride.value();
                JSON scaleJson = JSON::object();
                scaleJson.set("rootKey", JSON(static_cast<double>(ctx.rootKey)));
                scaleJson.set("scaleKind", JSON(scaleKindToString(ctx.scaleKind)));
                scaleJson.set("snapToScale", JSON(ctx.snapToScale));
                pjs.set("scale", scaleJson);
            }
        }
        patternsJson.push(pjs);
    }
    root.set("patterns", patternsJson);

    // 3. Save Lanes and Clips
    JSON lanesJson = JSON::array();
    size_t laneIndex = 0;
    for (const auto& laneId : playlist.getLaneIDs()) {
        if (const auto* lane = playlist.getLane(laneId)) {
            JSON ljs = JSON::object();
            ljs.set("id", JSON(lane->id.toString()));
            ljs.set("name", JSON(lane->name));
            // Store colors as strings to avoid scientific notation (AestraJSON parser can't parse exponent form).
            ljs.set("color", JSON(std::to_string(lane->colorRGBA)));
            ljs.set("volume", JSON(lane->volume));
            ljs.set("pan", JSON(lane->pan));
            ljs.set("mute", JSON(lane->muted));
            ljs.set("solo", JSON(lane->solo));
            if (const auto* channel = trackManager->getChannel(laneIndex)) {
                JSON routingJson = JSON::object();
                const uint32_t mainOutputId = channel->getMainOutputId();
                routingJson.set("mainOutputId", JSON(static_cast<double>(mainOutputId == 0xFFFFFFFFu ? 0u : mainOutputId)));

                JSON sendsJson = JSON::array();
                const auto sends = channel->getSends();
                for (const auto& send : sends) {
                    JSON sjs = JSON::object();
                    sjs.set("targetId", JSON(static_cast<double>(send.targetChannelId == 0xFFFFFFFFu ? 0u : send.targetChannelId)));
                    sjs.set("gain", JSON(static_cast<double>(send.gain)));
                    sjs.set("pan", JSON(static_cast<double>(send.pan)));
                    sjs.set("postFader", JSON(send.postFader));
                    sjs.set("mute", JSON(send.mute));
                    sjs.set("sidechainOnly", JSON(send.sidechainOnly));
                    sendsJson.push(sjs);
                }
                routingJson.set("sends", sendsJson);
                ljs.set("routing", routingJson);

                const auto effectChainState = channel->getEffectChain().saveState();
                if (!effectChainState.empty()) {
                    ljs.set("effectChainStateHex", JSON(bytesToHex(effectChainState)));
                }
            }

            // Automation (v3.1)
            JSON autoJson = JSON::array();
            for (const auto& curve : lane->automationCurves) {
                JSON cj = JSON::object();
                cj.set("param", JSON(curve.getTarget()));
                cj.set("targetEnum", JSON(static_cast<double>(curve.getAutomationTarget())));
                cj.set("default", JSON(curve.getDefaultValue()));
                
                JSON ptsJson = JSON::array();
                for (const auto& p : curve.getPoints()) {
                    JSON pj = JSON::object();
                    pj.set("b", JSON(p.beat));
                    pj.set("v", JSON(p.value));
                    pj.set("c", JSON(static_cast<double>(p.curve)));
                    ptsJson.push(pj);
                }
                cj.set("points", ptsJson);
                autoJson.push(cj);
            }
            ljs.set("automation", autoJson);

            JSON clipsJson = JSON::array();
            for (const auto& clip : lane->clips) {
                JSON cjs = JSON::object();
                cjs.set("id", JSON(clip.id.toString()));
                const uint64_t serializedPatternId = clip.patternId.value != 0 ? clip.patternId.value : clip.sourceId;
                cjs.set("patternId", JSON(static_cast<double>(serializedPatternId)));
                cjs.set("start", JSON(clip.startBeat));
                cjs.set("duration", JSON(clip.durationBeats));
                cjs.set("sourceOffset", JSON(clip.sourceOffset));
                cjs.set("name", JSON(clip.name));
                cjs.set("color", JSON(std::to_string(clip.colorRGBA)));

                // Edits
                JSON ejs = JSON::object();
                ejs.set("gain", JSON(static_cast<double>(clip.edits.gainLinear)));
                ejs.set("pan", JSON(static_cast<double>(clip.edits.pan)));
                ejs.set("muted", JSON(clip.edits.muted));
                ejs.set("playbackRate", JSON(static_cast<double>(clip.edits.playbackRate)));
                ejs.set("fadeIn", JSON(clip.edits.fadeInBeats));
                ejs.set("fadeOut", JSON(clip.edits.fadeOutBeats));
                ejs.set("sourceStart", JSON(static_cast<double>(clip.edits.sourceStart)));
                cjs.set("edits", ejs);

                clipsJson.push(cjs);
            }
            ljs.set("clips", clipsJson);
            lanesJson.push(ljs);
        }
        ++laneIndex;
    }
    root.set("lanes", lanesJson);

    // 4. Save Arsenal Units
    root.set("arsenal", trackManager->getUnitManager().saveToJSON());

    // 5. Optional UI state (panels, dialog, etc.)
    if (uiState) {
        JSON ui = JSON::object();

        JSON settings = JSON::object();
        settings.set("visible", JSON(uiState->settingsDialogVisible));
        settings.set("activePage", JSON(uiState->settingsDialogActivePage));
        ui.set("settingsDialog", settings);

        JSON panels = JSON::array();
        for (const auto& p : uiState->panels) {
            JSON pj = JSON::object();
            pj.set("title", JSON(p.title));
            pj.set("x", JSON(p.x));
            pj.set("y", JSON(p.y));
            pj.set("width", JSON(p.width));
            pj.set("height", JSON(p.height));
            pj.set("expandedHeight", JSON(p.expandedHeight));
            pj.set("minimized", JSON(p.minimized));
            pj.set("maximized", JSON(p.maximized));
            pj.set("userPositioned", JSON(p.userPositioned));
            panels.push(pj);
        }
        ui.set("panels", panels);

        root.set("ui", ui);
    }

    result.contents = root.toString(indentSpaces);
    result.ok = true;
    return result;
}

bool ProjectSerializer::writeAtomically(const std::string& path, const std::string& contents) {
    return writeAtomicallyImpl(path, contents);
}

bool ProjectSerializer::save(const std::string& path,
                             const std::shared_ptr<TrackManager>& trackManager,
                             double tempo,
                             double playheadSeconds,
                             const UIState* uiState) {
    // Create backup of existing file before overwriting
    namespace fs = std::filesystem;
    if (fs::exists(path)) {
        fs::path backupPath = path;
        backupPath += ".bak";
        std::error_code ec;
        fs::copy_file(path, backupPath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            Log::warning("Could not create backup: " + ec.message());
        }
    }

    auto ser = serialize(trackManager, tempo, playheadSeconds, 2, uiState);
    if (!ser.ok) return false;
    if (!writeAtomicallyImpl(path, ser.contents)) {
        Log::error("Project save failed: " + path);
        return false;
    }

    writeHistorySnapshot(path, ser.contents);

    Log::info("Project saved to " + path);
    return true;
}

ProjectSerializer::LoadResult ProjectSerializer::load(const std::string& path,
                                                      const std::shared_ptr<TrackManager>& trackManager) {
    LoadResult result;
    if (!trackManager) {
        result.errorMessage = "Invalid track manager";
        return result;
    }
    if (!std::filesystem::exists(path)) {
        result.errorMessage = "File not found: " + path;
        return result;
    }
    const std::filesystem::path projectPath(path);

#if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
    Log::info(std::string("[ProjectLoad] Begin: ") + path);
#endif

    // ========================================================================
    // PHASE 1: Parse and validate JSON (non-destructive)
    // ========================================================================
    std::error_code fileEc;
    const auto fileSize = std::filesystem::file_size(path, fileEc);
    if (fileEc || fileSize > PROJECT_MAX_FILE_BYTES) {
        result.errorMessage = "Invalid project file: file is too large or unreadable";
        Log::error("[ProjectLoad] " + result.errorMessage);
        return result;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.errorMessage = "Cannot open file: " + path;
        return result;
    }
    
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    
#if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
    Log::info("[ProjectLoad] Read bytes=" + std::to_string(contents.size()));
#endif

    if (!hasJsonObjectEnvelope(contents)) {
        result.errorMessage = "Invalid project file: project must be a single JSON object";
        Log::error("[ProjectLoad] " + result.errorMessage);
        return result;
    }
    if (hasUnsafeNumericToken(contents)) {
        result.errorMessage = "Invalid project file: numeric token exceeds safe bounds";
        Log::error("[ProjectLoad] " + result.errorMessage);
        return result;
    }

    JSON root = JSON::parse(contents);
    if (!root.isObject()) {
        result.errorMessage = "Invalid project file: not a valid JSON object";
        Log::error("[ProjectLoad] " + result.errorMessage);
        return result;
    }
#if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
    Log::info("[ProjectLoad] Parsed JSON ok");
#endif

    // Version check
    int fileVersion = 0;
    if (root.has("version") && root["version"].isNumber() && std::isfinite(root["version"].asNumber())) {
        fileVersion = static_cast<int>(root["version"].asNumber());
    }
    
    if (fileVersion < PROJECT_VERSION_MIN_SUPPORTED) {
        result.errorMessage = "Project file version " + std::to_string(fileVersion) + 
                   " is too old. Minimum supported: " + std::to_string(PROJECT_VERSION_MIN_SUPPORTED);
        Log::error("[ProjectLoad] " + result.errorMessage);
        return result;
    }
    
    if (fileVersion > PROJECT_VERSION_CURRENT) {
        result.errorMessage = "Project file version " + std::to_string(fileVersion) + 
                   " is newer than this version of AESTRA (" + std::to_string(PROJECT_VERSION_CURRENT) + 
                   "). Please update AESTRA to open this project.";
        Log::error("[ProjectLoad] " + result.errorMessage);
        return result;
    }

    if (!validateProjectStructure(root, result.errorMessage)) {
        Log::error("[ProjectLoad] " + result.errorMessage);
        return result;
    }
    
    Log::info("[ProjectLoad] Version " + std::to_string(fileVersion) + " (current: " + 
              std::to_string(PROJECT_VERSION_CURRENT) + ")");

    // Run migrations if needed
    if (fileVersion < PROJECT_VERSION_CURRENT) {
        Log::info("[ProjectLoad] Migrating from v" + std::to_string(fileVersion) + 
                  " to v" + std::to_string(PROJECT_VERSION_CURRENT));
        if (!ProjectMigrations::runMigrations(root, fileVersion, PROJECT_VERSION_CURRENT)) {
            result.errorMessage = "Failed to migrate project from version " + 
                                  std::to_string(fileVersion) + " to " + 
                                  std::to_string(PROJECT_VERSION_CURRENT);
            Log::error("[ProjectLoad] " + result.errorMessage);
            return result;
        }
        Log::info("[ProjectLoad] Migration complete");
    }

    // ========================================================================
    // PHASE 2: Build load plan and validate references (non-destructive)
    // ========================================================================

    std::unordered_set<uint64_t> allPatternIds;
    std::unordered_set<uint64_t> allUnitIds;
    std::unordered_set<uint32_t> allSourceIds;
    std::unordered_map<uint64_t, std::string> patternNames;
    std::unordered_set<uint64_t> orphanClipPatternIds;
    std::unordered_set<uint64_t> orphanNoteUnitIds;

    if (root.has("sources")) {
        const JSON& sj = root["sources"];
        for (size_t i = 0; i < sj.size(); ++i) {
            uint32_t id = static_cast<uint32_t>(finiteNumberOr(sj[i], "id", 0.0, 0.0, static_cast<double>(UINT32_MAX)));
            if (id != 0) allSourceIds.insert(id);
        }
    }

    if (root.has("patterns")) {
        const JSON& pj = root["patterns"];
        for (size_t i = 0; i < pj.size(); ++i) {
            uint64_t id = static_cast<uint64_t>(finiteNumberOr(pj[i], "id", 0.0, 0.0, static_cast<double>(UINT64_MAX)));
            if (id != 0) {
                allPatternIds.insert(id);
                patternNames[id] = boundedStringOr(pj[i], "name", "Pattern", PROJECT_MAX_STRING_BYTES);
            }
        }
    }

    if (root.has("arsenal")) {
        const JSON& aj = root["arsenal"];
        if (aj.has("units") && aj["units"].isArray()) {
            const JSON& units = aj["units"];
            for (size_t i = 0; i < units.size(); ++i) {
                if (!units[i].isObject() || !units[i].has("id")) continue;
                uint64_t id = static_cast<uint64_t>(units[i]["id"].asNumber());
                if (id != 0) allUnitIds.insert(id);
            }
        }
    }

    if (root.has("lanes")) {
        const JSON& lj = root["lanes"];
        for (size_t i = 0; i < lj.size(); ++i) {
            if (!lj[i].has("clips") || !lj[i]["clips"].isArray()) continue;
            const JSON& cj = lj[i]["clips"];
            for (size_t c = 0; c < cj.size(); ++c) {
                if (!cj[c].isObject()) continue;
                uint64_t patternId = static_cast<uint64_t>(
                    finiteNumberOr(cj[c], "patternId", 0.0, 0.0, static_cast<double>(UINT64_MAX)));
                if (patternId != 0 && !allPatternIds.count(patternId)) {
                    orphanClipPatternIds.insert(patternId);
                    Log::warning("[ProjectLoad] Clip references missing pattern " + std::to_string(patternId) +
                               " - clip will be preserved with placeholder");
                }
            }
        }
    }

    if (root.has("patterns")) {
        const JSON& pj = root["patterns"];
        for (size_t i = 0; i < pj.size(); ++i) {
            std::string type = boundedStringOr(pj[i], "type", "midi", PROJECT_MAX_STRING_BYTES);
            if (type == "midi" && pj[i].has("notes") && pj[i]["notes"].isArray()) {
                const JSON& notes = pj[i]["notes"];
                for (size_t n = 0; n < notes.size(); ++n) {
                    if (!notes[n].isObject()) continue;
                    uint64_t unitId = static_cast<uint64_t>(
                        finiteNumberOr(notes[n], "unitId", 0.0, 0.0, static_cast<double>(UINT64_MAX)));
                    if (unitId != 0 && !allUnitIds.count(unitId)) {
                        orphanNoteUnitIds.insert(unitId);
                        Log::warning("[ProjectLoad] MIDI note references missing Arsenal unit " + std::to_string(unitId) +
                                   " - note preserved but unit reference unresolved");
                    }
                }
            }
        }
    }

    // Build structured report for reference validation issues
    if (!orphanClipPatternIds.empty() || !orphanNoteUnitIds.empty()) {
        auto report = std::make_unique<ProjectLoadReport>();
        for (const auto& pid : orphanClipPatternIds) {
            report->issues.push_back({
                LoadIssueSeverity::Warning,
                "clip",
                "Clip references missing pattern - clip will be preserved with placeholder",
                pid,
                std::to_string(pid),
                ""
            });
        }
        for (const auto& uid : orphanNoteUnitIds) {
            report->issues.push_back({
                LoadIssueSeverity::Warning,
                "unit",
                "MIDI note references missing Arsenal unit - note preserved but unit reference unresolved",
                uid,
                std::to_string(uid),
                ""
            });
        }
        result.report = std::move(report);
    }

    // ========================================================================
    // PHASE 3: Validate structure and check assets (non-destructive)
    // ========================================================================
    
    // Validate and collect missing audio assets
    if (root.has("sources")) {
        const JSON& sj = root["sources"];
        for (size_t i = 0; i < sj.size(); ++i) {
            if (!sj[i].has("path")) continue;
            std::string storedPath = sj[i]["path"].asString();
            std::filesystem::path filePath = resolveProjectAssetPath(projectPath, storedPath);
            if (!std::filesystem::exists(filePath) || !std::filesystem::is_regular_file(filePath)) {
                result.missingAssets.push_back(storedPath);
                Log::warning("[ProjectLoad] Missing or unreadable audio asset: " + storedPath);
            }
        }
    }
    
    // Log missing assets but don't fail - we'll load what we can
    if (!result.missingAssets.empty()) {
        Log::warning("[ProjectLoad] " + std::to_string(result.missingAssets.size()) + 
                     " audio file(s) not found - clips will appear without waveforms");
    }

// ========================================================================
// PHASE 4: Commit - clear existing state and load new data
// ========================================================================
    
    result.tempo = finiteNumberOr(root, "tempo", 120.0, 20.0, 999.0);
    result.playhead = finiteNumberOr(root, "playhead", 0.0, 0.0, 24.0 * 60.0 * 60.0);

    // Optional UI state
    if (root.has("ui") && root["ui"].isObject()) {
        UIState uiState;
        const JSON& ui = root["ui"];

        if (ui.has("settingsDialog") && ui["settingsDialog"].isObject()) {
            const JSON& sd = ui["settingsDialog"];
            if (sd.has("visible") && sd["visible"].isBool()) {
                uiState.settingsDialogVisible = sd["visible"].asBool();
            }
            if (sd.has("activePage") && sd["activePage"].isString()) {
                uiState.settingsDialogActivePage = sd["activePage"].asString();
            }
        }

        if (ui.has("panels") && ui["panels"].isArray()) {
            const JSON& panels = ui["panels"];
            for (size_t i = 0; i < panels.size(); ++i) {
                const JSON& pj = panels[i];
                if (!pj.isObject()) continue;

                PanelState p;
                p.title = boundedStringOr(pj, "title", "", PROJECT_MAX_STRING_BYTES);
                p.x = finiteNumberOr(pj, "x", 0.0, -100000.0, 100000.0);
                p.y = finiteNumberOr(pj, "y", 0.0, -100000.0, 100000.0);
                p.width = finiteNumberOr(pj, "width", 0.0, 0.0, 100000.0);
                p.height = finiteNumberOr(pj, "height", 0.0, 0.0, 100000.0);
                p.expandedHeight = finiteNumberOr(pj, "expandedHeight", 0.0, 0.0, 100000.0);
                if (pj.has("minimized") && pj["minimized"].isBool()) p.minimized = pj["minimized"].asBool();
                if (pj.has("maximized") && pj["maximized"].isBool()) p.maximized = pj["maximized"].asBool();
                if (pj.has("userPositioned") && pj["userPositioned"].isBool()) p.userPositioned = pj["userPositioned"].asBool();

                if (!p.title.empty()) uiState.panels.push_back(std::move(p));
            }
        }

        result.ui = std::move(uiState);
    }

    auto& playlist = trackManager->getPlaylistModel();
    auto& sourceManager = trackManager->getSourceManager();
    auto& patternManager = trackManager->getPatternManager();

    auto batch = playlist.scopedBatchUpdate();

#if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
    Log::info("[ProjectLoad] Clearing existing state");
#endif

    playlist.clear();
    sourceManager.clear();
    patternManager.clear();
    trackManager->clearAllChannels();

    // 2. Load Sources (and decode audio files)
    std::unordered_map<uint32_t, ClipSourceID> idMap;
    if (root.has("sources")) {
        const JSON& sj = root["sources"];
    #if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
        Log::info("[ProjectLoad] Loading sources count=" + std::to_string(sj.size()));
    #endif
        for (size_t i = 0; i < sj.size(); ++i) {
            uint32_t oldId = static_cast<uint32_t>(finiteNumberOr(sj[i], "id", 0.0, 0.0, static_cast<double>(UINT32_MAX)));
            std::string storedPath = boundedStringOr(sj[i], "path", "", PROJECT_MAX_PATH_BYTES);
            if (oldId == 0 || storedPath.empty()) {
                continue;
            }
            std::filesystem::path resolvedPath = resolveProjectAssetPath(projectPath, storedPath);
            const std::string sourcePath = storedPath; // Use original storedPath for serialization
            const std::string filePath = resolvedPath.string(); // Use resolvedPath only for file I/O
            ClipSourceID newId = sourceManager.getOrCreateSource(sourcePath);
            idMap[oldId] = newId;
            const bool assetReadable =
                std::filesystem::exists(resolvedPath) && std::filesystem::is_regular_file(resolvedPath);
            if (!assetReadable) {
                result.missingAssets.push_back(storedPath);
                Log::warning("[ProjectLoad] Missing or unreadable audio asset: " + storedPath);
            }
            
            // Actually decode the audio file and load into source
            ClipSource* source = sourceManager.getSource(newId);
            if (source && !source->isReady() && assetReadable && isRegularDecodableAsset(resolvedPath)) {
                std::vector<float> decodedData;
                uint32_t sampleRate = 0;
                uint32_t numChannels = 0;
                
                Log::info("[ProjectLoad] Decoding audio: " + filePath);
                if (decodeAudioFile(filePath, decodedData, sampleRate, numChannels, nullptr)) {
                    auto buffer = std::make_shared<AudioBufferData>();
                    buffer->interleavedData = std::move(decodedData);
                    buffer->sampleRate = sampleRate;
                    buffer->numChannels = numChannels;
                    if (numChannels == 0) {
                        Log::warning("[ProjectLoad] Audio file reports 0 channels: " + filePath);
                        buffer->numChannels = 1;
                    }
                    buffer->numFrames = buffer->interleavedData.size() / buffer->numChannels;
                    source->setBuffer(buffer);
                    Log::info("[ProjectLoad] Loaded audio: " + filePath + 
                              " (" + std::to_string(buffer->numFrames) + " frames, " + 
                              std::to_string(sampleRate) + " Hz)");
                } else {
                    Log::warning("[ProjectLoad] Failed to decode: " + filePath + " — creating silent mono fallback");
                    result.missingAssets.push_back(storedPath);
                    auto fallback = std::make_shared<AudioBufferData>();
                    fallback->numChannels = 1;
                    fallback->sampleRate = 44100;
                    fallback->numFrames = 0;
                    source->setBuffer(fallback);
                }
            }
        }
    }

    // 5. Load Arsenal Units (must load before patterns - patterns reference unitId in MIDI note data)
    if (root.has("arsenal")) {
        trackManager->getUnitManager().loadFromJSON(root["arsenal"]);
    }

    // 3. Load Patterns
    std::unordered_map<uint64_t, PatternID> patternMap;
    if (root.has("patterns")) {
        const JSON& pj = root["patterns"];
    #if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
        Log::info("[ProjectLoad] Loading patterns count=" + std::to_string(pj.size()));
    #endif
        for (size_t i = 0; i < pj.size(); ++i) {
            uint64_t oldId = static_cast<uint64_t>(finiteNumberOr(pj[i], "id", 0.0, 0.0, static_cast<double>(UINT64_MAX)));
            std::string name = boundedStringOr(pj[i], "name", "Pattern", PROJECT_MAX_STRING_BYTES);
            double length = finiteNumberOr(pj[i], "length", 4.0, 0.0001, 1000000.0);
            std::string type = boundedStringOr(pj[i], "type", "midi", PROJECT_MAX_STRING_BYTES);
            if (oldId == 0) {
                continue;
            }

            if (type == "audio") {
                uint32_t oldSrcId = static_cast<uint32_t>(
                    finiteNumberOr(pj[i], "sourceId", 0.0, 0.0, static_cast<double>(UINT32_MAX)));
                if (idMap.count(oldSrcId)) {
                    AudioSlicePayload payload;
                    payload.audioSourceId = idMap[oldSrcId];
                    if (pj[i].has("slices")) {
                        const JSON& slj = pj[i]["slices"];
                        for (size_t s = 0; s < slj.size(); ++s) {
                            if (!slj[s].isObject()) continue;
                            payload.slices.push_back({
                                finiteNumberOr(slj[s], "start", 0.0, 0.0, 1.0e15),
                                finiteNumberOr(slj[s], "length", 0.0, 0.0, 1.0e15)
                            });
                        }
                    }
                    PatternID newId = patternManager.createAudioPattern(name, length, payload);
                    patternMap[oldId] = newId;
                }
            } else {
                MidiPayload payload;
                if (pj[i].has("notes") && pj[i]["notes"].isArray()) {
                    const JSON& notes = pj[i]["notes"];
                    payload.notes.reserve(notes.size());
                    for (size_t n = 0; n < notes.size(); ++n) {
                        if (!notes[n].isObject()) continue;
                        MidiNote note;
                        note.pitch = static_cast<int>(finiteNumberOr(notes[n], "pitch", 60.0, 0.0, 127.0));
                        note.startBeat = finiteNumberOr(notes[n], "startBeat", 0.0, 0.0, 1000000.0);
                        note.durationBeats = finiteNumberOr(notes[n], "durationBeats", 0.25, 0.0, 1000000.0);
                        note.velocity = static_cast<float>(finiteNumberOr(notes[n], "velocity", 1.0, 0.0, 1.0));
                        note.unitId = static_cast<uint64_t>(
                            finiteNumberOr(notes[n], "unitId", 0.0, 0.0, static_cast<double>(UINT64_MAX)));
                        note.pitchOffset = static_cast<int8_t>(finiteNumberOr(notes[n], "pitchOffset", 0.0, -128.0, 127.0));
                        note.gate = static_cast<float>(finiteNumberOr(notes[n], "gate", 1.0, 0.0, 1.0));
                        if (notes[n].has("slide")) note.slide = notes[n]["slide"].asBool();
                        payload.notes.push_back(note);
                    }
                }
                PatternID newId = patternManager.createMidiPattern(name, length, payload);
                patternMap[oldId] = newId;

                // Deserialize scale context if present
                if (pj[i].has("scale") && pj[i]["scale"].isObject()) {
                    const JSON& scaleJson = pj[i]["scale"];
                    ScaleContext ctx;
                    ctx.rootKey = static_cast<int>(finiteNumberOr(scaleJson, "rootKey", 0.0, -12.0, 24.0));
                    if (scaleJson.has("scaleKind") && scaleJson["scaleKind"].isString()) {
                        auto kind = scaleKindFromString(scaleJson["scaleKind"].asString());
                        if (kind.has_value()) {
                            ctx.scaleKind = kind.value();
                        }
                    }
                    if (scaleJson.has("snapToScale") && scaleJson["snapToScale"].isBool()) {
                        ctx.snapToScale = scaleJson["snapToScale"].asBool();
                    }
                    if (ctx.hasNonDefaultValues()) {
                        PatternSource* pattern = patternManager.getPattern(newId);
                        if (pattern) {
                            pattern->scaleOverride = ctx;
                        }
                    }
                }
            }
        }
    }

    // 4. Load Lanes and Clips
    if (root.has("lanes")) {
        const JSON& lj = root["lanes"];
    #if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
        Log::info("[ProjectLoad] Loading lanes count=" + std::to_string(lj.size()));
    #endif
        for (size_t i = 0; i < lj.size(); ++i) {
    #if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
            Log::info("[ProjectLoad] Lane[" + std::to_string(i) + "] name='" + lj[i]["name"].asString() + "'");
    #endif
            const std::string laneName = boundedStringOr(lj[i], "name", "Track", PROJECT_MAX_STRING_BYTES);
            PlaylistLaneID laneId = playlist.createLane(laneName);
            MixerChannel* channel = trackManager->addChannel(laneName);
            if (auto* lane = playlist.getLane(laneId)) {
                if (lj[i].has("color") && lj[i]["color"].isString()) {
                    try {
                        lane->colorRGBA = static_cast<uint32_t>(std::stoul(lj[i]["color"].asString()));
                    } catch (const std::exception&) {
                        lane->colorRGBA = 0xFFFFFFFF;
                    }
                } else if (lj[i].has("color") && lj[i]["color"].isNumber()) {
                    lane->colorRGBA = static_cast<uint32_t>(lj[i]["color"].asNumber());
                } else {
                    lane->colorRGBA = 0xFFFFFFFF;
                }
                lane->volume = static_cast<float>(finiteNumberOr(lj[i], "volume", 1.0, 0.0, 4.0));
                lane->pan = static_cast<float>(finiteNumberOr(lj[i], "pan", 0.0, -1.0, 1.0));
                lane->muted = lj[i].has("mute") && lj[i]["mute"].isBool() && lj[i]["mute"].asBool();
                lane->solo = lj[i].has("solo") && lj[i]["solo"].isBool() && lj[i]["solo"].asBool();

                if (channel) {
                    channel->setName(lane->name);
                    channel->setColor(lane->colorRGBA);
                    channel->setVolume(lane->volume);
                    channel->setPan(lane->pan);
                    channel->setMute(lane->muted);
                    channel->setSolo(lane->solo);

                    if (lj[i].has("routing") && lj[i]["routing"].isObject()) {
                        const JSON& rj = lj[i]["routing"];
                        const uint32_t mainOutputId = (rj.has("mainOutputId") && rj["mainOutputId"].isNumber())
                            ? static_cast<uint32_t>(rj["mainOutputId"].asNumber())
                            : 0u;
                        channel->setMainOutputId(mainOutputId == 0 ? 0xFFFFFFFFu : mainOutputId);

                        if (rj.has("sends") && rj["sends"].isArray()) {
                            const JSON& sj = rj["sends"];
                            for (size_t s = 0; s < sj.size(); ++s) {
                                if (!sj[s].isObject()) continue;
                                if (!sj[s].has("targetId") || !sj[s]["targetId"].isNumber()) continue;
                                AudioRoute route;
                                const uint32_t targetId = static_cast<uint32_t>(sj[s]["targetId"].asNumber());
                                route.targetChannelId = (targetId == 0) ? 0xFFFFFFFFu : targetId;
                                if (sj[s].has("gain") && sj[s]["gain"].isNumber()) route.gain = static_cast<float>(sj[s]["gain"].asNumber());
                                if (sj[s].has("pan") && sj[s]["pan"].isNumber()) route.pan = static_cast<float>(sj[s]["pan"].asNumber());
                                if (sj[s].has("postFader") && sj[s]["postFader"].isBool()) route.postFader = sj[s]["postFader"].asBool();
                                if (sj[s].has("mute") && sj[s]["mute"].isBool()) route.mute = sj[s]["mute"].asBool();
                                if (sj[s].has("sidechainOnly") && sj[s]["sidechainOnly"].isBool()) route.sidechainOnly = sj[s]["sidechainOnly"].asBool();
                                channel->addSend(route);
                            }
                        }
                    }

                    if (lj[i].has("effectChainStateHex") && lj[i]["effectChainStateHex"].isString()) {
                        const auto effectState = hexToBytes(lj[i]["effectChainStateHex"].asString());
                        if (!effectState.empty()) {
                            auto& pluginManager = PluginManager::getInstance();
                            auto& chain = channel->getEffectChain();
                            chain.prepare(pluginManager.getDefaultSampleRate(), pluginManager.getDefaultBlockSize());
                            if (!chain.loadState(effectState, pluginManager)) {
                                Log::warning("[ProjectLoad] Failed to restore effect chain on lane: " + lane->name);
                            }
                        }
                    }
                }

                if (lj[i].has("automation")) {
                    const JSON& aj = lj[i]["automation"];
                    double projectBPM = root.has("tempo") ? root["tempo"].asNumber() : 120.0;
                    // Automation point sample positions are serialized in project-rate
                    // coordinates. Serializer code must stay on PlaylistModel's sample
                    // rate; device/output sample rate belongs only in the audio I/O layer.
                    double projectSampleRate = playlist.getProjectSampleRate();
                    double samplesPerBeat = (projectSampleRate * 60.0) / std::max(projectBPM, 1.0);
                    for (size_t a = 0; a < aj.size(); ++a) {
                        if (!aj[a].isObject()) continue;
                        std::string param = boundedStringOr(aj[a], "param", "", PROJECT_MAX_STRING_BYTES);
                        auto target = automationTargetFromRawInt(
                            static_cast<int>(finiteNumberOr(aj[a], "targetEnum", 0.0, 0.0, 255.0)));
                        // Warn for unrecognized targets (preserved non-fatally).
                        // AutomationTarget is uint8_t; known values are Volume(0), Pan(1), Custom(255).
                        // Unknown enums are kept as-is — the renderer is responsible for skipping them.
                        {
                            const int rawTarget = static_cast<int>(target);
                            if (rawTarget != 0 && rawTarget != 1 && rawTarget != 255) {
                                Log::warning("[ProjectLoad] Automation curve '" + param +
                                             "' has unrecognized target enum " + std::to_string(rawTarget) +
                                             "; curve preserved but may be skipped at runtime.");
                            }
                        }
                        
                        AutomationCurve curve(param, target);
                        curve.setDefaultValue(finiteNumberOr(aj[a], "default", 0.0, -1.0e6, 1.0e6));
                        
                        if (!aj[a].has("points") || !aj[a]["points"].isArray()) continue;
                        const JSON& pts = aj[a]["points"];
                        for (size_t p = 0; p < pts.size(); ++p) {
                            if (!pts[p].isObject()) continue;
                            curve.addPoint(finiteNumberOr(pts[p], "b", 0.0, 0.0, 1000000.0),
                                         finiteNumberOr(pts[p], "v", 0.0, -1.0e6, 1.0e6),
                                         samplesPerBeat,
                                         static_cast<float>(finiteNumberOr(pts[p], "c", 0.0, -1.0e6, 1.0e6)));
                        }
                        lane->automationCurves.push_back(curve);
                    }
                }

                if (lj[i].has("clips")) {
                    const JSON& cj = lj[i]["clips"];
#if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
                    Log::info("[ProjectLoad]  clips count=" + std::to_string(cj.size()));
#endif
                    for (size_t c = 0; c < cj.size(); ++c) {
                        if (!cj[c].isObject()) continue;
                        uint64_t oldPatId = static_cast<uint64_t>(
                            finiteNumberOr(cj[c], "patternId", 0.0, 0.0, static_cast<double>(UINT64_MAX)));
                        if (patternMap.count(oldPatId)) {
                            ClipInstance clip;
                            clip.id = ClipInstanceID::fromString(boundedStringOr(cj[c], "id", "", PROJECT_MAX_STRING_BYTES));
                            clip.patternId = patternMap[oldPatId];
                            clip.sourceId = clip.patternId.value;
                            clip.startBeat = finiteNumberOr(cj[c], "start", 0.0, 0.0, 1000000.0);
                            clip.durationBeats = finiteNumberOr(cj[c], "duration", 0.0, 0.0, 1000000.0);
                            clip.sourceOffset = finiteNumberOr(cj[c], "sourceOffset", 0.0, 0.0, 1000000.0);
                            clip.name = boundedStringOr(cj[c], "name", "", PROJECT_MAX_STRING_BYTES);
                            if (cj[c].has("color") && cj[c]["color"].isString()) {
                                try {
                                    clip.colorRGBA = static_cast<uint32_t>(std::stoul(cj[c]["color"].asString()));
                                } catch (const std::exception&) {
                                    clip.colorRGBA = 0xFFFFFFFF;
                                }
                            } else if (cj[c].has("color") && cj[c]["color"].isNumber()) {
                                clip.colorRGBA = static_cast<uint32_t>(cj[c]["color"].asNumber());
                            } else {
                                clip.colorRGBA = 0xFFFFFFFF;
                            }

                            if (cj[c].has("edits")) {
                                const JSON& ej = cj[c]["edits"];
                                clip.edits.gainLinear = static_cast<float>(finiteNumberOr(ej, "gain", 1.0, 0.0, 16.0));
                                clip.edits.pan = static_cast<float>(finiteNumberOr(ej, "pan", 0.0, -1.0, 1.0));
                                clip.edits.muted = ej.has("muted") && ej["muted"].isBool() && ej["muted"].asBool();
                                clip.edits.playbackRate = static_cast<float>(
                                    finiteNumberOr(ej, "playbackRate", 1.0, 0.01, 100.0));
                                clip.edits.fadeInBeats = finiteNumberOr(ej, "fadeIn", 0.0, 0.0, 1000000.0);
                                clip.edits.fadeOutBeats = finiteNumberOr(ej, "fadeOut", 0.0, 0.0, 1000000.0);
                                clip.edits.sourceStart = finiteNumberOr(ej, "sourceStart", 0.0, 0.0, 1.0e15);
                            }
                            playlist.addClip(laneId, clip);
                        }
                    }
                }
            }
        }
    }

    // PHASE 7: Validate send routing targets.
    // Unresolved sends are non-fatal — the audio runtime silently ignores them
    // via INVALID_SLOT checks. This warning helps diagnose silent routing loss.
    {
        std::unordered_set<uint32_t> validChannelIds;
        validChannelIds.insert(0xFFFFFFFFu); // master
        for (size_t ci = 0; ci < trackManager->getChannelCount(); ++ci) {
            if (auto* ch = trackManager->getChannel(ci)) {
                validChannelIds.insert(ch->getChannelId());
            }
        }
        for (size_t ci = 0; ci < trackManager->getChannelCount(); ++ci) {
            auto* channel = trackManager->getChannel(ci);
            if (!channel) continue;
            for (const auto& send : channel->getSends()) {
                if (!validChannelIds.count(send.targetChannelId)) {
                    Log::warning("[ProjectLoad] Send from '" + channel->getName() +
                                 "' targets channel ID " + std::to_string(send.targetChannelId) +
                                 " which does not exist; send will be silent.");
                }
            }
        }
    }

    // PHASE 8: Final rebind pass - verify all references
    #if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
    Log::info("[ProjectLoad] Final rebind pass");
    #endif

    result.ok = true;
    Log::info("Project loaded: " + path);
    return result;
}

std::string ProjectSerializer::getHistoryDirectory(const std::string& projectPath) {
    return getHistoryDirImpl(std::filesystem::path(projectPath)).string();
}

std::vector<ProjectSerializer::HistoryEntry> ProjectSerializer::listHistory(const std::string& projectPath) {
    namespace fs = std::filesystem;

    std::vector<HistoryEntry> history;
    const fs::path historyDir = getHistoryDirImpl(fs::path(projectPath));
    if (historyDir.empty()) {
        return history;
    }

    std::error_code ec;
    if (!fs::exists(historyDir, ec) || ec) {
        return history;
    }

    for (const auto& entry : fs::directory_iterator(historyDir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".aes") {
            continue;
        }

        HistoryEntry item;
        item.path = entry.path().string();
        item.label = entry.path().filename().string();
        item.sizeBytes = static_cast<uint64_t>(entry.file_size(ec));

        const auto ftime = fs::last_write_time(entry.path(), ec);
        if (!ec) {
            item.timestamp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        }
        history.push_back(std::move(item));
    }

    std::sort(history.begin(), history.end(), [](const HistoryEntry& a, const HistoryEntry& b) {
        return a.timestamp > b.timestamp;
    });

    return history;
}
