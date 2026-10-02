// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// PluginEditorRegistry — which editor a plugin gets, as data.
//
// P4's registry. The dispatch was twelve arms of `else if (pluginId == "...")`
// in PluginUIController.cpp, each doing the identical four steps:
//
//     auto ed = std::make_shared<XEditor>(instance);
//     wireEditorClose(ed);
//     ed->setPlatformBridge(m_platformBridge);
//     editorComp = ed;
//
// A typo in a plugin id compiled clean and silently degraded the plugin to the
// generic editor. A new special editor meant finding the right place in an
// if/else chain in a 700-line controller. And the four steps being copy-pasted
// twelve times is the shape that lets one arm forget the close-wiring and leak
// an editor, which nothing would catch.
//
// The registry inverts that: one table, and the four steps exist once.
//
// WHAT THIS IS NOT. It is not a layout kit, and it does not try to be. The eleven
// editors are 12,297 lines, AestraEQEditor alone is 5,646, and AestraVerbEditor
// carries a visualizer. Making a new effect's editor "a layout plus, if needed, a
// visualizer" means rewriting those as declarative layouts, and that cannot be
// verified headlessly -- it needs eyes on rendering. That work is deliberately
// left for v0.8.0 rather than attempted blind inside a hardening pass.
//
// The exit criterion P4 actually needed is mostly met already: a new effect needs
// no editor at all today, because the fallback hands it GenericPluginEditor. What
// was missing is that the fallback sat inside an if/else chain. This moves it into
// a table where the entries are enumerable, testable, and checkable against the
// registry of plugins that actually exist.

#pragma once

#include "AestraPanelWindow.h"
#include "PluginHost.h"

#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

namespace AestraUI {

class NUIComponent;
class NUIPlatformBridge;

/**
 * @brief Builds one plugin's editor.
 *
 * Takes the instance and the platform bridge and returns the editor. The bridge
 * is a parameter rather than something the editor reaches for, so a factory
 * cannot forget it -- one of the two things every arm of the old chain had to
 * remember to do by hand.
 *
 * Defined in PluginEditorRegistry.cpp. The factories construct concrete editors,
 * so a translation unit holding only ids must not link them; that is what lets
 * PluginEditorRegistryTest check every id without the widget stack.
 */
using PluginEditorFactory = std::function<std::shared_ptr<AestraPanelWindow>(
    std::shared_ptr<Aestra::Audio::IPluginInstance>, NUIPlatformBridge*)>;

/**
 * @brief One plugin id and the editor name it maps to.
 *
 * No factory here on purpose. A factory is a std::function that constructs a
 * concrete editor, so a table holding them could only be consumed by a
 * translation unit that links all eleven editor .cpp files -- which drags the NUI
 * widget stack and thorvg into a test whose entire job is checking that eleven
 * strings are spelled correctly and resolve.
 *
 * So the id/name table is header-inline and the factory lookup lives in the .cpp,
 * scanning this same table. It cannot disagree with it: there is one table, and
 * the .cpp reads it rather than repeating it.
 */
struct PluginEditorRegistration {
    /// Permanent plugin id. Never a display name -- see AGENTS.md §19.
    const char* pluginId;
    /// Short label for diagnostics, so a log line naming an editor is readable.
    const char* editorName;
};

/**
 * @brief Every editor registration, including the fallback.
 *
 * Order does not matter: lookup is by id. The fallback is identified by an EMPTY
 * id, which no real plugin can have, so it is matched by exclusion rather than by
 * position -- a plugin id that somehow collided with the sentinel could not
 * become the catch-all by accident.
 */
/// How many rows the registry has, premium or not.
///
/// A named constant rather than a literal at the array, because the rumble row is
/// behind an #ifdef: a hardcoded size compiled the default build and broke the
/// premium one with thirteen rows in twelve slots. This counts the same condition
/// CMake sets, so the two cannot disagree.
inline constexpr size_t kPluginEditorCount =
#ifdef AESTRAUI_ENABLE_PREMIUM_EDITORS
    13;
#else
    12;
#endif

inline constexpr std::array<PluginEditorRegistration, kPluginEditorCount> pluginEditorRegistry{{
    {"", "GenericPluginEditor"},
#ifdef AESTRAUI_ENABLE_PREMIUM_EDITORS
    {"com.Aestrastudios.rumble", "RumblePluginEditor"},
#endif
    {"com.Aestrastudios.eq", "AestraEQEditor"},
    {"com.Aestrastudios.comp", "AestraCompEditor"},
    {"com.Aestrastudios.verb", "AestraVerbEditor"},
    {"com.Aestrastudios.delay", "AestraDelayEditor"},
    {"com.Aestrastudios.drift", "AestraDriftEditor"},
    {"com.Aestrastudios.limiter", "AestraLimitEditor"},
    {"com.Aestrastudios.sat", "AestraSatEditor"},
    {"com.Aestrastudios.filter", "AestraFilterEditor"},
    {"com.Aestrastudios.ott", "AestraOTTEditor"},
    {"com.Aestrastudios.lfo", "AestraLFOEditor"},
    {"com.Aestrastudios.transient", "AestraTransientEditor"},
}};

inline constexpr bool pluginEditorHasDedicatedEntry(const std::string& pluginId) {
    for (const PluginEditorRegistration& r : pluginEditorRegistry) {
        if (r.pluginId[0] != '\0' && std::strcmp(r.pluginId, pluginId.c_str()) == 0)
            return true;
    }
    return false;
}

inline constexpr const char* pluginEditorNameFor(const std::string& pluginId) {
    for (const PluginEditorRegistration& r : pluginEditorRegistry) {
        if (r.pluginId[0] != '\0' && std::strcmp(r.pluginId, pluginId.c_str()) == 0)
            return r.editorName;
    }
    return pluginEditorRegistry.front().editorName;
}

/**
 * @brief The editor factory for @p pluginId, or the generic fallback.
 *
 * Never returns an empty factory: an unregistered plugin gets GenericPluginEditor,
 * which is the behaviour the old `default:` arm had and the reason a new effect
 * needs no editor today. Defined in the .cpp, because it indexes the registry.
 */
const PluginEditorFactory& pluginEditorFactoryFor(const std::string& pluginId);

} // namespace AestraUI