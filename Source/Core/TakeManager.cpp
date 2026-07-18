// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "TakeManager.h"

#include "AestraJSON.h"
#include "AestraLog.h"
#include "ProjectSerializer.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace {

constexpr int TAKE_MANIFEST_VERSION = 1;
constexpr size_t TAKE_MAX_ENTRIES = 256;
constexpr size_t TAKE_MAX_STRING_BYTES = 4096;
constexpr uintmax_t TAKE_MAX_MANIFEST_BYTES = 4ull * 1024ull * 1024ull;

std::atomic<uint64_t> g_takeIdCounter{1};

uint64_t nowMs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string boundedStringOr(const Aestra::JSON& object, const char* key, const std::string& fallback) {
    if (!object.has(key) || !object[key].isString()) {
        return fallback;
    }
    const std::string& value = object[key].asString();
    if (value.size() > TAKE_MAX_STRING_BYTES) {
        return value.substr(0, TAKE_MAX_STRING_BYTES);
    }
    return value;
}

uint64_t boundedUint64Or(const Aestra::JSON& object, const char* key, uint64_t fallback) {
    if (!object.has(key) || !object[key].isNumber()) {
        return fallback;
    }
    const double value = object[key].asNumber();
    if (!std::isfinite(value) || value < 0.0 || value > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
        return fallback;
    }
    return static_cast<uint64_t>(value);
}

std::string sanitizeIdPart(const std::string& input) {
    if (input.empty()) {
        return {};
    }

    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    return out.empty() ? "take" : out;
}

std::string makeTakeId() {
    std::ostringstream oss;
    oss << "take_" << std::hex << nowMs() << "_" << g_takeIdCounter.fetch_add(1, std::memory_order_relaxed);
    return oss.str();
}

std::string makeSnapshotFilename(const std::string& takeId) {
    return sanitizeIdPart(takeId) + ".aes";
}

bool ensureDirectoryForProject(const std::string& projectPath, std::string& error) {
    namespace fs = std::filesystem;
    const fs::path takesDir = TakeManager::getTakesDirectory(projectPath);
    if (takesDir.empty()) {
        error = "Project path is empty";
        return false;
    }

    std::error_code ec;
    fs::create_directories(takesDir, ec);
    if (ec) {
        error = "Could not create Takes directory: " + ec.message();
        return false;
    }
    return true;
}

