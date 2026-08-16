// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ProjectSerializer.h"
#include "ProjectMigrations.h"
#include "WorkspaceFocus.h"
#include "AestraFile.h"
#include "../AestraCore/include/AestraLog.h"
#include "MiniAudioDecoder.h"
#include "PluginManager.h"
#include "Music/ScaleContext.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
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
    constexpr size_t PROJECT_HISTORY_DEFAULT_MAX_ENTRIES = 50;
    // Total on-disk cap for a project's .history directory. A large project can
    // produce multi-MB snapshots, so a count-only limit (50) let history grow to
    // GBs (issue #274). Newest snapshots are always kept; older ones are pruned
    // once either the count or this byte budget is exceeded. Both limits are
    // runtime-configurable via ProjectSerializer::setHistoryLimits().
    constexpr uintmax_t PROJECT_HISTORY_DEFAULT_MAX_TOTAL_BYTES = 512ull * 1024ull * 1024ull; // 512 MB

    std::atomic<size_t> g_historyMaxEntries{PROJECT_HISTORY_DEFAULT_MAX_ENTRIES};
    std::atomic<uintmax_t> g_historyMaxTotalBytes{PROJECT_HISTORY_DEFAULT_MAX_TOTAL_BYTES};
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
    constexpr size_t PROJECT_LOAD_WARNING_LIMIT_PER_CATEGORY = 64;
    std::atomic<uint64_t> g_projectHistoryCounter{0};

    enum class ProjectLoadWarningCategory : size_t {
        ReferenceClip = 0,
        ReferenceUnit,
        MissingAsset,
        MissingAssetDecode,
        LaneCreate,
        LaneCreateChannel,
        EffectChain,
        AutomationTarget,
        DroppedClip,
        SendRoute,
        ClipTiming,
        LegacyDemoAutomation,
        Count
    };

    class ProjectLoadWarningLimiter {
    public:
        void warning(ProjectLoadWarningCategory category,
                     const std::string& message,
                     const std::string& suppressedSummary) {
            const size_t index = static_cast<size_t>(category);
            size_t& count = m_counts[index];
            if (count < PROJECT_LOAD_WARNING_LIMIT_PER_CATEGORY) {
                Log::warning(message);
            } else if (count == PROJECT_LOAD_WARNING_LIMIT_PER_CATEGORY) {
                Log::warning(suppressedSummary);
            }
            ++count;
        }

    private:
        std::array<size_t, static_cast<size_t>(ProjectLoadWarningCategory::Count)> m_counts{};
    };

    // --- Content integrity (#263) -------------------------------------------
    // FNV-1a 64-bit over the canonical compact serialization. Corruption
    // detection only — keyless, so it authenticates nothing; it catches disk
    // corruption, truncated writes, and accidental edits.
    uint64_t fnv1a64(const std::string& data) {
        uint64_t hash = 1469598103934665603ull;
        for (const unsigned char c : data) {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    constexpr const char* INTEGRITY_ALGO = "fnv1a64";

    // Computes the canonical content hash with the integrity hash field pinned
    // to "" — save and load call this with an identical tree shape, so both
    // hash identical bytes. NOTE: mutates root's integrity field; the caller
    // either overwrites it with the real hash (save) or no longer needs it (load).
    std::string computeIntegrityHashHex(JSON& root) {
        JSON integrity = JSON::object();
        integrity.set("algo", JSON(INTEGRITY_ALGO));
        integrity.set("hash", JSON(""));
        root.set("integrity", integrity);
        const uint64_t hash = fnv1a64(root.toString(0));
        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
        return std::string(buf);
    }

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

    std::filesystem::path makePrivateRollbackPath() {
        namespace fs = std::filesystem;

        std::error_code ec;
        fs::path base = fs::temp_directory_path(ec);
        if (ec || base.empty()) {
            return {};
        }
        base /= "Aestra";
        base /= "project-load-rollback";

        fs::create_directories(base, ec);
        if (ec) {
            return {};
        }
#ifndef _WIN32
        // On shared /tmp, permissions may fail if another user owns the dir.
        // Treat this as non-fatal — the dir exists and we can still write to it
        // if the umask allows; only the chmod fails.
        fs::permissions(base, fs::perms::owner_all, fs::perm_options::replace, ec);
        ec.clear(); // non-fatal
#endif

        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto seq = g_projectHistoryCounter.fetch_add(1, std::memory_order_relaxed);
        return base / ("rollback-" + std::to_string(stamp) + "-" + std::to_string(seq) + ".aes.rollback");
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
        if (!validArraySection(root, "mixerChannels", PROJECT_MAX_LANES, error)) return false;

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

    if (root.has("mixerChannels")) {
        const JSON& channels = root["mixerChannels"];
        for (size_t i = 0; i < channels.size(); ++i) {
            if (!channels[i].isObject()) {
                error = "Invalid project file: mixer channel entry must be an object";
                return false;
            }
            if (!validFiniteNumber(channels[i], "id", error, true))
                return false;
            if (!validStringField(channels[i], "name", PROJECT_MAX_STRING_BYTES, error))
                return false;
            if (!validColorField(channels[i], "color", error))
                return false;
            if (channels[i].has("effectChainStateHex") &&
                (!channels[i]["effectChainStateHex"].isString() ||
                 channels[i]["effectChainStateHex"].asString().size() > PROJECT_MAX_EFFECT_STATE_HEX_BYTES)) {
                error = "Invalid project file: mixer effect chain state is too large";
                return false;
            }
            if (channels[i].has("routing") && channels[i]["routing"].isObject() &&
                channels[i]["routing"].has("sends")) {
                const JSON& sends = channels[i]["routing"]["sends"];
                if (!sends.isArray() || sends.size() > PROJECT_MAX_SENDS_PER_LANE) {
                    error = "Invalid project file: mixer sends must be a bounded array";
                    return false;
                }
            }
        }
    }

    if (root.has("master")) {
        if (!root["master"].isObject()) {
            error = "Invalid project file: master entry must be an object";
            return false;
        }
        if (root["master"].has("effectChainStateHex") &&
            (!root["master"]["effectChainStateHex"].isString() ||
             root["master"]["effectChainStateHex"].asString().size() > PROJECT_MAX_EFFECT_STATE_HEX_BYTES)) {
            error = "Invalid project file: master effect chain state is too large";
            return false;
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
        // Sync to disk before atomic rename to prevent data loss on crash
        if (!Aestra::syncOfstream(out, tmp.string())) {
            Log::error("Project save failed: sync error: " + tmp.string());
            out.close();
            fs::remove(tmp, ec);
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

    if (!Aestra::fsyncParentDirectory(target.string())) {
        Log::error("Project save failed: directory sync error: " + target.parent_path().string());
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
    uintmax_t totalBytes = 0;
    for (const auto& entry : fs::directory_iterator(historyDir, ec)) {
        if (ec) {
            return;
        }
        if (entry.is_regular_file(ec) && entry.path().extension() == ".aes") {
            entries.push_back(entry);
            std::error_code sizeEc;
            totalBytes += fs::file_size(entry.path(), sizeEc);
        }
    }

    const size_t maxEntries = g_historyMaxEntries.load(std::memory_order_relaxed);
    const uintmax_t maxTotalBytes = g_historyMaxTotalBytes.load(std::memory_order_relaxed);
    if (entries.size() <= maxEntries && totalBytes <= maxTotalBytes) {
        return;
    }

    // Newest first, so the retained prefix is always the most recent snapshots.
    std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        std::error_code aec;
        std::error_code bec;
        return fs::last_write_time(a.path(), aec) > fs::last_write_time(b.path(), bec);
    });

    // Keep snapshots while they fit both the count cap and the cumulative byte
    // budget; remove everything past the first cap hit. The newest snapshot is
    // always retained even if it alone exceeds the budget — losing it would
    // defeat the point of writing history at all.
    uintmax_t keptBytes = 0;
    size_t kept = 0;
    bool prunedForSize = false;
    for (size_t i = 0; i < entries.size(); ++i) {
        std::error_code sizeEc;
        const uintmax_t entryBytes = fs::file_size(entries[i].path(), sizeEc);
        const bool withinCount = kept < maxEntries;
        const bool withinBytes = kept == 0 || keptBytes + entryBytes <= maxTotalBytes;
        if (withinCount && withinBytes) {
            keptBytes += entryBytes;
            ++kept;
            continue;
        }
        if (!withinBytes) {
            prunedForSize = true;
        }
        fs::remove(entries[i].path(), ec);
    }

    if (prunedForSize) {
        Log::warning("Project history exceeded its " +
                     std::to_string(maxTotalBytes / (1024ull * 1024ull)) +
                     " MB budget; pruned oldest snapshots in " + historyDir.string());
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
    root.set("version", JSON(static_cast<double>(ProjectSerializer::PROJECT_VERSION_CURRENT)));
    root.set("tempo", JSON(tempo));
    root.set("playhead", JSON(playheadSeconds));

    auto& playlist = trackManager->getPlaylistModel();
    auto& sourceManager = trackManager->getSourceManager();
    auto& patternManager = trackManager->getPatternManager();

    // 1. Save Sources
    JSON sourcesJson = JSON::array();
    std::vector<ClipSourceID> sourceIds = sourceManager.getAllSourceIDs();
    // Deterministic order: the store is an unordered_map, and byte-stable
    // save→load→save (#446) needs identical array order every time.
    std::sort(sourceIds.begin(), sourceIds.end(),
              [](const ClipSourceID& a, const ClipSourceID& b) { return a.value < b.value; });
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
    // Deterministic order (see sources note above / #446).
    std::sort(patterns.begin(), patterns.end(),
              [](const std::shared_ptr<PatternSource>& a, const std::shared_ptr<PatternSource>& b) {
                  return a->id.value < b->id.value;
              });
    for (const auto& p : patterns) {
        JSON pjs = JSON::object();
        pjs.set("id", JSON(static_cast<double>(p->id.value)));
        pjs.set("name", JSON(p->name));
        pjs.set("length", JSON(p->lengthBeats));
        pjs.set("mixerChannelId", JSON(static_cast<double>(p->getMixerChannelId())));
        
        if (p->isAudio()) {
            pjs.set("type", JSON("audio"));
            const auto& payload = std::get<AudioSlicePayload>(p->payload);
            pjs.set("sourceId", JSON(static_cast<double>(payload.audioSourceId.value)));
            if (payload.durationSeconds > 0.0) {
                pjs.set("durationSeconds", JSON(payload.durationSeconds));
            }
            
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
                nj.set("pan", JSON(static_cast<double>(note.pan)));
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

    // Mixer channels are persisted independently from Playlist lanes. The
    // lane-local copies below remain temporarily for backward readers, but this
    // top-level section is authoritative for current projects.
    JSON mixerChannelsJson = JSON::array();
    for (size_t channelIndex = 0; channelIndex < trackManager->getChannelCount(); ++channelIndex) {
        const auto* channel = trackManager->getChannel(channelIndex);
        if (!channel) {
            continue;
        }
        JSON mjs = JSON::object();
        mjs.set("id", JSON(static_cast<double>(channel->getChannelId())));
        mjs.set("name", JSON(channel->getName()));
        mjs.set("color", JSON(std::to_string(channel->getColor())));
        mjs.set("volume", JSON(static_cast<double>(channel->getVolume())));
        mjs.set("pan", JSON(static_cast<double>(channel->getPan())));
        mjs.set("mute", JSON(channel->isMuted()));
        mjs.set("solo", JSON(channel->isSoloed()));
        mjs.set("soloSafe", JSON(channel->isSoloSafe()));
        mjs.set("armed", JSON(channel->isArmed()));
        mjs.set("monitorInput", JSON(channel->isMonitoringEnabled()));
        mjs.set("inputChannelIndex", JSON(static_cast<double>(channel->getInputChannelIndex())));
        mjs.set("width", JSON(static_cast<double>(channel->getWidth())));
        mjs.set("trackColorIndex", JSON(static_cast<double>(channel->getTrackColorIndex())));

        JSON routingJson = JSON::object();
        const uint32_t mainOutputId = channel->getMainOutputId();
        routingJson.set("mainOutputId", JSON(static_cast<double>(mainOutputId == 0xFFFFFFFFu ? 0u : mainOutputId)));
        JSON sendsJson = JSON::array();
        for (const auto& send : channel->getSends()) {
            JSON sjs = JSON::object();
            sjs.set("targetId",
                    JSON(static_cast<double>(send.targetChannelId == 0xFFFFFFFFu ? 0u : send.targetChannelId)));
            sjs.set("gain", JSON(static_cast<double>(send.gain)));
            sjs.set("pan", JSON(static_cast<double>(send.pan)));
            sjs.set("postFader", JSON(send.postFader));
            sjs.set("mute", JSON(send.mute));
            sjs.set("sidechainOnly", JSON(send.sidechainOnly));
            sjs.set("sendId", JSON(std::to_string(send.sendId)));
            sendsJson.push(sjs);
        }
        routingJson.set("sends", sendsJson);
        mjs.set("routing", routingJson);

        const auto effectChainState = channel->getEffectChain().saveState();
        if (!effectChainState.empty()) {
            mjs.set("effectChainStateHex", JSON(bytesToHex(effectChainState)));
        }
        mixerChannelsJson.push(mjs);
    }
    root.set("mixerChannels", mixerChannelsJson);

    // 2b. Save the Master strip (a plugin host like any other channel, but a
    // terminal sink: no routing, no lane, just its insert chain).
    if (const auto* master = trackManager->getMasterChannel()) {
        JSON masterJson = JSON::object();
        const auto masterChainState = master->getEffectChain().saveState();
        if (!masterChainState.empty()) {
            masterJson.set("effectChainStateHex", JSON(bytesToHex(masterChainState)));
        }
        root.set("master", masterJson);
    }

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
                // MixerChannel state not covered by PlaylistLane
                ljs.set("mixerChannelId", JSON(static_cast<double>(channel->getChannelId())));
                ljs.set("soloSafe", JSON(channel->isSoloSafe()));
                ljs.set("armed", JSON(channel->isArmed()));
                ljs.set("monitorInput", JSON(channel->isMonitoringEnabled()));
                ljs.set("inputChannelIndex", JSON(static_cast<double>(channel->getInputChannelIndex())));
                ljs.set("width", JSON(static_cast<double>(channel->getWidth())));
                ljs.set("trackColorIndex", JSON(static_cast<double>(channel->getTrackColorIndex())));
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
                    sjs.set("sendId", JSON(std::to_string(send.sendId)));
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
                cj.set("mixerChannelId", JSON(static_cast<double>(curve.mixerChannelId)));
                cj.set("default", JSON(curve.getDefaultValue()));
                if (curve.getAutomationTarget() == Aestra::Audio::AutomationTarget::Custom) {
                    // Plugin-parameter address. instanceId is the authoritative
                    // decimal-string identity (Automation Identity Contract);
                    // slot stays for older-build compatibility and v1 readers.
                    cj.set("slot", JSON(static_cast<double>(curve.effectSlot)));
                    cj.set("paramId", JSON(static_cast<double>(curve.paramId)));
                    cj.set("instanceId", JSON(std::to_string(curve.deviceInstanceId)));
                }

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
                const auto* clipPattern = patternManager.getPattern(clip.patternId);
                const bool isAudioClip = clipPattern && clipPattern->isAudio();
                if (isAudioClip) {
                    const double durationSeconds = clip.durationSeconds > 0.0
                                                       ? clip.durationSeconds
                                                       : clip.durationBeats * 60.0 / std::max(tempo, 1.0);
                    cjs.set("durationSeconds", JSON(durationSeconds));
                    cjs.set("sourceOffsetSeconds", JSON(clip.sourceOffsetSeconds));
                } else {
                    cjs.set("duration", JSON(clip.durationBeats));
                }
                cjs.set("sourceOffset", JSON(clip.sourceOffset));
                cjs.set("name", JSON(clip.name));
                cjs.set("color", JSON(std::to_string(clip.colorRGBA)));

                // Edits
                JSON ejs = JSON::object();
                ejs.set("gain", JSON(static_cast<double>(clip.edits.gainLinear)));
                ejs.set("pan", JSON(static_cast<double>(clip.edits.pan)));
                ejs.set("muted", JSON(clip.edits.muted));
                ejs.set("playbackRate", JSON(static_cast<double>(clip.edits.playbackRate)));
                const float persistedPitch = std::isfinite(clip.edits.pitchSemitones)
                                                 ? std::clamp(clip.edits.pitchSemitones, ClipEdits::kMinPitchSemitones,
                                                              ClipEdits::kMaxPitchSemitones)
                                                 : 0.0f;
                ejs.set("pitchSemitones", JSON(static_cast<double>(persistedPitch)));
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

        // Phase-3 workspace state (optional on load; written on every save).
        if (!uiState->viewFocus.empty()) {
            ui.set("viewFocus", JSON(uiState->viewFocus));
        }
        ui.set("pianoRollOpen", JSON(uiState->pianoRollOpen));
        ui.set("sequencerOpen", JSON(uiState->sequencerOpen));

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

    // Content integrity (#263): stamp a canonical-content checksum so load can
    // detect corruption. Must be the LAST field written — the hash covers every
    // other field, computed with the hash slot pinned to "".
    {
        const std::string hashHex = computeIntegrityHashHex(root);
        JSON integrity = JSON::object();
        integrity.set("algo", JSON(INTEGRITY_ALGO));
        integrity.set("hash", JSON(hashHex));
        root.set("integrity", integrity);
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
    namespace fs = std::filesystem;

    // Serialize first — if this fails, we never touch the existing file
    auto ser = serialize(trackManager, tempo, playheadSeconds, 2, uiState);
    if (!ser.ok) return false;

    // Only create backup once we know serialization succeeded
    if (fs::exists(path)) {
        fs::path backupPath = path;
        backupPath += ".bak";
        std::error_code ec;
        fs::copy_file(path, backupPath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            Log::warning("Backup creation failed (non-fatal): " + ec.message());
        }
    }

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
    return load(path, trackManager, path);
}

ProjectSerializer::CandidateLoadResult
ProjectSerializer::loadFirstValid(const std::vector<std::string>& candidatePaths,
                                  const std::shared_ptr<TrackManager>& trackManager,
                                  const std::string& assetBasePath) {
    CandidateLoadResult selected;
    for (const auto& candidate : candidatePaths) {
        selected.result = load(candidate, trackManager, assetBasePath.empty() ? candidate : assetBasePath);
        if (selected.result.ok) {
            selected.loadedPath = candidate;
            return selected;
        }
        Log::warning("[ProjectLoad] Recovery candidate rejected: " + candidate + " (" +
                     selected.result.errorMessage + ")");
    }
    return selected;
}

ProjectSerializer::LoadResult ProjectSerializer::load(const std::string& path,
                                                      const std::shared_ptr<TrackManager>& trackManager,
                                                      const std::string& assetBasePath) {
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
    const std::filesystem::path assetRoot(assetBasePath);

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

    bool consumedAllInput = false;
    JSON root = JSON::parseStrict(contents, consumedAllInput);
    if (!root.isObject() || !consumedAllInput) {
        result.errorMessage = "Invalid project file: not exactly one valid JSON object";
        Log::error("[ProjectLoad] " + result.errorMessage);
        return result;
    }
#if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
    Log::info("[ProjectLoad] Parsed JSON ok");
#endif

    // Content integrity (#263): verify the checksum BEFORE migrations mutate
    // the tree. Files that predate the integrity field (or use an unknown
    // algo) load as Unchecked with no warning — backward compatible. A
    // mismatch loads non-destructively but loudly: the session may be
    // recoverable, and holding it hostage would betray recovery obligations;
    // structurally corrupt files still hard-fail through the validators below.
    if (root.has("integrity") && root["integrity"].isObject()) {
        const JSON& integrityField = root["integrity"];
        const std::string storedAlgo =
            integrityField.has("algo") && integrityField["algo"].isString() ? integrityField["algo"].asString() : "";
        const std::string storedHash =
            integrityField.has("hash") && integrityField["hash"].isString() ? integrityField["hash"].asString() : "";
        if (storedAlgo == INTEGRITY_ALGO && storedHash.size() == 16) {
            // Recompute over the canonical form (hash slot pinned to "") — this
            // overwrites root's integrity field, which nothing downstream reads.
            const std::string computedHash = computeIntegrityHashHex(root);
            if (computedHash == storedHash) {
                result.integrity = LoadIntegrity::Verified;
            } else {
                result.integrity = LoadIntegrity::Mismatch;
                Log::error("[ProjectLoad] CONTENT INTEGRITY MISMATCH: file was modified or corrupted since save "
                           "(stored " + storedHash + ", computed " + computedHash +
                           "). Loading non-destructively — verify the session before overwriting backups.");
            }
        } else {
            Log::info("[ProjectLoad] Integrity field present but unrecognized (algo='" + storedAlgo +
                      "') — skipping verification.");
        }
    }

    // Version check
    int fileVersion = 0;
    if (root.has("version") && root["version"].isNumber() && std::isfinite(root["version"].asNumber())) {
        fileVersion = static_cast<int>(root["version"].asNumber());
    }

    result.sourceSchemaVersion = fileVersion;
    result.resultingSchemaVersion = fileVersion;

    if (fileVersion < ProjectSerializer::PROJECT_VERSION_MIN_SUPPORTED) {
        result.errorMessage =
            "Project file version " + std::to_string(fileVersion) +
            " is too old. Minimum supported: " + std::to_string(ProjectSerializer::PROJECT_VERSION_MIN_SUPPORTED);
        Log::error("[ProjectLoad] " + result.errorMessage);
        return result;
    }

    if (fileVersion > ProjectSerializer::PROJECT_VERSION_CURRENT) {
        result.errorMessage = "Project file version " + std::to_string(fileVersion) +
                              " is newer than this version of AESTRA (" +
                              std::to_string(ProjectSerializer::PROJECT_VERSION_CURRENT) +
                              "). Please update AESTRA to open this project.";
        Log::error("[ProjectLoad] " + result.errorMessage);
        return result;
    }

    if (!validateProjectStructure(root, result.errorMessage)) {
        Log::error("[ProjectLoad] " + result.errorMessage);
        return result;
    }

    Log::info("[ProjectLoad] Version " + std::to_string(fileVersion) +
              " (current: " + std::to_string(ProjectSerializer::PROJECT_VERSION_CURRENT) + ")");

    // Run migrations if needed
    if (fileVersion < ProjectSerializer::PROJECT_VERSION_CURRENT) {
        Log::info("[ProjectLoad] Migrating from v" + std::to_string(fileVersion) + " to v" +
                  std::to_string(ProjectSerializer::PROJECT_VERSION_CURRENT));
        const auto migration =
            ProjectMigrations::runMigrations(root, fileVersion, ProjectSerializer::PROJECT_VERSION_CURRENT);
        result.migrationOutcome = migration.outcome;
        result.resultingSchemaVersion = migration.resultingVersion;
        if (!migration.ok()) {
            result.errorMessage = "Failed to migrate project from version " + std::to_string(fileVersion) + " to " +
                                  std::to_string(ProjectSerializer::PROJECT_VERSION_CURRENT);
            Log::error("[ProjectLoad] " + result.errorMessage);
            return result;
        }
        Log::info("[ProjectLoad] Migration complete");
    } else {
        result.resultingSchemaVersion = ProjectSerializer::PROJECT_VERSION_CURRENT;
    }

    // ========================================================================
    // PHASE 2: Build load plan and validate references (non-destructive)
    // ========================================================================

    std::unordered_set<uint64_t> allPatternIds;
    std::unordered_set<uint64_t> allUnitIds;
    std::unordered_set<uint32_t> allSourceIds;
    std::unordered_map<uint64_t, std::string> patternNames;
    std::unordered_set<uint64_t> unloadablePatternIds;
    std::unordered_set<uint64_t> recoverableClipPatternIds;
    std::unordered_set<uint64_t> orphanNoteUnitIds;
    ProjectLoadWarningLimiter warningLimiter;

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
                const std::string type = boundedStringOr(pj[i], "type", "midi", PROJECT_MAX_STRING_BYTES);
                if (type == "audio") {
                    const uint32_t sourceId = static_cast<uint32_t>(
                        finiteNumberOr(pj[i], "sourceId", 0.0, 0.0, static_cast<double>(UINT32_MAX)));
                    if (sourceId == 0 || !allSourceIds.count(sourceId)) {
                        unloadablePatternIds.insert(id);
                    }
                }
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
                if (patternId != 0 &&
                    (!allPatternIds.count(patternId) || unloadablePatternIds.count(patternId))) {
                    recoverableClipPatternIds.insert(patternId);
                    warningLimiter.warning(
                        ProjectLoadWarningCategory::ReferenceClip,
                        "[ProjectLoad] Clip references missing or unresolved pattern " + std::to_string(patternId) +
                            " - clip will be preserved with placeholder",
                        "[ProjectLoad] Additional recoverable clip-pattern warnings suppressed.");
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
                        warningLimiter.warning(
                            ProjectLoadWarningCategory::ReferenceUnit,
                            "[ProjectLoad] MIDI note references missing Arsenal unit " + std::to_string(unitId) +
                                " - note preserved but unit reference unresolved",
                            "[ProjectLoad] Additional missing-Arsenal-unit MIDI note warnings suppressed.");
                    }
                }
            }
        }
    }

    // Build structured report for reference validation issues
    if (!recoverableClipPatternIds.empty() || !orphanNoteUnitIds.empty() ||
        result.integrity == LoadIntegrity::Mismatch) {
        auto report = std::make_unique<ProjectLoadReport>();
        if (result.integrity == LoadIntegrity::Mismatch) {
            report->issues.push_back({
                LoadIssueSeverity::Warning,
                "integrity",
                "Project content checksum mismatch - file was modified or corrupted since save; "
                "loaded non-destructively, verify the session before overwriting backups",
                0,
                "",
                ""
            });
        }
        for (const auto& pid : recoverableClipPatternIds) {
            report->issues.push_back({
                LoadIssueSeverity::Warning,
                "clip",
                "Clip references missing or unresolved pattern - clip will be preserved with placeholder",
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
            std::filesystem::path filePath = resolveProjectAssetPath(assetRoot, storedPath);
            if (!std::filesystem::exists(filePath) || !std::filesystem::is_regular_file(filePath)) {
                result.missingAssets.push_back(storedPath);
                warningLimiter.warning(
                    ProjectLoadWarningCategory::MissingAsset,
                    "[ProjectLoad] Missing or unreadable audio asset: " + storedPath,
                    "[ProjectLoad] Additional missing or unreadable audio asset warnings suppressed.");
            }
        }
    }
    
    // Deduplicate missing assets
    {
        std::sort(result.missingAssets.begin(), result.missingAssets.end());
        result.missingAssets.erase(
            std::unique(result.missingAssets.begin(), result.missingAssets.end()),
            result.missingAssets.end());
    }
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

        // Phase-3 workspace state: all keys optional (pre-phase-3 files keep the
        // historical defaults: Timeline focus, overlays closed).
        if (ui.has("viewFocus") && ui["viewFocus"].isString()) {
            const std::string focusName = ui["viewFocus"].asString();
            if (focusName.size() <= 32) {
                ViewFocus parsed;
                if (WorkspaceFocusModel::parseWorkspaceFocus(focusName, parsed)) {
                    uiState.viewFocus = focusName;
                }
            }
        }
        if (ui.has("pianoRollOpen") && ui["pianoRollOpen"].isBool()) {
            uiState.pianoRollOpen = ui["pianoRollOpen"].asBool();
        }
        if (ui.has("sequencerOpen") && ui["sequencerOpen"].isBool()) {
            uiState.sequencerOpen = ui["sequencerOpen"].asBool();
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

    // Save snapshot of current state to a private rollback file BEFORE clearing.
    // If the new load fails, we restore from this file to avoid leaving
    // the project in an empty/corrupted state.
    // Skip rollback creation when loading a rollback file itself (prevents recursion).
    std::string rollbackPath;
    const bool isRollbackLoad = (path.find(".rollback") != std::string::npos);
    if (!isRollbackLoad) {
        SerializeResult preloadSnapshot;
        try {
            preloadSnapshot = serialize(trackManager, result.tempo, result.playhead, 0, nullptr);
        } catch (...) {
            Log::warning("[ProjectLoad] Could not create preload snapshot — rollback unavailable");
        }
        if (preloadSnapshot.ok && !preloadSnapshot.contents.empty()) {
            namespace fs = std::filesystem;
            fs::path tmpPath = makePrivateRollbackPath();
            if (!tmpPath.empty() && writeAtomicallyImpl(tmpPath.string(), preloadSnapshot.contents)) {
                rollbackPath = tmpPath.string();
            } else {
                Log::warning("[ProjectLoad] Could not write rollback file — recovery unavailable");
            }
        }
    }

#if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
    Log::info("[ProjectLoad] Clearing existing state");
#endif

    try {
        playlist.clear();
        sourceManager.clear();
        patternManager.clear();
        trackManager->clearAllChannels();
        playlist.setPatternManager(&patternManager);
        playlist.setBPM(result.tempo);
    
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
                std::filesystem::path resolvedPath = resolveProjectAssetPath(assetRoot, storedPath);
                const std::string sourcePath = storedPath; // Use original storedPath for serialization
                const std::string filePath = resolvedPath.string(); // Use resolvedPath only for file I/O
                // Restore the serialized source identity (#446); the idMap
                // remains for files whose ids collide or fail to restore.
                ClipSourceID newId = sourceManager.getOrCreateSourceWithId(ClipSourceID{oldId}, sourcePath);
                idMap[oldId] = newId;
                const bool assetReadable =
                    std::filesystem::exists(resolvedPath) && std::filesystem::is_regular_file(resolvedPath);
                if (!assetReadable) {
                    result.missingAssets.push_back(storedPath);
                    warningLimiter.warning(
                        ProjectLoadWarningCategory::MissingAsset,
                        "[ProjectLoad] Missing or unreadable audio asset: " + storedPath,
                        "[ProjectLoad] Additional missing or unreadable audio asset warnings suppressed.");
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
                        warningLimiter.warning(
                            ProjectLoadWarningCategory::MissingAssetDecode,
                            "[ProjectLoad] Failed to decode: " + filePath + " — creating silent mono fallback",
                            "[ProjectLoad] Additional audio decode failure warnings suppressed.");
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
        std::unordered_set<uint64_t> legacyAudioPatternIds;
        std::unordered_map<uint64_t, std::unordered_map<uint32_t, PatternID>> legacyAudioRouteVariants;
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
                        payload.durationSeconds = finiteNumberOr(pj[i], "durationSeconds", 0.0, 0.0, 1000000.0);
                        if (pj[i].has("slices")) {
                            const JSON& slj = pj[i]["slices"];
                            for (size_t s = 0; s < slj.size(); ++s) {
                                if (!slj[s].isObject()) continue;
                                // Mirror the writer, which persists startSamples/lengthSamples
                                // (AudioSlice fields 3-4). The old first-two-field aggregate
                                // filled startOffset/duration instead, so slice sample data
                                // zeroed out on the next save.
                                AudioSlice slice;
                                slice.startSamples = finiteNumberOr(slj[s], "start", 0.0, 0.0, 1.0e15);
                                slice.lengthSamples = finiteNumberOr(slj[s], "length", 0.0, 0.0, 1.0e15);
                                payload.slices.push_back(slice);
                            }
                        }
                        // Restore serialized pattern identity (#446); the map
                        // still covers collision/mint fallbacks.
                        PatternID newId = patternManager.createAudioPatternWithId(PatternID{oldId}, name, length, payload);
                        if (auto* pattern = patternManager.getPattern(newId)) {
                            if (pj[i].has("mixerChannelId") && pj[i]["mixerChannelId"].isNumber()) {
                                const double rawMixerChannelId = pj[i]["mixerChannelId"].asNumber();
                                if (std::isfinite(rawMixerChannelId) && rawMixerChannelId >= 0.0 &&
                                    rawMixerChannelId < static_cast<double>(UINT32_MAX)) {
                                    pattern->setMixerChannelId(static_cast<uint32_t>(rawMixerChannelId));
                                }
                            } else {
                                pattern->legacyMixerRoutePending = true;
                                legacyAudioPatternIds.insert(newId.value);
                            }
                        }
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
                            note.pan = static_cast<float>(finiteNumberOr(notes[n], "pan", 0.0, -1.0, 1.0));
                            note.unitId = static_cast<uint64_t>(
                                finiteNumberOr(notes[n], "unitId", 0.0, 0.0, static_cast<double>(UINT64_MAX)));
                            note.pitchOffset = static_cast<int8_t>(finiteNumberOr(notes[n], "pitchOffset", 0.0, -128.0, 127.0));
                            note.gate = static_cast<float>(finiteNumberOr(notes[n], "gate", 1.0, 0.0, 1.0));
                            if (notes[n].has("slide")) note.slide = notes[n]["slide"].asBool();
                            payload.notes.push_back(note);
                        }
                    }
                    // Restore serialized pattern identity (#446), as above.
                    PatternID newId = patternManager.createMidiPatternWithId(PatternID{oldId}, name, length, payload);
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

        // Preserve clip placement when a project references a pattern record
        // that is missing from the file. The empty MIDI placeholder keeps the
        // serialized pattern identity recoverable and deliberately renders
        // silence until the user replaces or repairs the missing pattern.
        for (uint64_t oldId : recoverableClipPatternIds) {
            MidiPayload placeholderPayload;
            std::string placeholderName = "[Missing Pattern " + std::to_string(oldId) + "]";
            const auto originalName = patternNames.find(oldId);
            if (originalName != patternNames.end() && !originalName->second.empty()) {
                placeholderName += " " + originalName->second;
            }
            const PatternID placeholderId = patternManager.createMidiPatternWithId(
                PatternID{oldId}, placeholderName, 4.0, placeholderPayload);
            patternMap[oldId] = placeholderId;
        }

        // Arsenal unit default patterns are serialized with project-file IDs.
        // Patterns are recreated during load, so rebind units to the new runtime IDs
        // after both unit and pattern stores have been restored.
        {
            auto& unitManager = trackManager->getUnitManager();
            for (UnitID unitId : unitManager.getAllUnitIDs()) {
                auto* unit = unitManager.getUnit(unitId);
                if (!unit || !unit->defaultPatternId.isValid()) {
                    continue;
                }

                auto remapped = patternMap.find(unit->defaultPatternId.value);
                if (remapped != patternMap.end()) {
                    unit->defaultPatternId = remapped->second;
                } else if (!patternManager.getPattern(unit->defaultPatternId)) {
                    Log::warning("[ProjectLoad] Arsenal unit " + std::to_string(unitId) +
                                 " references missing default pattern " +
                                 std::to_string(unit->defaultPatternId.value) + "; clearing association.");
                    unit->defaultPatternId = PatternID{};
                }
            }
        }
    
        // 4. Load mixer channels independently from Playlist lanes (v3+).
        const bool hasIndependentMixerChannels = root.has("mixerChannels") && root["mixerChannels"].isArray();
        if (hasIndependentMixerChannels) {
            const JSON& channels = root["mixerChannels"];
            for (size_t i = 0; i < channels.size(); ++i) {
                if (!channels[i].isObject()) {
                    continue;
                }
                const std::string channelName =
                    boundedStringOr(channels[i], "name", "Insert", PROJECT_MAX_STRING_BYTES);
                const uint32_t storedId = static_cast<uint32_t>(
                    finiteNumberOr(channels[i], "id", 0.0, 1.0, static_cast<double>(UINT32_MAX - 1)));
                MixerChannel* channel = trackManager->addChannelWithId(channelName, storedId);
                if (!channel) {
                    warningLimiter.warning(ProjectLoadWarningCategory::LaneCreateChannel,
                                           "[ProjectLoad] Failed to restore mixer channel '" + channelName + "'",
                                           "[ProjectLoad] Additional mixer channel creation failures suppressed.");
                    continue;
                }

                if (channels[i].has("color") && channels[i]["color"].isString()) {
                    try {
                        channel->setColor(
                            static_cast<uint32_t>(std::stoull(channels[i]["color"].asString()) & 0xFFFFFFFFu));
                    } catch (const std::exception&) {
                        channel->setColor(0xFFFFFFFFu);
                    }
                }
                channel->setVolume(static_cast<float>(finiteNumberOr(channels[i], "volume", 1.0, 0.0, 4.0)));
                channel->setPan(static_cast<float>(finiteNumberOr(channels[i], "pan", 0.0, -1.0, 1.0)));
                channel->setMute(channels[i].has("mute") && channels[i]["mute"].isBool() &&
                                 channels[i]["mute"].asBool());
                channel->setSolo(channels[i].has("solo") && channels[i]["solo"].isBool() &&
                                 channels[i]["solo"].asBool());
                if (channels[i].has("soloSafe") && channels[i]["soloSafe"].isBool())
                    channel->setSoloSafe(channels[i]["soloSafe"].asBool());
                if (channels[i].has("armed") && channels[i]["armed"].isBool())
                    channel->setArmed(channels[i]["armed"].asBool());
                if (channels[i].has("monitorInput") && channels[i]["monitorInput"].isBool())
                    channel->setMonitoringEnabled(channels[i]["monitorInput"].asBool());
                channel->setInputChannelIndex(
                    static_cast<int>(finiteNumberOr(channels[i], "inputChannelIndex", -1.0, -2.0, 1024.0)));
                channel->setWidth(static_cast<float>(finiteNumberOr(channels[i], "width", 1.0, 0.0, 4.0)));
                channel->setTrackColorIndex(
                    static_cast<int>(finiteNumberOr(channels[i], "trackColorIndex", -1.0, -1.0, 1024.0)));

                if (channels[i].has("routing") && channels[i]["routing"].isObject()) {
                    const JSON& routing = channels[i]["routing"];
                    const uint32_t mainOutputId = static_cast<uint32_t>(
                        finiteNumberOr(routing, "mainOutputId", 0.0, 0.0, static_cast<double>(UINT32_MAX)));
                    channel->setMainOutputId(mainOutputId == 0 ? 0xFFFFFFFFu : mainOutputId);
                    if (routing.has("sends") && routing["sends"].isArray()) {
                        const JSON& sends = routing["sends"];
                        for (size_t s = 0; s < sends.size(); ++s) {
                            if (!sends[s].isObject())
                                continue;
                            AudioRoute route;
                            const uint32_t targetId = static_cast<uint32_t>(
                                finiteNumberOr(sends[s], "targetId", 0.0, 0.0, static_cast<double>(UINT32_MAX)));
                            route.targetChannelId = targetId == 0 ? 0xFFFFFFFFu : targetId;
                            route.gain = static_cast<float>(finiteNumberOr(sends[s], "gain", 1.0, 0.0, 16.0));
                            route.pan = static_cast<float>(finiteNumberOr(sends[s], "pan", 0.0, -1.0, 1.0));
                            route.postFader = !sends[s].has("postFader") || !sends[s]["postFader"].isBool() ||
                                              sends[s]["postFader"].asBool();
                            route.mute = sends[s].has("mute") && sends[s]["mute"].isBool() && sends[s]["mute"].asBool();
                            route.sidechainOnly = sends[s].has("sidechainOnly") && sends[s]["sidechainOnly"].isBool() &&
                                                  sends[s]["sidechainOnly"].asBool();
                            // Stable send identity (Contract D2). Serialized as an
                            // exact decimal string so ids above 2^53 survive the
                            // JSON round trip. Legacy numeric values are accepted
                            // only inside the exact JSON-integer range; anything
                            // else (or a missing key) is 0, which mints in addSend.
                            route.sendId = 0;
                            if (sends[s].has("sendId") && sends[s]["sendId"].isString()) {
                                try {
                                    route.sendId = std::stoull(sends[s]["sendId"].asString());
                                } catch (const std::exception&) {
                                    route.sendId = 0;
                                }
                            } else if (sends[s].has("sendId") && sends[s]["sendId"].isNumber()) {
                                const double legacyId = sends[s]["sendId"].asNumber();
                                if (std::isfinite(legacyId) && legacyId >= 0.0 && legacyId <= 9007199254740991.0) {
                                    route.sendId = static_cast<uint64_t>(legacyId);
                                }
                            }
                            channel->addSend(route);
                        }
                    }
                }

                auto& pluginManager = PluginManager::getInstance();
                auto& chain = channel->getEffectChain();
                chain.prepare(pluginManager.getDefaultSampleRate(), pluginManager.getDefaultBlockSize());
                if (channels[i].has("effectChainStateHex") && channels[i]["effectChainStateHex"].isString()) {
                    const auto effectState = hexToBytes(channels[i]["effectChainStateHex"].asString());
                    std::vector<std::string> missingIds;
                    if (!effectState.empty() && !chain.loadState(effectState, pluginManager, &missingIds)) {
                        warningLimiter.warning(ProjectLoadWarningCategory::EffectChain,
                                               "[ProjectLoad] Failed to restore mixer effect chain on: " + channelName,
                                               "[ProjectLoad] Additional mixer effect-chain warnings suppressed.");
                    }
                    for (auto& id : missingIds) {
                        result.missingPlugins.push_back({std::move(id), channelName});
                    }
                }
            }
        }

        // 4b. Restore the Master strip's insert chain (absent in older
        // projects: loaders must ignore a missing master node).
        if (auto* master = trackManager->getMasterChannel()) {
            if (root.has("master") && root["master"].isObject()) {
                const JSON& masterJson = root["master"];
                auto& pluginManager = PluginManager::getInstance();
                auto& masterChain = master->getEffectChain();
                masterChain.prepare(pluginManager.getDefaultSampleRate(), pluginManager.getDefaultBlockSize());
                if (masterJson.has("effectChainStateHex") && masterJson["effectChainStateHex"].isString()) {
                    const auto effectState = hexToBytes(masterJson["effectChainStateHex"].asString());
                    std::vector<std::string> missingIds;
                    if (!effectState.empty() && !masterChain.loadState(effectState, pluginManager, &missingIds)) {
                        warningLimiter.warning(ProjectLoadWarningCategory::EffectChain,
                                               "[ProjectLoad] Failed to restore Master effect chain",
                                               "[ProjectLoad] Additional Master effect-chain warnings suppressed.");
                    }
                    for (auto& id : missingIds) {
                        result.missingPlugins.push_back({std::move(id), "Master"});
                    }
                }
            }
        }

        // 5. Load Playlist lanes and clips.
        // Clip ids restored from the file must stay unique — a hand-edited or
        // corrupted file with duplicates would silently break id lookups.
        std::unordered_set<AestraUUID> seenClipIds;
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
                // Restore the serialized lane identity so save→load→save is
                // id-stable (#446); invalid/missing ids fall back to minting.
                PlaylistLaneID storedLaneId;
                {
                    AestraUUID parsedLaneId;
                    if (AestraUUID::tryParse(boundedStringOr(lj[i], "id", "", PROJECT_MAX_STRING_BYTES),
                                             parsedLaneId)) {
                        storedLaneId = PlaylistLaneID(parsedLaneId);
                    }
                }
                PlaylistLaneID laneId = playlist.createLaneWithId(storedLaneId, laneName);
                if (!laneId.isValid()) {
                    warningLimiter.warning(
                        ProjectLoadWarningCategory::LaneCreate,
                        "[ProjectLoad] Failed to create lane '" + laneName + "' — skipping",
                        "[ProjectLoad] Additional lane creation failure warnings suppressed.");
                    continue;
                }
                MixerChannel* channel = nullptr;
                if (!hasIndependentMixerChannels) {
                    uint32_t storedMixerChannelId = 0;
                    if (lj[i].has("mixerChannelId") && lj[i]["mixerChannelId"].isNumber()) {
                        const double rawId = lj[i]["mixerChannelId"].asNumber();
                        if (std::isfinite(rawId) && rawId > 0.0 && rawId < static_cast<double>(UINT32_MAX)) {
                            storedMixerChannelId = static_cast<uint32_t>(rawId);
                        }
                    }
                    channel = trackManager->addChannelWithId(laneName, storedMixerChannelId);
                    if (!channel) {
                        warningLimiter.warning(
                            ProjectLoadWarningCategory::LaneCreateChannel,
                            "[ProjectLoad] Failed to create channel for legacy lane '" + laneName + "' — removing lane",
                            "[ProjectLoad] Additional legacy lane channel creation failures suppressed.");
                        playlist.removeLane(laneId);
                        continue;
                    }
                }
                if (auto* lane = playlist.getLane(laneId)) {
                    if (lj[i].has("color") && lj[i]["color"].isString()) {
                        try {
                            lane->colorRGBA = static_cast<uint32_t>(std::stoull(lj[i]["color"].asString()) & 0xFFFFFFFFu);
                        } catch (const std::exception&) {
                            lane->colorRGBA = 0xFFFFFFFF;
                        }
                    } else if (lj[i].has("color") && lj[i]["color"].isNumber()) {
                        lane->colorRGBA = static_cast<uint32_t>(std::llround(lj[i]["color"].asNumber()) & 0xFFFFFFFFu);
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

                        // MixerChannel state (not on PlaylistLane)
                        if (lj[i].has("soloSafe") && lj[i]["soloSafe"].isBool())
                            channel->setSoloSafe(lj[i]["soloSafe"].asBool());
                        if (lj[i].has("armed") && lj[i]["armed"].isBool())
                            channel->setArmed(lj[i]["armed"].asBool());
                        if (lj[i].has("monitorInput") && lj[i]["monitorInput"].isBool())
                            channel->setMonitoringEnabled(lj[i]["monitorInput"].asBool());
                        if (lj[i].has("inputChannelIndex") && lj[i]["inputChannelIndex"].isNumber()) {
                            const double raw = lj[i]["inputChannelIndex"].asNumber();
                            if (std::isfinite(raw)) {
                                channel->setInputChannelIndex(
                                    static_cast<int>(std::clamp(raw, -2.0, 1024.0)));
                            }
                        }
                        if (lj[i].has("width") && lj[i]["width"].isNumber()) {
                            const double raw = lj[i]["width"].asNumber();
                            if (std::isfinite(raw)) {
                                channel->setWidth(static_cast<float>(std::clamp(raw, 0.0, 4.0)));
                            }
                        }
                        if (lj[i].has("trackColorIndex") && lj[i]["trackColorIndex"].isNumber()) {
                            const double raw = lj[i]["trackColorIndex"].asNumber();
                            if (std::isfinite(raw)) {
                                channel->setTrackColorIndex(
                                    static_cast<int>(std::clamp(raw, -1.0, 1024.0)));
                            }
                        }

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
    
                        {
                            auto& pluginManager = PluginManager::getInstance();
                            auto& chain = channel->getEffectChain();
                            chain.prepare(pluginManager.getDefaultSampleRate(), pluginManager.getDefaultBlockSize());
                            if (lj[i].has("effectChainStateHex") && lj[i]["effectChainStateHex"].isString()) {
                                const auto effectState = hexToBytes(lj[i]["effectChainStateHex"].asString());
                                if (!effectState.empty()) {
                                    std::vector<std::string> missingIds;
                                    if (!chain.loadState(effectState, pluginManager, &missingIds)) {
                                        warningLimiter.warning(
                                            ProjectLoadWarningCategory::EffectChain,
                                            "[ProjectLoad] Failed to restore effect chain on lane: " + lane->name,
                                            "[ProjectLoad] Additional effect chain restore warnings suppressed.");
                                    }
                                    for (auto& id : missingIds) {
                                        result.missingPlugins.push_back({std::move(id), lane->name});
                                    }
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
                                    warningLimiter.warning(
                                        ProjectLoadWarningCategory::AutomationTarget,
                                        "[ProjectLoad] Automation curve '" + param +
                                            "' has unrecognized target enum " + std::to_string(rawTarget) +
                                            "; curve preserved but may be skipped at runtime.",
                                        "[ProjectLoad] Additional unrecognized automation target warnings suppressed.");
                                }
                            }
                            
                            AutomationCurve curve(param, target);
                            if (aj[a].has("mixerChannelId") && aj[a]["mixerChannelId"].isNumber()) {
                                const double rawMixerId = aj[a]["mixerChannelId"].asNumber();
                                if (std::isfinite(rawMixerId) && rawMixerId > 0.0 &&
                                    rawMixerId < static_cast<double>(UINT32_MAX)) {
                                    curve.mixerChannelId = static_cast<uint32_t>(rawMixerId);
                                }
                            } else if (channel) {
                                // Pre-v3 curves inherited their lane's paired insert.
                                curve.mixerChannelId = channel->getChannelId();
                            }
                            curve.setDefaultValue(finiteNumberOr(aj[a], "default", 0.0, -1.0e6, 1.0e6));
                            // Plugin-parameter address (Custom target). Bounded:
                            // slot to the effect-chain size, paramId defensively.
                            curve.effectSlot = static_cast<uint32_t>(finiteNumberOr(aj[a], "slot", 0.0, 0.0, 9.0));
                            curve.paramId = static_cast<uint32_t>(finiteNumberOr(aj[a], "paramId", 0.0, 0.0, 1.0e6));
                            // Automation Identity Contract: a v2 curve carries
                            // its exact instance id. A v1 curve (no key)
                            // migrates exactly once: the slot's minted instance
                            // id, resolved against the already-loaded chains.
                            // Empty slots yield 0 — the curve is preserved as
                            // dangling (diagnostic), never re-pointed.
                            if (aj[a].has("instanceId") && aj[a]["instanceId"].isString()) {
                                // Untrusted input: digits-only parse. std::stoull
                                // would accept "-1" (wraps to UINT64_MAX, which the
                                // chain refuses by design) and "12abc" (trailing
                                // junk silently attaching the curve to id 12).
                                const std::string rawId = aj[a]["instanceId"].asString();
                                const bool digitsOnly =
                                    !rawId.empty() &&
                                    std::all_of(rawId.begin(), rawId.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
                                if (digitsOnly) {
                                    curve.deviceInstanceId = std::stoull(rawId);
                                    if (curve.deviceInstanceId == std::numeric_limits<uint64_t>::max()) {
                                        warningLimiter.warning(
                                            ProjectLoadWarningCategory::AutomationTarget,
                                            "[ProjectLoad] Automation curve '" + param +
                                                "' carries the reserved max instance id; preserved as dangling.",
                                            "[ProjectLoad] Additional reserved-instance-id warnings suppressed.");
                                        curve.deviceInstanceId = 0;
                                    }
                                } else {
                                    warningLimiter.warning(
                                        ProjectLoadWarningCategory::AutomationTarget,
                                        "[ProjectLoad] Automation curve '" + param +
                                            "' carries a malformed instance id; preserved as dangling.",
                                        "[ProjectLoad] Additional malformed instance-id warnings suppressed.");
                                }
                            } else if (curve.getAutomationTarget() == Aestra::Audio::AutomationTarget::Custom) {
                                if (auto* targetChannel = trackManager->getChannelById(curve.mixerChannelId)) {
                                    curve.deviceInstanceId =
                                        targetChannel->getEffectChain().getSlotInstanceId(curve.effectSlot);
                                }
                                if (curve.deviceInstanceId == 0) {
                                    warningLimiter.warning(
                                        ProjectLoadWarningCategory::AutomationTarget,
                                        "[ProjectLoad] Automation curve '" + param +
                                            "' targets an empty insert slot; preserved as dangling.",
                                        "[ProjectLoad] Additional dangling automation curve warnings suppressed.");
                                }
                            }

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

                    // Migration: the pre-2026-08-14 demo automation curve
                    // (addDemoTracks-era) — channel 1 Volume, default 0.8,
                    // exactly the four points 0.5/1.0/0.2/0.8 at beats
                    // 0/4/8/12 — is dropped on load so projects saved before
                    // the demo-automation removal self-heal instead of
                    // silently automating channel 1 on every playback.
                    // Exact-shape match only: a real user curve is never
                    // touched unless it matches all characteristics.
                    if (!lane->automationCurves.empty()) {
                        auto& curve = lane->automationCurves.back();
                        const bool legacyDemoShape =
                            curve.getAutomationTarget() == Aestra::Audio::AutomationTarget::Volume &&
                            curve.mixerChannelId == 1 && std::abs(curve.getDefaultValue() - 0.8f) < 1e-3f &&
                            curve.getPoints().size() == 4;
                        if (legacyDemoShape) {
                            constexpr double kDemoBeats[4] = {0.0, 4.0, 8.0, 12.0};
                            constexpr double kDemoValues[4] = {0.5, 1.0, 0.2, 0.8};
                            bool shapeMatches = true;
                            for (size_t i = 0; i < 4; ++i) {
                                const auto& pt = curve.getPoints()[i];
                                if (std::abs(pt.beat - kDemoBeats[i]) > 1e-3 ||
                                    std::abs(static_cast<double>(pt.value) - kDemoValues[i]) > 1e-3) {
                                    shapeMatches = false;
                                    break;
                                }
                            }
                            if (shapeMatches) {
                                lane->automationCurves.pop_back();
                                warningLimiter.warning(
                                    ProjectLoadWarningCategory::LegacyDemoAutomation,
                                    "[ProjectLoad] Dropped the legacy demo automation curve on channel 1 "
                                    "(pre-0.7.0 demo data).",
                                    "[ProjectLoad] Additional legacy demo automation drops suppressed.");
                            }
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
                                if (clip.id.isValid() && !seenClipIds.insert(clip.id).second) {
                                    clip.id = ClipInstanceID(); // duplicate in file — mint below
                                }
                                clip.patternId = patternMap[oldPatId];
                                if (channel && legacyAudioPatternIds.count(clip.patternId.value) != 0) {
                                    const uint32_t legacyDestination = channel->getChannelId();
                                    auto& variants = legacyAudioRouteVariants[clip.patternId.value];
                                    auto variant = variants.find(legacyDestination);
                                    if (variant == variants.end()) {
                                        PatternID routedPattern = clip.patternId;
                                        if (!variants.empty()) {
                                            // A pre-v3 pattern carries no mixerChannelId, so one old
                                            // pattern used on several lanes has to become one pattern
                                            // per destination channel. This MANUFACTURES a pattern the
                                            // file does not contain and repoints the clip at it.
                                            //
                                            // That is a loader-side transformation under the contract
                                            // in ProjectMigrations.h: re-serializing now emits more
                                            // patterns than were read, so the document must be saved.
                                            // Flagged here rather than at the `else` branch below,
                                            // because that branch can only reach a manufactured
                                            // variant after this clone has already run.
                                            routedPattern = patternManager.clonePattern(clip.patternId);
                                        }
                                        if (routedPattern.isValid()) {
                                            if (routedPattern != clip.patternId) {
                                                ++result.legacyAudioPatternsSplit;
                                            }
                                            patternManager.setPatternMixerChannel(routedPattern, legacyDestination);
                                            variants.emplace(legacyDestination, routedPattern);
                                            clip.patternId = routedPattern;
                                        }
                                    } else {
                                        clip.patternId = variant->second;
                                    }
                                }
                                clip.sourceId = clip.patternId.value;
                                clip.startBeat = finiteNumberOr(cj[c], "start", 0.0, 0.0, 1000000.0);
                                const auto* loadedPattern = patternManager.getPattern(clip.patternId);
                                const bool isAudioClip = loadedPattern && loadedPattern->isAudio();
                                if (isAudioClip) {
                                    clip.durationSeconds =
                                        finiteNumberOr(cj[c], "durationSeconds", 0.0, 0.0, 1000000.0);
                                    if (clip.durationSeconds <= 0.0) {
                                        const double legacyDurationBeats =
                                            finiteNumberOr(cj[c], "duration", 0.0, 0.0, 1000000.0);
                                        clip.durationSeconds = legacyDurationBeats * 60.0 / std::max(result.tempo, 1.0);
                                    }
                                    const bool hadNegativeSourceOffset =
                                        cj[c].has("sourceOffsetSeconds") && cj[c]["sourceOffsetSeconds"].isNumber() &&
                                        std::isfinite(cj[c]["sourceOffsetSeconds"].asNumber()) &&
                                        cj[c]["sourceOffsetSeconds"].asNumber() < 0.0;
                                    if (hadNegativeSourceOffset) {
                                        clip.sourceOffsetSeconds = 0.0;
                                        ++result.negativeAudioClipOffsetsCorrected;
                                        const std::string clipReference = clip.id.toString();
                                        warningLimiter.warning(
                                            ProjectLoadWarningCategory::ClipTiming,
                                            "[ProjectLoad] Audio clip " + clipReference +
                                                " had a negative source offset; clamped to 0 because source material "
                                                "before time zero does not exist",
                                            "[ProjectLoad] Additional negative audio clip source-offset warnings "
                                            "suppressed.");
                                        if (!result.report) {
                                            result.report = std::make_unique<ProjectLoadReport>();
                                        }
                                        result.report->issues.push_back(
                                            {LoadIssueSeverity::Warning, "clip_timing",
                                             "Audio clip had a negative source offset; clamped to zero because source "
                                             "material before time zero does not exist",
                                             0, clipReference, laneName});
                                    } else {
                                        clip.sourceOffsetSeconds =
                                            finiteNumberOr(cj[c], "sourceOffsetSeconds", 0.0, 0.0, 1000000.0);
                                    }
                                    if (!hadNegativeSourceOffset && clip.sourceOffsetSeconds <= 0.0) {
                                        const double legacySourceOffsetBeats =
                                            finiteNumberOr(cj[c], "sourceOffset", 0.0, 0.0, 1000000.0);
                                        clip.sourceOffsetSeconds =
                                            legacySourceOffsetBeats * 60.0 / std::max(result.tempo, 1.0);
                                    }
                                    clip.durationBeats = playlist.secondsToBeats(clip.durationSeconds);
                                    clip.sourceOffset = playlist.secondsToBeats(clip.sourceOffsetSeconds);
                                } else {
                                    clip.durationBeats = finiteNumberOr(cj[c], "duration", 0.0, 0.0, 1000000.0);
                                    clip.sourceOffset = finiteNumberOr(cj[c], "sourceOffset", 0.0, 0.0, 1000000.0);
                                }
                                clip.name = boundedStringOr(cj[c], "name", "", PROJECT_MAX_STRING_BYTES);
                                if (cj[c].has("color") && cj[c]["color"].isString()) {
                                    try {
                                        clip.colorRGBA = static_cast<uint32_t>(std::stoull(cj[c]["color"].asString()) & 0xFFFFFFFFu);
                                    } catch (const std::exception&) {
                                        clip.colorRGBA = 0xFFFFFFFF;
                                    }
                                } else if (cj[c].has("color") && cj[c]["color"].isNumber()) {
                                    clip.colorRGBA = static_cast<uint32_t>(std::llround(cj[c]["color"].asNumber()) & 0xFFFFFFFFu);
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
                                    clip.edits.pitchSemitones = static_cast<float>(
                                        finiteNumberOr(ej, "pitchSemitones", 0.0, -24.0, 24.0));
                                    clip.edits.fadeInBeats = finiteNumberOr(ej, "fadeIn", 0.0, 0.0, 1000000.0);
                                    clip.edits.fadeOutBeats = finiteNumberOr(ej, "fadeOut", 0.0, 0.0, 1000000.0);
                                    clip.edits.sourceStart = finiteNumberOr(ej, "sourceStart", 0.0, 0.0, 1.0e15);
                                }
                                playlist.addClip(laneId, clip);
                            } else {
                                warningLimiter.warning(
                                    ProjectLoadWarningCategory::DroppedClip,
                                    "[ProjectLoad] Clip references missing pattern " + std::to_string(oldPatId) +
                                        " — clip dropped from lane '" + laneName + "'",
                                    "[ProjectLoad] Additional dropped clip missing-pattern warnings suppressed.");
                            }
                        }
                    }
                }
            }
        }
    
        // Resolve pre-stable-ID unit routes after all mixer channels exist.
        {
            std::vector<uint32_t> mixerChannelIds;
            mixerChannelIds.reserve(trackManager->getChannelCount());
            for (size_t ci = 0; ci < trackManager->getChannelCount(); ++ci) {
                if (const auto* channel = trackManager->getChannel(ci)) {
                    mixerChannelIds.push_back(channel->getChannelId());
                }
            }
            trackManager->getUnitManager().migrateLegacyMixerRoutes(mixerChannelIds);
            trackManager->getPatternManager().validateMixerChannels(mixerChannelIds);
        }

        // PHASE 7: Validate routing against the loaded channel set (Contract
        // I8/I10/D3/D4). Unresolved destinations are repaired, never silently
        // dropped later: mains reroute to master, dangling sends are removed,
        // and sends to master (illegal since D4) are removed. Every repair
        // produces a diagnostic.
        {
            std::unordered_set<uint32_t> validChannelIds;
            validChannelIds.insert(0xFFFFFFFFu); // master
            for (size_t ci = 0; ci < trackManager->getChannelCount(); ++ci) {
                if (auto* ch = trackManager->getChannel(ci)) {
                    validChannelIds.insert(ch->getChannelId());
                }
            }
            auto reportRoutingIssue = [&](const std::string& message, uint64_t objectId) {
                warningLimiter.warning(ProjectLoadWarningCategory::SendRoute, message,
                                       "[ProjectLoad] Additional routing repair warnings suppressed.");
                if (!result.report) {
                    result.report = std::make_unique<ProjectLoadReport>();
                }
                result.report->issues.push_back({LoadIssueSeverity::Warning, "routing", message, objectId, {}, {}});
            };

            for (size_t ci = 0; ci < trackManager->getChannelCount(); ++ci) {
                auto* channel = trackManager->getChannel(ci);
                if (!channel) continue;

                // Main output: dangling destinations fail safe to master.
                const uint32_t mainOutput = channel->getMainOutputId();
                if (mainOutput != 0xFFFFFFFFu && !validChannelIds.count(mainOutput)) {
                    channel->setMainOutputId(0xFFFFFFFFu);
                    reportRoutingIssue("[ProjectLoad] Main output of '" + channel->getName() +
                                           "' targets channel ID " + std::to_string(mainOutput) +
                                           " which does not exist; rerouted to Master.",
                                       channel->getChannelId());
                }

                // Sends: dangling targets and sends to master are removed.
                std::vector<AudioRoute> keptSends;
                for (const auto& send : channel->getSends()) {
                    if (send.targetChannelId == 0xFFFFFFFFu) {
                        reportRoutingIssue("[ProjectLoad] Send from '" + channel->getName() +
                                               "' targets Master; sends to master are illegal and were removed.",
                                           channel->getChannelId());
                        continue;
                    }
                    if (!validChannelIds.count(send.targetChannelId)) {
                        reportRoutingIssue("[ProjectLoad] Send from '" + channel->getName() +
                                               "' targets channel ID " + std::to_string(send.targetChannelId) +
                                               " which does not exist; send was removed.",
                                           channel->getChannelId());
                        continue;
                    }
                    keptSends.push_back(send);
                }
                if (keptSends.size() != channel->getSends().size()) {
                    channel->replaceSends(keptSends);
                }
            }
        }
    
        // PHASE 7b: Push loaded volume/pan to ContinuousParamBuffer.
        // setVolume/setPan writes to m_volume/m_pan and sends a command via
        // m_commandSink, but during load m_commandSink is null. The UIMixerPanel
        // fader reads from ContinuousParamBuffer (dB), so we must push explicitly.
        {
            auto* continuous = trackManager->getContinuousParams().get();
            auto* slotMap = trackManager->getChannelSlotMapRaw();
            if (continuous && slotMap) {
                for (size_t ci = 0; ci < trackManager->getChannelCount(); ++ci) {
                    auto* channel = trackManager->getChannel(ci);
                    if (!channel) continue;
                    const uint32_t slot = slotMap->getSlotIndex(channel->getChannelId());
                    if (slot >= ContinuousParamBuffer::MAX_SLOTS) continue;

                    const float linearVol = channel->getVolume();
                    const float faderDb = (linearVol <= 0.0f) ? -90.0f
                                          : 20.0f * std::log10(linearVol);
                    continuous->setFaderDb(slot, faderDb);
                    continuous->setPan(slot, channel->getPan());
                }
            }
        }

        // PHASE 8: Final rebind pass - verify all references
        #if defined(AESTRA_ENABLE_PROJECT_LOAD_LOGS)
        Log::info("[ProjectLoad] Final rebind pass");
        #endif
    
    } // end try (Phase 4 commit)
    catch (const std::exception& e) {
        result.errorMessage = std::string("Project load failed at Phase 4: ") + e.what();
        Log::error("[ProjectLoad] " + result.errorMessage);

        // Attempt to restore previous state from rollback file.
        if (!rollbackPath.empty()) {
            namespace fs = std::filesystem;
            Log::warning("[ProjectLoad] Attempting to restore previous state from rollback");
            try {
                LoadResult rollbackResult = load(rollbackPath, trackManager, path);
                if (rollbackResult.ok) {
                    Log::info("[ProjectLoad] Rollback successful — previous state restored");
                } else {
                    Log::error("[ProjectLoad] Rollback failed: " + rollbackResult.errorMessage);
                }
            } catch (const std::exception& restoreEx) {
                Log::error("[ProjectLoad] Rollback threw exception: " + std::string(restoreEx.what()));
            }
            // Clean up rollback file regardless of outcome
            std::error_code rmEc;
            fs::remove(rollbackPath, rmEc);
        }

        return result;
    }

    // Success — clean up rollback file
    if (!rollbackPath.empty()) {
        namespace fs = std::filesystem;
        std::error_code rmEc;
        fs::remove(rollbackPath, rmEc);
    }

    // A load has two independent sources of transformation: the migration
    // registry, and version-conditional interpretation inside this loader. The
    // reported outcome must be their combination — reporting only the registry's
    // verdict is exactly how a loader-side upgrade goes silent and the user is
    // never prompted to save the upgraded representation.
    //
    // Note this cannot be folded into the migration block above: the legacy
    // audio split happens in Phase 4, long after migrations run.
    if (result.legacyAudioPatternsSplit > 0) {
        result.migrationOutcome =
            combineMigrationOutcome(result.migrationOutcome, MigrationOutcome::Transformed);
        Log::info("[ProjectLoad] Split " + std::to_string(result.legacyAudioPatternsSplit) +
                  " legacy audio pattern(s) across mixer channels — project must be saved to keep the split");
    }
    if (result.negativeAudioClipOffsetsCorrected > 0) {
        result.migrationOutcome = combineMigrationOutcome(result.migrationOutcome, MigrationOutcome::Transformed);
        Log::warning("[ProjectLoad] Corrected " + std::to_string(result.negativeAudioClipOffsetsCorrected) +
                     " negative audio clip source offset(s) — project must be saved to keep the correction");
    }

    result.ok = true;
    Log::info("Project loaded: " + path);
    return result;
}

void ProjectSerializer::setHistoryLimits(size_t maxEntries, uintmax_t maxTotalBytes) {
    // A zero cap would delete all history (or all-but-newest); guard against it
    // so a misconfiguration can't silently disable history entirely.
    g_historyMaxEntries.store(maxEntries == 0 ? PROJECT_HISTORY_DEFAULT_MAX_ENTRIES : maxEntries,
                              std::memory_order_relaxed);
    g_historyMaxTotalBytes.store(maxTotalBytes == 0 ? PROJECT_HISTORY_DEFAULT_MAX_TOTAL_BYTES : maxTotalBytes,
                                 std::memory_order_relaxed);
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
