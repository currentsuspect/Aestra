// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-C14 step 1 (FD-23): the UISurfaceStore schema has no runtime caller yet
// (that is a later step), so this proves only the store's own file-format
// contract in isolation: round-trip fidelity, corruption/absence safety,
// forward-compat with an unrecognized future field, editor.*-only pruning,
// and that the store never mutates the explicit maximize preference on its
// own. Every future step (3, 5, 6) builds against this contract.

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
        mixer.x = 0.1;
        mixer.y = 0.2;
        mixer.width = 0.6;
        mixer.height = 0.4;
        mixer.maximized = false;
        mixer.lastUsedAt = recentUnixSeconds();
        store.surfaces["panel.mixer"] = mixer;

        UISurfaceGeometry editor;
        editor.x = 0.3;
        editor.y = 0.25;
        editor.width = 0.2;
        editor.height = 0.3;
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
        require(loadedMixer.x == mixer.x && loadedMixer.y == mixer.y && loadedMixer.width == mixer.width &&
                    loadedMixer.height == mixer.height && loadedMixer.maximized == mixer.maximized &&
                    loadedMixer.lastUsedAt == mixer.lastUsedAt,
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

    std::cout << "\nAll UISurfaceStore tests passed.\n";
    return 0;
}