std::filesystem::path resolveManifestSnapshotPath(const std::string& projectPath, const std::string& snapshotPath) {
    namespace fs = std::filesystem;
    fs::path path(snapshotPath);
    if (path.is_absolute()) {
        return {};
    }
    const fs::path takesDir = fs::path(TakeManager::getTakesDirectory(projectPath)).lexically_normal();
    path = (takesDir / path).lexically_normal();

    std::string takesDirStr = takesDir.generic_string();
    std::string pathStr = path.generic_string();
#ifdef _WIN32
    std::transform(takesDirStr.begin(), takesDirStr.end(), takesDirStr.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    while (takesDirStr.size() > 1 && takesDirStr.back() == '/') {
        takesDirStr.pop_back();
    }
    if (pathStr.size() > 2 && pathStr.compare(pathStr.size() - 2, 2, "/.") == 0) {
        pathStr.resize(pathStr.size() - 2);
    }
    while (pathStr.size() > 1 && pathStr.back() == '/') {
        pathStr.pop_back();
    }
    if (pathStr == takesDirStr) {
        return {};
    }
    if (pathStr != takesDirStr && (pathStr.size() <= takesDirStr.size() ||
                                   pathStr.compare(0, takesDirStr.size(), takesDirStr) != 0 ||
                                   pathStr[takesDirStr.size()] != '/')) {
        return {};
    }
    return path;
}

TakeManager::Manifest makeErrorManifest(const std::string& projectPath, const std::string& error) {
    TakeManager::Manifest manifest;
    manifest.ok = false;
    manifest.projectPath = projectPath;
    manifest.errorMessage = error;
    return manifest;
}

Aestra::JSON takeToJson(const TakeManager::TakeEntry& take) {
    Aestra::JSON json = Aestra::JSON::object();
    json.set("id", Aestra::JSON(take.id));
    json.set("name", Aestra::JSON(take.name));
    json.set("parentId", Aestra::JSON(take.parentId));
    json.set("snapshotPath", Aestra::JSON(take.snapshotPath));
    json.set("createdAtMs", Aestra::JSON(static_cast<double>(take.createdAtMs)));
    json.set("updatedAtMs", Aestra::JSON(static_cast<double>(take.updatedAtMs)));
    return json;
}

Aestra::JSON manifestToJson(const TakeManager::Manifest& manifest) {
    Aestra::JSON root = Aestra::JSON::object();
    root.set("version", Aestra::JSON(static_cast<double>(TAKE_MANIFEST_VERSION)));
    root.set("projectPath", Aestra::JSON(std::filesystem::path(manifest.projectPath).generic_string()));
    root.set("activeTakeId", Aestra::JSON(manifest.activeTakeId));

    Aestra::JSON takes = Aestra::JSON::array();
    for (const auto& take : manifest.takes) {
        takes.push(takeToJson(take));
    }
    root.set("takes", takes);
    return root;
}

bool saveManifest(const TakeManager::Manifest& manifest, std::string& error) {
    if (manifest.projectPath.empty()) {
        error = "Project path is empty";
        return false;
    }

    const Aestra::JSON root = manifestToJson(manifest);
    if (!ProjectSerializer::writeAtomically(TakeManager::getManifestPath(manifest.projectPath), root.toString(2))) {
        error = "Could not write Takes manifest";
        return false;
    }
    return true;
}

TakeManager::Result makeResult(bool ok, const std::string& error, const TakeManager::Manifest& manifest,
                               const TakeManager::TakeEntry& take = {}) {
    TakeManager::Result result;
    result.ok = ok;
    result.errorMessage = error;
    result.manifest = manifest;
    result.take = take;
    return result;
}

bool writeSnapshot(const std::string& projectPath, const TakeManager::TakeEntry& take, const std::string& contents,
                   std::string& error) {
    if (contents.empty()) {
        error = "Cannot write an empty take snapshot";
        return false;
    }

    const std::string snapshotPath = TakeManager::resolveSnapshotPath(projectPath, take);
    if (snapshotPath.empty()) {
        error = "Take snapshot path is empty";
        return false;
    }

    if (!ProjectSerializer::writeAtomically(snapshotPath, contents)) {
        error = "Could not write take snapshot: " + snapshotPath;
        return false;
    }
    return true;
}

} // namespace

const TakeManager::TakeEntry* TakeManager::Manifest::findTake(const std::string& id) const {
    for (const auto& take : takes) {
        if (take.id == id) {
            return &take;
        }
    }
    return nullptr;
}

const TakeManager::TakeEntry* TakeManager::Manifest::activeTake() const {
    return findTake(activeTakeId);
}

std::string TakeManager::getTakesDirectory(const std::string& projectPath) {
    namespace fs = std::filesystem;
    if (projectPath.empty()) {
        return {};
    }
    const fs::path project(projectPath);
    return (project.parent_path() / (project.stem().string() + ".takes")).string();
}

std::string TakeManager::getManifestPath(const std::string& projectPath) {
    namespace fs = std::filesystem;
    const fs::path dir(getTakesDirectory(projectPath));
    if (dir.empty()) {
        return {};
    }
    return (dir / "manifest.json").string();
}

std::string TakeManager::resolveSnapshotPath(const std::string& projectPath, const TakeEntry& take) {
    if (projectPath.empty() || take.snapshotPath.empty()) {
        return {};
    }
    return resolveManifestSnapshotPath(projectPath, take.snapshotPath).string();
}

