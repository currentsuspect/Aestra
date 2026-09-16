// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-C14 (FD-23): the UISurfaceStore file-format contract in isolation —
// round-trip fidelity, corruption/absence safety, forward-compat with an
// unrecognized future field, editor.*-only pruning, that the store never mutates
// the explicit maximize preference on its own, and (since step 2) that sizes are
// pixels validated as pixels while position is an anchor. Since step 3 it also pins
// the single owner, UISurfaceStoreFile: constructing it never writes, a change is
// saved, an unchanged value writes nothing, and a save never discards the rest of
// the store.

#include "../../Source/Core/UISurfaceStore.h"
#include "../Support/TestTempDirectory.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

// A recent timestamp for fixtures that must survive the editor.* pruning
// pass -- distinct from the deliberately-ancient one pruning test #4 uses.
int64_t recentUnixSeconds() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

bool writeRawFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.close();
    return out.good();
}

} // namespace

int main() {
    using namespace Aestra;

    const Tests::ScopedTempDirectory tempDirScope{"UISurfaceStore"};
    const auto& tempDir = tempDirScope.path();

    // --- 1. Round-trip fidelity: every field shape, field-for-field. ---
    {
        UISurfaceStore store;

        UISurfaceGeometry mixer;
        mixer.anchorX = 0.1;
        mixer.anchorY = 0.2;
        mixer.width = 960.5;
        mixer.height = 420.25;
        mixer.maximized = false;
        mixer.lastUsedAt = recentUnixSeconds();
        store.surfaces["panel.mixer"] = mixer;

        UISurfaceGeometry editor;
        editor.anchorX = 0.3;
        editor.anchorY = 0.25;
        editor.width = 520.0;
        editor.height = 300.0;
        editor.maximized = true;
        editor.lastUsedAt = recentUnixSeconds();
        store.surfaces["editor.some-instance-id"] = editor;

        UIDialogExportOptions exportOpts;
        exportOpts.sampleRateIndex = 2;
        exportOpts.bitDepthIndex = 1;
        exportOpts.scopeIndex = 0;
        exportOpts.tailInput = "2.5s";
        exportOpts.lastOutputDirectory = "/home/user/exports";
        store.dialogExport = exportOpts;

        store.boolPreferences["panel.mixer.inspectorExpanded"] = true;
        store.listPreferences["pluginBrowser.favorites"] = {"comp", "delay", "verb"};

        const auto path = (tempDir / "roundtrip.json").string();
        require(store.save(path), "save reports success");

        const UISurfaceStore loaded = UISurfaceStore::load(path);
        require(loaded.schemaVersion == UISurfaceStore::kCurrentSchemaVersion, "schemaVersion round-trips");

        require(loaded.surfaces.count("panel.mixer") == 1, "panel.mixer key present after reload");
        const UISurfaceGeometry& loadedMixer = loaded.surfaces.at("panel.mixer");
        require(loadedMixer.anchorX == mixer.anchorX && loadedMixer.anchorY == mixer.anchorY &&
                    loadedMixer.width == mixer.width && loadedMixer.height == mixer.height &&
                    loadedMixer.maximized == mixer.maximized && loadedMixer.lastUsedAt == mixer.lastUsedAt,
                "panel.mixer geometry is field-for-field identical");

        require(loaded.surfaces.count("editor.some-instance-id") == 1, "editor.* key present after reload");
        const UISurfaceGeometry& loadedEditor = loaded.surfaces.at("editor.some-instance-id");
        require(loadedEditor.maximized == editor.maximized, "editor maximized preference round-trips");
        require(loadedEditor.lastUsedAt == editor.lastUsedAt, "editor lastUsedAt round-trips");

        require(loaded.dialogExport.has_value(), "dialogExport present after reload");
        require(loaded.dialogExport->sampleRateIndex == exportOpts.sampleRateIndex &&
                    loaded.dialogExport->bitDepthIndex == exportOpts.bitDepthIndex &&
                    loaded.dialogExport->scopeIndex == exportOpts.scopeIndex &&
                    loaded.dialogExport->tailInput == exportOpts.tailInput &&
                    loaded.dialogExport->lastOutputDirectory == exportOpts.lastOutputDirectory,
                "dialogExport is field-for-field identical");

        require(loaded.boolPreferences.count("panel.mixer.inspectorExpanded") == 1 &&
                    loaded.boolPreferences.at("panel.mixer.inspectorExpanded"),
                "boolPreferences entry round-trips");
        require(loaded.listPreferences.count("pluginBrowser.favorites") == 1 &&
                    loaded.listPreferences.at("pluginBrowser.favorites") ==
                        std::vector<std::string>{"comp", "delay", "verb"},
                "listPreferences entry round-trips");

        std::cout << "[PASS] round-trip fidelity across every field shape\n";
    }

    // --- 2. Corruption/absence safety: missing, empty, malformed all -> defaults. ---
    {
        const auto missingPath = (tempDir / "does-not-exist.json").string();
        const UISurfaceStore fromMissing = UISurfaceStore::load(missingPath);
        require(fromMissing.surfaces.empty() && !fromMissing.dialogExport.has_value(),
                "missing file loads to defaults");

        const auto emptyPath = (tempDir / "empty.json").string();
        require(writeRawFile(emptyPath, ""), "empty scratch file written");
        const UISurfaceStore fromEmpty = UISurfaceStore::load(emptyPath);
        require(fromEmpty.surfaces.empty(), "empty file loads to defaults");

        const auto malformedPath = (tempDir / "malformed.json").string();
        require(writeRawFile(malformedPath, "{\"schemaVersion\": 1, \"surfaces\": {\"panel.mixer\": "),
                "malformed scratch file written");
        const UISurfaceStore fromMalformed = UISurfaceStore::load(malformedPath);
        require(fromMalformed.surfaces.empty(), "malformed (truncated) file loads to defaults");

        std::cout << "[PASS] missing/empty/malformed all load to defaults without throwing\n";
    }

    // --- 3. Forward-compat: unrecognized future schemaVersion + extra field. ---
    {
        const auto futurePath = (tempDir / "future-version.json").string();
        const int futureVersion = UISurfaceStore::kCurrentSchemaVersion + 1;
        const std::string fixture = "{\"schemaVersion\": " + std::to_string(futureVersion) +
                                     ", \"surfaces\": {}, \"somethingFromTheFuture\": true}";
        require(writeRawFile(futurePath, fixture), "future-version scratch file written");

        // A future, unrecognized schemaVersion is treated as foreign/corrupt
        // (see decision c: "schemaVersion < current" is the only migration
        // path defined; anything newer falls back rather than guessing at an
        // unknown shape) -- it must still load to defaults without failing,
        // never crash or throw.
        const UISurfaceStore fromFuture = UISurfaceStore::load(futurePath);
        require(fromFuture.surfaces.empty(), "unrecognized future version loads to defaults, not a crash");

        // Re-saving must not resurrect the unrecognized field.
        const auto resavePath = (tempDir / "future-resaved.json").string();
        require(fromFuture.save(resavePath), "re-save after loading a future-version file succeeds");
        std::ifstream in(resavePath);
        const std::string resaved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(resaved.find("somethingFromTheFuture") == std::string::npos,
                "unrecognized field is not round-tripped back out on save");

        std::cout << "[PASS] unrecognized future schemaVersion/field handled without failure\n";
    }

    // --- 4. Pruning: editor.* only, panel.* is exempt. ---
    {
        UISurfaceStore store;
        const int64_t veryOld = 1; // 1970-01-01 plus one second: far older than the 180-day cutoff.

        UISurfaceGeometry staleEditor;
        staleEditor.lastUsedAt = veryOld;
        store.surfaces["editor.stale-instance"] = staleEditor;

        UISurfaceGeometry stalePanel;
        stalePanel.lastUsedAt = veryOld;
        store.surfaces["panel.mixer"] = stalePanel;

        const auto path = (tempDir / "pruning.json").string();
        require(store.save(path), "pruning fixture saved");

        const UISurfaceStore loaded = UISurfaceStore::load(path);
        require(loaded.surfaces.count("editor.stale-instance") == 0,
                "a stale editor.* entry is pruned on load");
        require(loaded.surfaces.count("panel.mixer") == 1,
                "an equally-old panel.* entry is NOT pruned -- pruning is editor.*-only");

        std::cout << "[PASS] pruning is scoped to editor.* only, not panel.*\n";
    }

    // --- 5. The store never mutates `maximized` on its own. ---
    {
        UISurfaceStore store;
        UISurfaceGeometry geom;
        geom.maximized = true;
        store.surfaces["panel.mixer"] = geom;

        const auto path = (tempDir / "maximize-isolation.json").string();
        for (int i = 0; i < 5; ++i) {
            require(store.save(path), "repeated save succeeds");
            store = UISurfaceStore::load(path);
            require(store.surfaces.at("panel.mixer").maximized,
                    "maximized preference is untouched across repeated load/save cycles -- "
                    "no clamping logic lives in the store itself");
        }

        std::cout << "[PASS] maximize preference is never mutated by the store itself\n";
    }

    // --- 6. Invalid schemaVersion values fall back to defaults, never import as "older". ---
    {
        const std::string body = ", \"surfaces\": {\"panel.mixer\": {\"anchorX\": 0.25}}}";

        // Positive control: the identical body under a valid version loads, so the
        // rejections below are caused by the version and cannot pass vacuously.
        const auto controlPath = (tempDir / "version-control.json").string();
        require(writeRawFile(controlPath, "{\"schemaVersion\": 1" + body), "version control fixture written");
        require(UISurfaceStore::load(controlPath).surfaces.count("panel.mixer") == 1,
                "control: schemaVersion 1 with the same body loads the entry");

        for (const char* badVersion : {"0", "-1", "0.5", "1.5", "1e300", "\"1\""}) {
            const auto path = (tempDir / "version-bad.json").string();
            require(writeRawFile(path, std::string("{\"schemaVersion\": ") + badVersion + body),
                    std::string("bad-version fixture written: ") + badVersion);
            require(UISurfaceStore::load(path).surfaces.empty(),
                    std::string("schemaVersion ") + badVersion + " must fall back to defaults, not be imported");
        }

        std::cout << "[PASS] invalid schemaVersion values fall back to defaults\n";
    }

    // --- 7. Out-of-domain lastUsedAt is rejected to the default, never converted. ---
    {
        for (const char* badTimestamp : {"1e20", "-9.3e18", "-1", "12.5"}) {
            const auto path = (tempDir / "timestamp-bad.json").string();
            require(writeRawFile(path, std::string("{\"schemaVersion\": 1, \"surfaces\": {\"panel.mixer\": "
                                                   "{\"anchorX\": 0.25, \"lastUsedAt\": ") +
                                           badTimestamp + "}}}"),
                    std::string("bad-timestamp fixture written: ") + badTimestamp);

            const UISurfaceStore loaded = UISurfaceStore::load(path);
            // panel.* is never pruned, so the entry must survive and only the timestamp is rejected.
            require(loaded.surfaces.count("panel.mixer") == 1, "panel.* entry with a bad timestamp still loads");
            require(loaded.surfaces.at("panel.mixer").anchorX == 0.25, "the rest of the entry is unaffected");
            require(loaded.surfaces.at("panel.mixer").lastUsedAt == 0,
                    std::string("lastUsedAt ") + badTimestamp + " must be rejected to the default 0");
        }

        std::cout << "[PASS] out-of-domain lastUsedAt values are rejected, not converted\n";
    }

    // --- 8. Sizes are pixels (step 2): in-domain values load exactly, out-of-domain keep the default. ---
    {
        const UISurfaceGeometry defaults;

        // Positive controls. 1280x720 is exactly what step 1's clamp01 would have
        // destroyed, and 16384 is the accepted boundary.
        const auto controlPath = (tempDir / "size-control.json").string();
        require(writeRawFile(controlPath, "{\"schemaVersion\": 1, \"surfaces\": {\"panel.mixer\": "
                                          "{\"width\": 1280, \"height\": 720}, \"panel.browser\": "
                                          "{\"width\": 16384, \"height\": 16384}}}"),
                "size control fixture written");
        const UISurfaceStore control = UISurfaceStore::load(controlPath);
        require(control.surfaces.count("panel.mixer") == 1 && control.surfaces.at("panel.mixer").width == 1280.0 &&
                    control.surfaces.at("panel.mixer").height == 720.0,
                "control: a 1280x720 pixel size loads exactly, not clamped to a fraction");
        require(control.surfaces.count("panel.browser") == 1 &&
                    control.surfaces.at("panel.browser").width == 16384.0,
                "control: 16384px, the boundary, is accepted");

        for (const char* badSize : {"0", "-5", "16385", "1e9", "\"NaN\""}) {
            const auto path = (tempDir / "size-bad.json").string();
            require(writeRawFile(path, std::string("{\"schemaVersion\": 1, \"surfaces\": {\"panel.mixer\": "
                                                   "{\"anchorX\": 0.25, \"width\": ") +
                                           badSize + ", \"height\": " + badSize + "}}}"),
                    std::string("bad-size fixture written: ") + badSize);

            const UISurfaceStore loaded = UISurfaceStore::load(path);
            require(loaded.surfaces.count("panel.mixer") == 1, "an entry with a bad size still loads");
            require(loaded.surfaces.at("panel.mixer").anchorX == 0.25, "the rest of the entry is unaffected");
            require(loaded.surfaces.at("panel.mixer").width == defaults.width &&
                        loaded.surfaces.at("panel.mixer").height == defaults.height,
                    std::string("size ") + badSize + " must keep the default, not be stored");
        }

        std::cout << "[PASS] pixel sizes validated as pixels; out-of-domain sizes keep the default\n";
    }

    const std::string inspectorKey = UISurfaceKeys::kMixerInspectorExpanded;

    // --- 9 (O1). Constructing the owner on a missing file loads defaults and writes nothing. ---
    {
        const auto path = (tempDir / "owner-missing.json").string();
        const UISurfaceStoreFile owner(path);
        require(!owner.boolPreference(inspectorKey).has_value(), "O1: a new store has no inspector preference");
        require(!std::filesystem::exists(path), "O1: constructing the owner creates no file");
        std::cout << "[PASS] O1 constructing the owner never writes\n";
    }

    // --- 10 (O2 + O3). A change is saved; setting the same value again writes nothing. ---
    {
        const auto path = (tempDir / "owner-save.json").string();
        {
            UISurfaceStoreFile owner(path);
            require(owner.setBoolPreference(inspectorKey, false), "O2: setting a new value reports success");
        }
        const UISurfaceStoreFile reopened(path);
        require(reopened.boolPreference(inspectorKey) == std::optional<bool>(false),
                "O2: the change was saved, so a fresh owner reads it");

        UISurfaceStoreFile same(path);
        std::filesystem::remove(path);
        require(same.setBoolPreference(inspectorKey, false), "O3: setting the value it already holds reports success");
        require(!std::filesystem::exists(path),
                "O3: an unchanged value writes nothing (so applying a loaded preference never echoes into a save)");
        std::cout << "[PASS] O2/O3 changes are saved; unchanged values are not\n";
    }

    // --- 11 (O4). Saving one preference never discards the rest of the store. ---
    {
        const auto path = (tempDir / "owner-preserve.json").string();
        UISurfaceStore seeded;
        UISurfaceGeometry geometry;
        geometry.width = 777.0;
        seeded.surfaces["panel.mixer"] = geometry;
        seeded.listPreferences["pluginBrowser.favorites"] = {"comp"};
        require(seeded.save(path), "O4 seed saved");

        {
            UISurfaceStoreFile owner(path);
            require(owner.setBoolPreference(inspectorKey, true), "O4: setting a preference reports success");
        }
        const UISurfaceStore reloaded = UISurfaceStore::load(path);
        require(reloaded.surfaces.count("panel.mixer") == 1 && reloaded.surfaces.at("panel.mixer").width == 777.0,
                "O4: stored geometry survives a preference save");
        require(reloaded.listPreferences.count("pluginBrowser.favorites") == 1,
                "O4: stored list preferences survive a preference save");
        require(reloaded.boolPreferences.count(inspectorKey) == 1 && reloaded.boolPreferences.at(inspectorKey),
                "O4: and the new preference was written");
        std::cout << "[PASS] O4 a save keeps everything else in the store\n";
    }

    // --- 12 (O5). A failed write is reported, and the value stays in memory. ---
    {
        const auto path = (tempDir / "no-such-dir" / "owner.json").string();
        UISurfaceStoreFile owner(path);
        require(!owner.setBoolPreference(inspectorKey, false), "O5: a write to an unwritable location reports failure");
        require(owner.boolPreference(inspectorKey) == std::optional<bool>(false),
                "O5: the session keeps the new value in memory");
        std::cout << "[PASS] O5 failed saves are reported, not hidden\n";
    }

    // --- 13 (G1). Geometry accessors: absent until set, saved on change, silent when unchanged. ---
    {
        const auto path = (tempDir / "owner-geometry.json").string();
        UISurfaceStoreFile owner(path);
        require(!owner.surfaceGeometry(UISurfaceKeys::kPanelMixer).has_value(),
                "G1: a surface the user never touched has no stored geometry");

        UISurfaceGeometry gesture;
        gesture.anchorX = 0.25;
        gesture.anchorY = 0.75;
        gesture.width = 640.0;
        gesture.height = 400.0;
        gesture.maximized = false;
        gesture.lastUsedAt = recentUnixSeconds();
        require(owner.setSurfaceGeometry(UISurfaceKeys::kPanelMixer, gesture),
                "G1: storing a new preference reports success");

        const UISurfaceStoreFile reopened(path);
        const auto loaded = reopened.surfaceGeometry(UISurfaceKeys::kPanelMixer);
        require(loaded.has_value(), "G1: the geometry was saved, so a fresh owner reads it");
        require(loaded->anchorX == gesture.anchorX && loaded->anchorY == gesture.anchorY &&
                    loaded->width == gesture.width && loaded->height == gesture.height &&
                    loaded->maximized == gesture.maximized && loaded->lastUsedAt == gesture.lastUsedAt,
                "G1: every geometry field round-trips, maximized and timestamp included");

        UISurfaceStoreFile same(path);
        std::filesystem::remove(path);
        UISurfaceGeometry timestampOnly = gesture;
        ++timestampOnly.lastUsedAt;
        require(same.setSurfaceGeometry(UISurfaceKeys::kPanelMixer, timestampOnly),
                "G1: changing lastUsedAt alone reports success");
        require(!std::filesystem::exists(path),
                "G1: an unchanged preference writes nothing (lastUsedAt alone is not a change)");
        std::cout << "[PASS] G1 geometry accessors save on change, stay silent otherwise\n";
    }

    std::cout << "\nAll UISurfaceStore tests passed.\n";
    return 0;
}
