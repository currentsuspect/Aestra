// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// PluginEditorRegistry — the factories, half of P4's dispatch table.
//
// The id/name table is header-inline, so a test can check every plugin id without
// linking this file. What lives here is the part that cannot be checked without a
// live instance and a platform bridge: the lambdas that actually construct the
// eleven editors.
//
// The factories are paired with registry rows BY EDITOR NAME, not by position.
// The name comes from the editor's own type (#T below), so a factory cannot sit
// under the wrong name, and the static_asserts check that the two lists name the
// same set of editors. Reordering either list is harmless; a row with no factory,
// or a factory with no row, does not build.

#include "PluginEditorRegistry.h"

#include "AestraCompEditor.h"
#include "AestraDelayEditor.h"
#include "AestraDriftEditor.h"
#include "AestraEQEditor.h"
#include "AestraFilterEditor.h"
#include "AestraLFOEditor.h"
#include "AestraLimitEditor.h"
#include "AestraOTTEditor.h"
#include "AestraSatEditor.h"
#include "AestraTransientEditor.h"
#include "AestraVerbEditor.h"
#include "GenericPluginEditor.h"
#include "RumblePluginEditor.h"

#include <array>
#include <cstring>

namespace AestraUI {

namespace {

/// One lambda per editor, so the three steps every arm of the old chain repeated
/// by hand exist in exactly one place. The bridge is a parameter rather than
/// something the editor reaches for, which is what makes it impossible to
/// construct an editor without one.
template <typename EditorT>
PluginEditorFactory factoryFor() {
    // The instance is a shared_ptr because every editor takes one and stores it.
    return [](std::shared_ptr<Aestra::Audio::IPluginInstance> instance, NUIPlatformBridge* bridge)
               -> std::shared_ptr<AestraPanelWindow> {
        if (!instance)
            return nullptr;
        auto editor = std::make_shared<EditorT>(instance);
        editor->setPlatformBridge(bridge);
        return editor;
    };
}

// Every editor type, once. Each entry's name is stringised from the type itself,
// which is what ties a factory to its name.
#define AESTRA_CORE_PLUGIN_EDITORS(X)                                                                                  \
    X(GenericPluginEditor)                                                                                             \
    X(AestraEQEditor)                                                                                                  \
    X(AestraCompEditor)                                                                                                \
    X(AestraVerbEditor)                                                                                                \
    X(AestraDelayEditor)                                                                                               \
    X(AestraDriftEditor)                                                                                               \
    X(AestraLimitEditor)                                                                                               \
    X(AestraSatEditor)                                                                                                 \
    X(AestraFilterEditor)                                                                                              \
    X(AestraOTTEditor)                                                                                                 \
    X(AestraLFOEditor)                                                                                                 \
    X(AestraTransientEditor)
#ifdef AESTRAUI_ENABLE_PREMIUM_EDITORS
#define AESTRA_PREMIUM_PLUGIN_EDITORS(X) X(RumblePluginEditor)
#else
#define AESTRA_PREMIUM_PLUGIN_EDITORS(X)
#endif

#define AESTRA_EDITOR_NAME(T) #T,
constexpr const char* kEditorNames[] = {AESTRA_CORE_PLUGIN_EDITORS(AESTRA_EDITOR_NAME)
                                            AESTRA_PREMIUM_PLUGIN_EDITORS(AESTRA_EDITOR_NAME)};
#undef AESTRA_EDITOR_NAME

constexpr bool sameName(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

constexpr bool isEditorName(const char* name) {
    for (const char* candidate : kEditorNames)
        if (sameName(candidate, name))
            return true;
    return false;
}

constexpr bool everyRowHasAFactory() {
    for (const PluginEditorRegistration& r : pluginEditorRegistry)
        if (!isEditorName(r.editorName))
            return false;
    return true;
}

constexpr bool everyFactoryHasARow() {
    for (const char* name : kEditorNames) {
        bool found = false;
        for (const PluginEditorRegistration& r : pluginEditorRegistry)
            found = found || sameName(r.editorName, name);
        if (!found)
            return false;
    }
    return true;
}

static_assert(everyRowHasAFactory(),
              "a pluginEditorRegistry row names an editor with no factory -- add it to AESTRA_CORE_PLUGIN_EDITORS");
static_assert(everyFactoryHasARow(), "an editor factory has no pluginEditorRegistry row");
static_assert(sameName(pluginEditorRegistry.front().editorName, "GenericPluginEditor"),
              "the first registry row is the generic fallback");

struct NamedFactory {
    const char* name;
    PluginEditorFactory factory;
};

constexpr size_t kEditorCount = sizeof(kEditorNames) / sizeof(kEditorNames[0]);

const std::array<NamedFactory, kEditorCount>& pluginEditorFactories() {
#define AESTRA_EDITOR_FACTORY(T) NamedFactory{#T, factoryFor<T>()},
    static const std::array<NamedFactory, kEditorCount> kFactories{
        {AESTRA_CORE_PLUGIN_EDITORS(AESTRA_EDITOR_FACTORY) AESTRA_PREMIUM_PLUGIN_EDITORS(AESTRA_EDITOR_FACTORY)}};
#undef AESTRA_EDITOR_FACTORY
    return kFactories;
}

const PluginEditorFactory& factoryNamed(const char* editorName) {
    const auto& factories = pluginEditorFactories();
    for (const NamedFactory& f : factories)
        if (std::strcmp(f.name, editorName) == 0)
            return f.factory;
    // Unreachable: the static_asserts above prove every registry name has a
    // factory. Kept as the generic editor rather than a null factory.
    return factories.front().factory;
}

} // namespace

const PluginEditorFactory& pluginEditorFactoryFor(const std::string& pluginId) {
    return factoryNamed(pluginEditorNameFor(pluginId));
}

} // namespace AestraUI
