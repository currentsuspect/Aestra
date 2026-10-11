// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-C14 step 3 (FD-23): the mixer inspector preference moved from
// ~/.config/aestra/mixer_settings.json into the shared UISurfaceStore. FD-23 allows
// "leave compatible" only if it does not mean two long-lived paths to one
// preference, so the import is one-time and one-way. These checks pin exactly that:
// the legacy value is copied once, the legacy file is never read again once the store
// holds a value, and it is never written, deleted, or used to invent a value.

#include "../../Source/Core/LegacyMixerSettingsImport.h"
#include "../Support/TestTempDirectory.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
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

std::string readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    using namespace Aestra;

    const Tests::ScopedTempDirectory tempDirScope{"LegacyMixerImport"};
    const auto& tempDir = tempDirScope.path();
    const std::string key = UISurfaceKeys::kMixerInspectorExpanded;

    // --- I1 + I3: a collapsed legacy preference is imported once, then never read again. ---
    {
        const auto legacy = tempDir / "i1-mixer_settings.json";
        const auto storePath = (tempDir / "i1-store.json").string();
        require(writeRawFile(legacy, "{\"version\": 1, \"inspectorExpanded\": false}"), "I1 legacy fixture written");

        {
            UISurfaceStoreFile store(storePath);
            require(importLegacyMixerInspectorPreference(store, legacy.string()) == LegacyImportResult::Imported,
                    "I1: a legacy false is reported as Imported");
        }
        const UISurfaceStoreFile reopened(storePath);
        require(reopened.boolPreference(key) == std::optional<bool>(false),
                "I1: the imported value was saved, so a fresh store reads false");

        // The user's old build (or anything else) rewrites the legacy file after the import.
        require(writeRawFile(legacy, "{\"version\": 1, \"inspectorExpanded\": true}"), "I3 legacy rewritten");
        UISurfaceStoreFile again(storePath);
        require(importLegacyMixerInspectorPreference(again, legacy.string()) == LegacyImportResult::AlreadyInStore,
                "I3: once the store holds the preference, the import reports AlreadyInStore");
        require(again.boolPreference(key) == std::optional<bool>(false),
                "I3: a disagreeing legacy file never overrides the stored value");
        std::cout << "[PASS] I1/I3 imported once, legacy never consulted again\n";
    }

    // --- I2: an expanded legacy preference is imported too, even though it equals the default. ---
    {
        const auto legacy = tempDir / "i2-mixer_settings.json";
        const auto storePath = (tempDir / "i2-store.json").string();
        require(writeRawFile(legacy, "{\"version\": 1, \"inspectorExpanded\": true}"), "I2 legacy fixture written");
        UISurfaceStoreFile store(storePath);
        require(importLegacyMixerInspectorPreference(store, legacy.string()) == LegacyImportResult::Imported,
                "I2: a legacy true is Imported, not skipped as 'already the default'");
        require(std::filesystem::exists(storePath) &&
                    UISurfaceStore::load(storePath).boolPreferences.count(key) == 1,
                "I2: the key is written, so the import cannot re-run on later launches");
        std::cout << "[PASS] I2 a default-valued legacy preference is still imported\n";
    }

    // --- I4: no legacy file -> nothing imported and nothing written. ---
    {
        const auto storePath = (tempDir / "i4-store.json").string();
        UISurfaceStoreFile store(storePath);
        require(importLegacyMixerInspectorPreference(store, (tempDir / "i4-absent.json").string()) ==
                    LegacyImportResult::NothingToImport,
                "I4: a missing legacy file is NothingToImport");
        require(!store.boolPreference(key).has_value(), "I4: no value is invented");
        require(!std::filesystem::exists(storePath), "I4: no store file is written just because the app started");
        std::cout << "[PASS] I4 no legacy file writes nothing\n";
    }

    // --- I5: a truncated legacy file imports nothing. ---
    {
        const auto legacy = tempDir / "i5-mixer_settings.json";
        const auto storePath = (tempDir / "i5-store.json").string();
        require(writeRawFile(legacy, "{\"inspectorExpanded\": fal"), "I5 truncated fixture written");
        UISurfaceStoreFile store(storePath);
        require(importLegacyMixerInspectorPreference(store, legacy.string()) == LegacyImportResult::NothingToImport,
                "I5: a truncated legacy file is NothingToImport");
        require(!std::filesystem::exists(storePath), "I5: and writes no store file");
        std::cout << "[PASS] I5 corrupt legacy file imports nothing\n";
    }

    // --- I6: wrong-typed or missing values import nothing (asBool() would import a collapse). ---
    {
        const char* payloads[] = {"{\"inspectorExpanded\": 1}", "{\"inspectorExpanded\": \"true\"}",
                                  "{\"inspectorExpanded\": null}", "{\"inspectorExpanded\": []}",
                                  "{\"version\": 1, \"somethingElse\": true}"};
        int index = 0;
        for (const char* payload : payloads) {
            const auto legacy = tempDir / ("i6-legacy-" + std::to_string(index) + ".json");
            const auto storePath = (tempDir / ("i6-store-" + std::to_string(index) + ".json")).string();
            ++index;
            require(writeRawFile(legacy, payload), std::string("I6 fixture written: ") + payload);
            UISurfaceStoreFile store(storePath);
            require(importLegacyMixerInspectorPreference(store, legacy.string()) == LegacyImportResult::NothingToImport,
                    std::string("I6: payload must import nothing: ") + payload);
            require(!store.boolPreference(key).has_value(), std::string("I6: no value invented for: ") + payload);
        }
        std::cout << "[PASS] I6 wrong-typed and missing values import nothing\n";
    }

    // --- I7: importing never rewrites or deletes the legacy file. ---
    {
        const auto legacy = tempDir / "i7-mixer_settings.json";
        const std::string original = "{\n  \"inspectorExpanded\": false,\n  \"version\": 1\n}";
        require(writeRawFile(legacy, original), "I7 legacy fixture written");
        const auto mtimeBefore = std::filesystem::last_write_time(legacy);

        UISurfaceStoreFile store((tempDir / "i7-store.json").string());
        require(importLegacyMixerInspectorPreference(store, legacy.string()) == LegacyImportResult::Imported,
                "I7 import ran");
        require(std::filesystem::exists(legacy), "I7: the legacy file is left in place");
        require(readAll(legacy) == original, "I7: its bytes are unchanged");
        require(std::filesystem::last_write_time(legacy) == mtimeBefore, "I7: its modification time is unchanged");
        std::cout << "[PASS] I7 legacy file untouched by the import\n";
    }

    // --- I8: a store that cannot be written reports SaveFailed, keeping the value in memory. ---
    {
        const auto legacy = tempDir / "i8-mixer_settings.json";
        require(writeRawFile(legacy, "{\"inspectorExpanded\": false}"), "I8 legacy fixture written");
        UISurfaceStoreFile store((tempDir / "i8-no-such-dir" / "store.json").string());
        require(importLegacyMixerInspectorPreference(store, legacy.string()) == LegacyImportResult::SaveFailed,
                "I8: a failed save is reported as SaveFailed, not Imported");
        require(store.boolPreference(key) == std::optional<bool>(false),
                "I8: the session still uses the imported value");
        std::cout << "[PASS] I8 failed save reported honestly\n";
    }

    // --- I9: resolving the legacy path creates nothing. ---
    {
        const auto home = tempDir / "i9-home";
        require(std::filesystem::create_directory(home), "I9 home fixture created");
        const std::string expected = (home / ".config" / "aestra" / "mixer_settings.json").string();
        require(legacyMixerSettingsPath(home.string()) == expected, "I9: legacy path is <home>/.config/aestra/mixer_settings.json");
        require(!std::filesystem::exists(home / ".config"), "I9: resolving the path creates no directory");
        require(legacyMixerSettingsPath(std::string{}).empty(), "I9: an empty home yields an empty path");
        std::cout << "[PASS] I9 legacy path resolution is side-effect free\n";
    }

    std::cout << "\nAll LegacyMixerSettingsImport tests passed.\n";
    return 0;
}
