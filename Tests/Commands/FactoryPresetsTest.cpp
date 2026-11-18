// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// FactoryPresetsTest — V83-S2 slice 4 (FD-72): 8 to 10 factory presets for every Aestra plugin, and each one real.
//   1. every built-in effect has 8 to 10, with unique names, a category, a description and at least one tag;
//   2. every parameter a preset names exists on the plugin (a typo would silently do nothing) and is not bypass;
//   3. loading it puts exactly those values on the plugin, and it changes the sound from the default;
//   4. the factory list is what the library lists first.
// AESTRA_PRESET_DUMP=1 prints each preset in the plugin's own display units, for a listening/reading pass.

#include "Plugin/BuiltInPlugins.h"
#include "Plugin/InternalPluginRegistry.h"
#include "Presets/Preset.h"
#include "Presets/PresetLibrary.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

using namespace Aestra::Audio;
using namespace Aestra::Audio::Presets;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

} // namespace

int main() {
    BuiltInPlugins::registerCoreBuiltIns();
    const bool dump = std::getenv("AESTRA_PRESET_DUMP") != nullptr;
    const char* ids[] = {"com.Aestrastudios.comp",    "com.Aestrastudios.delay",  "com.Aestrastudios.drift",
                         "com.Aestrastudios.eq",      "com.Aestrastudios.filter", "com.Aestrastudios.lfo",
                         "com.Aestrastudios.limiter", "com.Aestrastudios.ott",    "com.Aestrastudios.sat",
                         "com.Aestrastudios.transient", "com.Aestrastudios.verb"};
    int total = 0;
    for (const char* id : ids) {
        const auto presets = factoryPresets(id);
        const std::string who = id;
        check(presets.size() >= 8 && presets.size() <= 10,
              who + " has 8 to 10 factory presets (has " + std::to_string(presets.size()) + ")");
        total += static_cast<int>(presets.size());

        std::set<std::string> names;
        for (const auto& p : presets) {
            const std::string what = who + " / " + p.name;
            check(names.insert(p.name).second, what + ": name is unique");
            check(!p.category.empty() && !p.description.empty() && !p.tags.empty(),
                  what + ": has a category, a description and tags");
            check(p.source == PresetSource::Factory && p.pluginId == id && p.state.empty(), what + ": factory, params only");

            auto plugin = InternalPluginRegistry::instance().createInstance(id);
            check(plugin != nullptr, what + ": plugin created");
            if (!plugin) continue;
            plugin->initialize(48000.0, 512);
            const auto params = plugin->getParameters();
            const Preset defaults = capture(*plugin, "defaults");
            check(apply(*plugin, p), what + ": applies");

            for (const auto& want : p.params) {
                const PluginParameter* match = nullptr;
                for (const auto& q : params)
                    if (q.name == want.name) match = &q;
                check(match != nullptr, what + ": parameter \"" + want.name + "\" exists");
                if (!match) continue;
                check(!match->isBypass, what + ": does not set bypass");
                check(want.value >= 0.0f && want.value <= 1.0f, what + ": " + want.name + " in range");
                check(std::fabs(plugin->getParameter(match->id) - want.value) < 1.0e-4f,
                      what + ": " + want.name + " lands on the plugin");
            }
            check(countChanges(*plugin, defaults) > 0, what + ": sounds different from the default");

            if (dump) {
                std::printf("%-26s %-34s", id + 18, p.name.c_str());
                for (const auto& want : p.params)
                    for (const auto& q : params)
                        if (q.name == want.name)
                            std::printf(" %s=%s;", q.name.c_str(), plugin->getParameterDisplay(q.id).c_str());
                std::printf("\n");
            }
        }

        // The library lists factory first.
        PresetLibrary lib(std::filesystem::temp_directory_path() / "aestra-factory-presets-test-none");
        const auto listed = lib.list(id);
        check(listed.size() >= presets.size() && (presets.empty() || listed.front().name == presets.front().name),
              who + ": the library lists the factory set first");
    }
    std::cout << "FactoryPresetsTest: " << total << " presets, " << (g_checks - g_failures) << "/" << g_checks
              << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
