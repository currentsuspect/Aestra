// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UISurfaceStore.h"

#include "AestraJSONFile.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Aestra {

namespace {

constexpr const char* kAppDataFileName = "ui_surface_store.json";

/// editor.* entries older than this are pruned on load — long enough that
/// returning to a project after a few months is not punished, short enough
/// to actually bound growth. panel.* is a fixed 3-key set and dialog.export
/// a single key; neither namespace accumulates, so neither is pruned.
constexpr int64_t kEditorEntryMaxAgeSeconds = 180LL * 24 * 60 * 60;

/// Clock-independent backstop alongside the age cutoff, in case a system
/// clock is wrong or a project is reopened constantly forever.
constexpr size_t kEditorEntryMaxCount = 2000;

bool isEditorKey(const std::string& key) {
    return key.rfind("editor.", 0) == 0;
}

double clamp01(double v) {
    if (!std::isfinite(v)) {
        return 0.0;
    }
    return std::max(0.0, std::min(1.0, v));
}

JSON geometryToJson(const UISurfaceGeometry& g) {
    JSON obj = JSON::object();
    obj.set("anchorX", JSON(g.anchorX));
    obj.set("anchorY", JSON(g.anchorY));
    obj.set("width", JSON(g.width));
    obj.set("height", JSON(g.height));
    obj.set("maximized", JSON(g.maximized));
    obj.set("lastUsedAt", JSON(static_cast<double>(g.lastUsedAt)));
    return obj;
}

/// Type-checked, not merely present — a wrong-typed field falls back to the
/// UISurfaceGeometry default for that field — absent and wrong-typed land in
/// the same bucket — so a stray {"anchorX": "nan"} cannot corrupt geometry silently.
UISurfaceGeometry geometryFromJson(const JSON& obj) {
    UISurfaceGeometry g;
    const auto readAnchor = [&obj](const char* key, double& out) {
        if (obj.has(key) && obj[key].isNumber() && std::isfinite(obj[key].asNumber())) {
            out = clamp01(obj[key].asNumber());
        }
    };
    readAnchor("anchorX", g.anchorX);
    readAnchor("anchorY", g.anchorY);

    // Pixels, not fractions. Nothing is clamped to fit: an oversized preference is
    // still the user's preference, and resolution shrinks only what is displayed.
    // Beyond any real display (or non-finite, or not positive) is corrupt data.
    // static: MSVC rejects using a non-static constexpr local inside a lambda without capturing it (C3493).
    static constexpr double kMaxSurfaceExtent = 16384.0;
    const auto readExtent = [&obj](const char* key, double& out) {
        if (obj.has(key) && obj[key].isNumber()) {
            const double v = obj[key].asNumber();
            if (std::isfinite(v) && v > 0.0 && v <= kMaxSurfaceExtent) {
                out = v;
            }
        }
    };
    readExtent("width", g.width);
    readExtent("height", g.height);

    if (obj.has("maximized") && obj["maximized"].isBool()) {
        g.maximized = obj["maximized"].asBool();
    }
    if (obj.has("lastUsedAt") && obj["lastUsedAt"].isNumber()) {
        constexpr double kMaxLastUsedAt = 9007199254740992.0; // 2^53: exact in a double, so no cast UB or prune overflow.
        const double raw = obj["lastUsedAt"].asNumber();
        if (std::isfinite(raw) && std::trunc(raw) == raw && raw >= 0.0 && raw <= kMaxLastUsedAt) {
            g.lastUsedAt = static_cast<int64_t>(raw);
        }
    }
    return g;
}

JSON dialogExportToJson(const UIDialogExportOptions& d) {
    JSON obj = JSON::object();
    obj.set("sampleRateIndex", JSON(static_cast<double>(d.sampleRateIndex)));
    obj.set("bitDepthIndex", JSON(static_cast<double>(d.bitDepthIndex)));
    obj.set("scopeIndex", JSON(static_cast<double>(d.scopeIndex)));
    obj.set("tailInput", JSON(d.tailInput));
    obj.set("lastOutputDirectory", JSON(d.lastOutputDirectory));
    return obj;
}

UIDialogExportOptions dialogExportFromJson(const JSON& obj) {
    UIDialogExportOptions d;
    if (obj.has("sampleRateIndex") && obj["sampleRateIndex"].isNumber()) {
        d.sampleRateIndex = obj["sampleRateIndex"].asInt();
    }
    if (obj.has("bitDepthIndex") && obj["bitDepthIndex"].isNumber()) {
        d.bitDepthIndex = obj["bitDepthIndex"].asInt();
    }
    if (obj.has("scopeIndex") && obj["scopeIndex"].isNumber()) {
        d.scopeIndex = obj["scopeIndex"].asInt();
    }
    if (obj.has("tailInput") && obj["tailInput"].isString()) {
        d.tailInput = obj["tailInput"].asString();
    }
    if (obj.has("lastOutputDirectory") && obj["lastOutputDirectory"].isString()) {
        d.lastOutputDirectory = obj["lastOutputDirectory"].asString();
    }
    return d;
}

/// The version-gate mechanism named by the founder's schema-versioning open
/// question: today the only branch is the trivial identity, but the branch
/// point exists now so the first real schema bump is not also the first
/// compatibility break. `root` is mutated in place; unrecognized fields
/// elsewhere in the document are left alone (parsed past, never round-tripped
/// back out — see UISurfaceStore::load's fromJson pass for why).
JSON migrateToCurrent(JSON root, int fromVersion) {
    if (fromVersion == UISurfaceStore::kCurrentSchemaVersion) {
        return root;
    }
    // No prior versions exist yet; anything else is unrecognized/foreign and
    // is handled by the caller falling back to defaults before this is ever
    // reached (see UISurfaceStore::load).
    return root;
}

/// Drop stale editor.* entries: age cutoff first, then an oldest-first count
/// cap as a clock-independent backstop. panel.* and dialog.export are never
/// touched — this must only ever look at the editor.* namespace.
void pruneStaleEditorEntries(std::map<std::string, UISurfaceGeometry>& surfaces, int64_t nowSeconds) {
    for (auto it = surfaces.begin(); it != surfaces.end();) {
        if (isEditorKey(it->first) && (nowSeconds - it->second.lastUsedAt) > kEditorEntryMaxAgeSeconds) {
            it = surfaces.erase(it);
        } else {
            ++it;
        }
    }

    std::vector<std::string> editorKeys;
    for (const auto& [key, geom] : surfaces) {
        if (isEditorKey(key)) {
            editorKeys.push_back(key);
        }
    }
    if (editorKeys.size() <= kEditorEntryMaxCount) {
        return;
    }
    std::sort(editorKeys.begin(), editorKeys.end(), [&surfaces](const std::string& a, const std::string& b) {
        return surfaces.at(a).lastUsedAt < surfaces.at(b).lastUsedAt;
    });
    const size_t excess = editorKeys.size() - kEditorEntryMaxCount;
    for (size_t i = 0; i < excess; ++i) {
        surfaces.erase(editorKeys[i]);
    }
}

int64_t nowUnixSeconds() {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

} // namespace

std::string UISurfaceStore::defaultPath() {
    return resolveAppDataFilePath("Aestra", kAppDataFileName);
}

UISurfaceStore UISurfaceStore::load(const std::string& path) {
    UISurfaceStore store;

    const std::optional<JSON> parsed = readJSONStrict(path);
    if (!parsed.has_value()) {
        return store; // Missing, unreadable, empty, or malformed: defaults.
    }
    const JSON& root = *parsed;
    if (!root.isObject()) {
        return store;
    }

    if (!root.has("schemaVersion") || !root["schemaVersion"].isNumber()) {
        return store; // No recognizable version at all: treat as foreign/corrupt.
    }
    // Validate as a double first: asInt() is a bare static_cast, so 0.5 or -1 would pass as "older".
    const double rawVersion = root["schemaVersion"].asNumber();
    if (!std::isfinite(rawVersion) || std::trunc(rawVersion) != rawVersion || rawVersion < 1.0 ||
        rawVersion > static_cast<double>(kCurrentSchemaVersion)) {
        return store; // Invalid, pre-history, or from a future build: defaults.
    }
    const int fromVersion = static_cast<int>(rawVersion);

    // Non-const on purpose: JSON::asObject() const always returns a static
    // empty map regardless of content (AestraJSON.h's const overload is a
    // stub), while the non-const overload returns the real data. Nothing
    // else in the tree hits this because nothing else iterates a JSON object
    // by dynamic key -- editor.<id> is the first key shape that needs to.
    JSON migrated = migrateToCurrent(root, fromVersion);
    store.schemaVersion = kCurrentSchemaVersion;

    if (migrated.has("surfaces") && migrated["surfaces"].isObject()) {
        for (const auto& [key, value] : migrated["surfaces"].asObject()) {
            if (value.isObject()) {
                store.surfaces[key] = geometryFromJson(value);
            }
        }
    }

    if (migrated.has("dialogExport") && migrated["dialogExport"].isObject()) {
        store.dialogExport = dialogExportFromJson(migrated["dialogExport"]);
    }

    if (migrated.has("boolPreferences") && migrated["boolPreferences"].isObject()) {
        for (const auto& [key, value] : migrated["boolPreferences"].asObject()) {
            if (value.isBool()) {
                store.boolPreferences[key] = value.asBool();
            }
        }
    }

    if (migrated.has("listPreferences") && migrated["listPreferences"].isObject()) {
        // `value` non-const, not `const auto&`: JSON::asArray() const is the
        // same always-empty stub as asObject() const (see the comment on
        // `migrated` above) -- only the non-const overload returns real data.
        for (auto& [key, value] : migrated["listPreferences"].asObject()) {
            if (!value.isArray()) {
                continue;
            }
            std::vector<std::string> items;
            for (const JSON& item : value.asArray()) {
                if (item.isString()) {
                    items.push_back(item.asString());
                }
            }
            store.listPreferences[key] = std::move(items);
        }
    }

    pruneStaleEditorEntries(store.surfaces, nowUnixSeconds());

    return store;
}

bool UISurfaceStore::save(const std::string& path) const {
    if (path.empty()) {
        return false;
    }

    JSON root = JSON::object();
    root.set("schemaVersion", JSON(static_cast<double>(schemaVersion)));

    JSON surfacesJson = JSON::object();
    for (const auto& [key, geom] : surfaces) {
        surfacesJson.set(key, geometryToJson(geom));
    }
    root.set("surfaces", surfacesJson);

    if (dialogExport.has_value()) {
        root.set("dialogExport", dialogExportToJson(*dialogExport));
    }

    JSON boolPrefsJson = JSON::object();
    for (const auto& [key, value] : boolPreferences) {
        boolPrefsJson.set(key, JSON(value));
    }
    root.set("boolPreferences", boolPrefsJson);

    JSON listPrefsJson = JSON::object();
    for (const auto& [key, values] : listPreferences) {
        JSON arr = JSON::array();
        for (const std::string& item : values) {
            arr.push(JSON(item));
        }
        listPrefsJson.set(key, arr);
    }
    root.set("listPreferences", listPrefsJson);

    return writeJSONAtomic(path, root);
}

UISurfaceStoreFile::UISurfaceStoreFile(std::string path)
    : m_store(UISurfaceStore::load(path)), m_path(std::move(path)) {}

std::optional<bool> UISurfaceStoreFile::boolPreference(const std::string& key) const {
    const auto it = m_store.boolPreferences.find(key);
    if (it == m_store.boolPreferences.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool UISurfaceStoreFile::setBoolPreference(const std::string& key, bool value) {
    const auto it = m_store.boolPreferences.find(key);
    if (it != m_store.boolPreferences.end() && it->second == value) {
        return true;
    }
    m_store.boolPreferences[key] = value;
    if (m_path.empty()) {
        return true; // In-memory only: no write was attempted, so none failed.
    }
    return m_store.save(m_path);
}

std::optional<UISurfaceGeometry> UISurfaceStoreFile::surfaceGeometry(const std::string& key) const {
    const auto it = m_store.surfaces.find(key);
    if (it == m_store.surfaces.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool UISurfaceStoreFile::setSurfaceGeometry(const std::string& key, const UISurfaceGeometry& value) {
    const auto it = m_store.surfaces.find(key);
    if (it != m_store.surfaces.end()) {
        const UISurfaceGeometry& held = it->second;
        if (held.anchorX == value.anchorX && held.anchorY == value.anchorY && held.width == value.width &&
            held.height == value.height && held.maximized == value.maximized) {
            return true;
        }
    }
    m_store.surfaces[key] = value;
    if (m_path.empty()) {
        return true; // In-memory only: no write was attempted, so none failed.
    }
    return m_store.save(m_path);
}

} // namespace Aestra
