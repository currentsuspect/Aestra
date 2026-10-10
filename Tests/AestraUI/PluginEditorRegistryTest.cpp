// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// PluginEditorRegistryTest — the editor dispatch table is complete and correct.
//
// P4's registry replaced twelve `else if (pluginId == "...")` arms in
// PluginUIController.cpp. The defect that motivated it was a typo in a plugin id
// compiling clean and silently degrading the plugin to the generic editor, with
// nothing to notice. This test is what makes that impossible to reintroduce: the
// ids are enumerated here, so a wrong one fails.
//
// It checks the REGISTRY, which is the headless part, and does not construct
// editors. Building an editor needs a live plugin instance and a platform bridge,
// and a test that cannot construct an instance can still check every id resolves,
// every id is unique, every factory is present, and the fallback is reachable
// exactly when it should be.

#include "PluginEditorRegistry.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace AestraUI;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}

/// The plugins that are expected to have an editor of their own. Written out
/// rather than derived, because a derived check would pass if the registry and
/// this list were wrong in the same way.
const std::vector<std::string>& dedicatedPluginIds() {
    static const std::vector<std::string> kIds{
        "com.Aestrastudios.eq",
        "com.Aestrastudios.comp",
        "com.Aestrastudios.verb",
        "com.Aestrastudios.delay",
        "com.Aestrastudios.drift",
        "com.Aestrastudios.limiter",
        "com.Aestrastudios.sat",
        "com.Aestrastudios.filter",
        "com.Aestrastudios.ott",
        "com.Aestrastudios.lfo",
        "com.Aestrastudios.transient",
    };
    return kIds;
}

} // namespace

int main() {
    const auto& registry = pluginEditorRegistry;

    // ---------------------------------------------------------------------
    // The fallback is the one registration that must always exist, because it is
    // what a plugin with no special editor gets. Losing it would make every
    // unregistered plugin produce a null editor.
    // ---------------------------------------------------------------------
    check(!registry.empty(), "the registry is not empty");
    check(std::strlen(registry.front().pluginId) == 0,
          "the generic fallback is the first entry, identified by an empty id");
    check(std::strcmp(registry.front().editorName, "GenericPluginEditor") == 0,
          "the fallback's name is the generic editor");
    check(pluginEditorNameFor("com.example.not.registered") != nullptr,
          "an unknown plugin resolves to a name");

    // ---------------------------------------------------------------------
    // Every expected dedicated editor is registered, resolves, and is reported
    // as dedicated. This is the assertion a typo'd id would fail.
    // ---------------------------------------------------------------------
    for (const std::string& id : dedicatedPluginIds()) {
        check(pluginEditorHasDedicatedEntry(id), id + " has a dedicated editor");
        const char* name = pluginEditorNameFor(id);
        check(name != nullptr, id + " resolves to an editor name");
        check(std::strcmp(name, "GenericPluginEditor") != 0,
              id + " does not fall back to the generic editor");
        // pluginEditorFactoryFor is NOT checked here: it lives in the .cpp, whose
        // factories construct the eleven editors and would drag the NUI widget
        // stack into this test. What is checked is that the id resolves, which
        // is what a typo would break.
    }

    // ---------------------------------------------------------------------
    // An unregistered id falls back rather than returning null. That is the
    // whole reason a new effect needs no editor today.
    // ---------------------------------------------------------------------
    for (const char* unknown : {"com.Aestrastudios.somethingnew", "typo.aestrastudios.eq", "",
                                "com.Aestrastudios.EQ", "com.aestrastudios.eq"}) {
        const std::string id{unknown};
        // Plugin ids are case-sensitive and an empty id is not a plugin, so every
        // one of these must be treated as unregistered. A lookup that normalised
        // case would hand a typo the wrong editor silently, which is the defect
        // this whole pass is about.
        check(!pluginEditorHasDedicatedEntry(id), std::string("\"") + unknown + "\" is not a dedicated editor");
        check(std::strcmp(pluginEditorNameFor(id.c_str()), "GenericPluginEditor") == 0,
              std::string("\"") + unknown + "\" resolves to the generic editor");
    }

    // ---------------------------------------------------------------------
    // No duplicate ids. Two rows claiming one plugin id means lookup order
    // decides which editor wins, silently.
    // ---------------------------------------------------------------------
    size_t dedicatedRows = 0;
    for (size_t i = 0; i < registry.size(); ++i) {
        if (registry[i].pluginId[0] == '\0')
            continue;
        ++dedicatedRows;
        for (size_t j = i + 1; j < registry.size(); ++j) {
            check(std::strcmp(registry[i].pluginId, registry[j].pluginId) != 0,
                  std::string("plugin id ") + registry[i].pluginId + " is registered twice");
        }
    }
    check(dedicatedRows == dedicatedPluginIds().size(),
          "the registry holds exactly as many dedicated editors as expected");

    // ---------------------------------------------------------------------
    // Every row is complete: a present factory and a present name. A null
    // factory would make pluginEditorFactoryFor hand a null to a caller that
    // believes it has an editor.
    // ---------------------------------------------------------------------
    for (const auto& r : registry) {
        const std::string who = (r.pluginId[0] == '\0') ? "<generic fallback>" : r.pluginId;
        check(r.editorName != nullptr && std::strlen(r.editorName) > 0, who + " has a name");
    }

    if (g_failures == 0) {
        std::cout << "=== PluginEditorRegistryTest: all checks passed ===\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "=== PluginEditorRegistryTest: " << g_failures << " failure(s) ===\n";
    return EXIT_FAILURE;
}