TakeManager::Manifest TakeManager::loadManifest(const std::string& projectPath) {
    namespace fs = std::filesystem;

    Manifest manifest;
    manifest.projectPath = projectPath;

    if (projectPath.empty()) {
        return makeErrorManifest(projectPath, "Project path is empty");
    }

    const fs::path manifestPath(getManifestPath(projectPath));
    std::error_code ec;
    if (!fs::exists(manifestPath, ec) || ec) {
        return makeErrorManifest(projectPath, "No Takes manifest");
    }

    const auto size = fs::file_size(manifestPath, ec);
    if (ec || size > TAKE_MAX_MANIFEST_BYTES) {
        return makeErrorManifest(projectPath, "Takes manifest is too large or unreadable");
    }

    std::ifstream in(manifestPath, std::ios::binary);
    if (!in) {
        return makeErrorManifest(projectPath, "Could not open Takes manifest");
    }

    const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Aestra::JSON root = Aestra::JSON::parse(contents);
    if (!root.isObject()) {
        return makeErrorManifest(projectPath, "Takes manifest is not valid JSON");
    }

    const int version = [&]() -> int {
        if (!root.has("version") || !root["version"].isNumber()) {
            return 0;
        }
        const double v = root["version"].asNumber();
        if (!std::isfinite(v) || v < 1.0 || v > static_cast<double>(std::numeric_limits<int>::max())) {
            return 0;
        }
        return static_cast<int>(v);
    }();
    if (version < 1 || version > TAKE_MANIFEST_VERSION) {
        return makeErrorManifest(projectPath, "Unsupported Takes manifest version");
    }

    manifest.activeTakeId = boundedStringOr(root, "activeTakeId", "");

    if (!root.has("takes") || !root["takes"].isArray()) {
        return makeErrorManifest(projectPath, "Takes manifest is missing takes array");
    }

    const Aestra::JSON& takesJson = root["takes"];
    if (takesJson.size() > TAKE_MAX_ENTRIES) {
        return makeErrorManifest(projectPath, "Takes manifest exceeds maximum take count");
    }

    for (size_t i = 0; i < takesJson.size(); ++i) {
        const Aestra::JSON& item = takesJson[i];
        if (!item.isObject()) {
            continue;
        }

        TakeEntry take;
        take.id = sanitizeIdPart(boundedStringOr(item, "id", ""));
        take.name = boundedStringOr(item, "name", take.id.empty() ? "Take" : take.id);
        take.parentId = sanitizeIdPart(boundedStringOr(item, "parentId", ""));
        take.snapshotPath = boundedStringOr(item, "snapshotPath", makeSnapshotFilename(take.id));
        take.createdAtMs = boundedUint64Or(item, "createdAtMs", 0);
        take.updatedAtMs = boundedUint64Or(item, "updatedAtMs", take.createdAtMs);
        take.active = !take.id.empty() && take.id == manifest.activeTakeId;

        if (!take.id.empty()) {
            manifest.takes.push_back(std::move(take));
        }
    }

    if (manifest.takes.empty()) {
        return makeErrorManifest(projectPath, "Takes manifest contains no valid takes");
    }

    if (!manifest.findTake(manifest.activeTakeId)) {
        manifest.activeTakeId = manifest.takes.front().id;
    }
    for (auto& take : manifest.takes) {
        take.active = take.id == manifest.activeTakeId;
    }

    manifest.ok = true;
    return manifest;
}

TakeManager::Result TakeManager::ensureManifest(const std::string& projectPath,
                                                const std::string& currentProjectContents,
                                                const std::string& initialTakeName) {
    std::string error;
    if (!ensureDirectoryForProject(projectPath, error)) {
        return makeResult(false, error, makeErrorManifest(projectPath, error));
    }

    Manifest existing = loadManifest(projectPath);
    if (existing.ok) {
        bool changed = false;
        if (!existing.findTake(existing.activeTakeId) && !existing.takes.empty()) {
            existing.activeTakeId = existing.takes.front().id;
            changed = true;
        }
        for (auto& take : existing.takes) {
            take.active = take.id == existing.activeTakeId;
        }
        if (changed && !saveManifest(existing, error)) {
            return makeResult(false, error, existing);
        }
        return makeResult(true, "", existing, existing.activeTake() ? *existing.activeTake() : TakeEntry{});
    }

    // Only bootstrap a new manifest when the file simply doesn't exist.
    // Any other load error (corrupt JSON, too large, etc.) is a real failure.
    if (existing.errorMessage != "No Takes manifest") {
        return makeResult(false, existing.errorMessage, existing);
    }

    Manifest manifest;
    manifest.ok = true;
    manifest.projectPath = projectPath;

    TakeEntry mainTake;
    mainTake.id = "main";
    mainTake.name = initialTakeName.empty() ? "Main" : initialTakeName;
    mainTake.snapshotPath = makeSnapshotFilename(mainTake.id);
    mainTake.createdAtMs = nowMs();
    mainTake.updatedAtMs = mainTake.createdAtMs;
    mainTake.active = true;

    manifest.activeTakeId = mainTake.id;
    manifest.takes.push_back(mainTake);

    if (!writeSnapshot(projectPath, mainTake, currentProjectContents, error)) {
        return makeResult(false, error, manifest);
    }
    if (!saveManifest(manifest, error)) {
        return makeResult(false, error, manifest);
    }

    Aestra::Log::info("[Takes] Initialized Takes for project: " + projectPath);
    return makeResult(true, "", manifest, mainTake);
}

