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
    obj.set("x", JSON(g.x));
    obj.set("y", JSON(g.y));
    obj.set("width", JSON(g.width));
    obj.set("height", JSON(g.height));
    obj.set("maximized", JSON(g.maximized));
    obj.set("lastUsedAt", JSON(static_cast<double>(g.lastUsedAt)));
    return obj;
}

/// Type-checked, not merely present — a wrong-typed field falls back to the
/// UISurfaceGeometry default for that field, the same "absent and
/// wrong-typed land in the same bucket" policy MixerUIPreferences::load uses,
/// so a stray {"x": "nan"} cannot corrupt geometry silently.
UISurfaceGeometry geometryFromJson(const JSON& obj) {
    UISurfaceGeometry g;
    if (obj.has("x") && obj["x"].isNumber()) {
        g.x = clamp01(obj["x"].asNumber());
    }
    if (obj.has("y") && obj["y"].isNumber()) {
        g.y = clamp01(obj["y"].asNumber());
    }
    if (obj.has("width") && obj["width"].isNumber()) {
        g.width = clamp01(obj["width"].asNumber());
    }
    if (obj.has("height") && obj["height"].isNumber()) {
        g.height = clamp01(obj["height"].asNumber());
    }
    if (obj.has("maximized") && obj["maximized"].isBool()) {
        g.maximized = obj["maximized"].asBool();
    }
    if (obj.has("lastUsedAt") && obj["lastUsedAt"].isNumber()) {
        g.lastUsedAt = static_cast<int64_t>(obj["lastUsedAt"].asNumber());
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

    int fromVersion = 0;
    if (root.has("schemaVersion") && root["schemaVersion"].isNumber()) {
        fromVersion = root["schemaVersion"].asInt();
    } else {
        return store; // No recognizable version at all: treat as foreign/corrupt.
    }
    if (fromVersion > kCurrentSchemaVersion) {
        return store; // From a future build we don't know how to read: defaults.
    }

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

} // namespace Aestra