TakeManager::Result TakeManager::saveActiveTake(const std::string& projectPath,
                                                const std::string& currentProjectContents) {
    Result ensured = ensureManifest(projectPath, currentProjectContents);
    if (!ensured.ok) {
        return ensured;
    }

    Manifest manifest = ensured.manifest;
    TakeEntry* active = nullptr;
    for (auto& take : manifest.takes) {
        if (take.id == manifest.activeTakeId) {
            active = &take;
            break;
        }
    }
    if (!active) {
        return makeResult(false, "No active take", manifest);
    }

    std::string error;
    active->updatedAtMs = nowMs();
    active->active = true;
    if (!writeSnapshot(projectPath, *active, currentProjectContents, error)) {
        return makeResult(false, error, manifest, *active);
    }
    if (!saveManifest(manifest, error)) {
        return makeResult(false, error, manifest, *active);
    }

    return makeResult(true, "", manifest, *active);
}

TakeManager::Result TakeManager::createTake(const std::string& projectPath, const std::string& currentProjectContents,
                                            const std::string& name, const std::string& parentId) {
    Result ensured = ensureManifest(projectPath, currentProjectContents);
    if (!ensured.ok) {
        return ensured;
    }

    Manifest manifest = ensured.manifest;
    if (manifest.takes.size() >= TAKE_MAX_ENTRIES) {
        return makeResult(false, "Maximum number of takes reached (" + std::to_string(TAKE_MAX_ENTRIES) + ")", manifest);
    }
    const std::string effectiveParentId = parentId.empty() ? manifest.activeTakeId : sanitizeIdPart(parentId);

    TakeEntry take;
    take.id = makeTakeId();
    take.name = name.empty() ? ("Take " + std::to_string(manifest.takes.size() + 1)) : name;
    take.parentId = effectiveParentId;
    take.snapshotPath = makeSnapshotFilename(take.id);
    take.createdAtMs = nowMs();
    take.updatedAtMs = take.createdAtMs;
    take.active = true;

    for (auto& existing : manifest.takes) {
        existing.active = false;
    }
    manifest.activeTakeId = take.id;

    std::string error;
    if (!writeSnapshot(projectPath, take, currentProjectContents, error)) {
        return makeResult(false, error, manifest, take);
    }

    manifest.takes.push_back(take);
    if (!saveManifest(manifest, error)) {
        return makeResult(false, error, manifest, take);
    }

    Aestra::Log::info("[Takes] Created take '" + take.name + "' for project: " + projectPath);
    return makeResult(true, "", manifest, take);
}

TakeManager::Result TakeManager::setActiveTake(const std::string& projectPath, const std::string& takeId) {
    Manifest manifest = loadManifest(projectPath);
    if (!manifest.ok) {
        return makeResult(false, manifest.errorMessage, manifest);
    }

    const std::string sanitizedId = sanitizeIdPart(takeId);
    const TakeEntry* selected = manifest.findTake(sanitizedId);
    if (!selected) {
        return makeResult(false, "Take not found: " + takeId, manifest);
    }

    manifest.activeTakeId = sanitizedId;
    TakeEntry selectedCopy = *selected;
    for (auto& take : manifest.takes) {
        take.active = take.id == manifest.activeTakeId;
        if (take.active) {
            selectedCopy = take;
        }
    }

    std::string error;
    if (!saveManifest(manifest, error)) {
        return makeResult(false, error, manifest, selectedCopy);
    }

    return makeResult(true, "", manifest, selectedCopy);
}

TakeManager::Result TakeManager::renameTake(const std::string& projectPath, const std::string& takeId,
                                            const std::string& newName) {
    Manifest manifest = loadManifest(projectPath);
    if (!manifest.ok) {
        return makeResult(false, manifest.errorMessage, manifest);
    }

    std::string trimmedName = newName;
    trimmedName.erase(0, trimmedName.find_first_not_of(" \t\r\n"));
    trimmedName.erase(trimmedName.find_last_not_of(" \t\r\n") + 1);
    if (trimmedName.empty()) {
        return makeResult(false, "Take name cannot be empty", manifest);
    }
    if (trimmedName.size() > TAKE_MAX_STRING_BYTES) {
        trimmedName.resize(TAKE_MAX_STRING_BYTES);
    }

    const std::string sanitizedId = sanitizeIdPart(takeId);
    TakeEntry* target = nullptr;
    for (auto& take : manifest.takes) {
        if (take.id == sanitizedId) {
            target = &take;
            break;
        }
    }
    if (!target) {
        return makeResult(false, "Take not found: " + takeId, manifest);
    }

    target->name = trimmedName;
    target->updatedAtMs = nowMs();

    std::string error;
    if (!saveManifest(manifest, error)) {
        return makeResult(false, error, manifest, *target);
    }
    return makeResult(true, "", manifest, *target);
}

TakeManager::Result TakeManager::duplicateTake(const std::string& projectPath, const std::string& takeId,
                                               const std::string& newName) {
    namespace fs = std::filesystem;

    Manifest manifest = loadManifest(projectPath);
    if (!manifest.ok) {
        return makeResult(false, manifest.errorMessage, manifest);
    }
    if (manifest.takes.size() >= TAKE_MAX_ENTRIES) {
        return makeResult(false, "Maximum number of takes reached (" + std::to_string(TAKE_MAX_ENTRIES) + ")", manifest);
    }

    const std::string sanitizedId = sanitizeIdPart(takeId);
    const TakeEntry* source = manifest.findTake(sanitizedId);
    if (!source) {
        return makeResult(false, "Take not found: " + takeId, manifest);
    }

    const std::string sourceSnapshot = resolveSnapshotPath(projectPath, *source);
    std::error_code ec;
    if (sourceSnapshot.empty() || !fs::exists(sourceSnapshot, ec) || ec) {
        return makeResult(false, "Source take snapshot is missing: " + source->id, manifest);
    }

    TakeEntry copy;
    copy.id = makeTakeId();
    copy.name = newName.empty() ? (source->name + " Copy") : newName;
    copy.parentId = source->id;
    copy.snapshotPath = makeSnapshotFilename(copy.id);
    copy.createdAtMs = nowMs();
    copy.updatedAtMs = copy.createdAtMs;
    copy.active = false;

    const std::string copySnapshot = resolveSnapshotPath(projectPath, copy);
    if (copySnapshot.empty()) {
        return makeResult(false, "Could not resolve duplicate snapshot path", manifest);
    }
    fs::copy_file(sourceSnapshot, copySnapshot, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return makeResult(false, "Could not copy take snapshot: " + ec.message(), manifest);
    }

    const std::string sourceName = source->name; // push_back below may reallocate and dangle `source`
    manifest.takes.push_back(copy);
    std::string error;
    if (!saveManifest(manifest, error)) {
        fs::remove(copySnapshot, ec); // best effort: don't leave an orphan snapshot
        return makeResult(false, error, manifest, copy);
    }

    Aestra::Log::info("[Takes] Duplicated take '" + sourceName + "' as '" + copy.name + "'");
    return makeResult(true, "", manifest, copy);
}

TakeManager::Result TakeManager::deleteTake(const std::string& projectPath, const std::string& takeId) {
    namespace fs = std::filesystem;

    Manifest manifest = loadManifest(projectPath);
    if (!manifest.ok) {
        return makeResult(false, manifest.errorMessage, manifest);
    }

    const std::string sanitizedId = sanitizeIdPart(takeId);
    const TakeEntry* target = manifest.findTake(sanitizedId);
    if (!target) {
        return makeResult(false, "Take not found: " + takeId, manifest);
    }
    if (sanitizedId == manifest.activeTakeId) {
        return makeResult(false, "Cannot delete the active take — switch to another take first", manifest, *target);
    }

    TakeEntry deleted = *target;
    const std::string snapshotPath = resolveSnapshotPath(projectPath, deleted);

    // Children keep their history by moving up to the deleted take's parent.
    for (auto& take : manifest.takes) {
        if (take.parentId == deleted.id) {
            take.parentId = deleted.parentId;
        }
    }
    manifest.takes.erase(std::remove_if(manifest.takes.begin(), manifest.takes.end(),
                                        [&deleted](const TakeEntry& take) { return take.id == deleted.id; }),
                         manifest.takes.end());

    std::string error;
    if (!saveManifest(manifest, error)) {
        return makeResult(false, error, manifest, deleted);
    }

    // Snapshot removal is best-effort: an orphan file is harmless, a manifest
    // pointing at a deleted file is not — so the manifest write comes first.
    if (!snapshotPath.empty()) {
        std::error_code ec;
        fs::remove(snapshotPath, ec);
    }

    Aestra::Log::info("[Takes] Deleted take '" + deleted.name + "'");
    return makeResult(true, "", manifest, deleted);
}
