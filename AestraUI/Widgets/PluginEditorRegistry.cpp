// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// PluginEditorRegistry — the factories, half of P4's dispatch table.
//
// The id/name table is header-inline, so a test can check every plugin id without
// linking this file. What lives here is the part that cannot be checked without a
// live instance and a platform bridge: the lambdas that actually construct the
// eleven editors.

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

/// The factories, indexed to match `pluginEditorRegistry` in the header.
///
/// The static_assert is the load-bearing line: it ties the two tables together at
/// compile time, so a row added to one without a factory in the other is a build
/// failure rather than a null editor at runtime. Order is a human agreement
/// between two arrays, which is the one thing in this file that cannot be checked
/// by the compiler — so PluginEditorRegistryTest checks the id side, and this
/// assert checks the size side. A future refactor should replace the pairing with
/// a single table and accept the link cost, or put the factories in the header.
const std::array<PluginEditorFactory, pluginEditorRegistry.size()>& pluginEditorFactories() {
    static const std::array<PluginEditorFactory, pluginEditorRegistry.size()> kFactories{{
        factoryFor<GenericPluginEditor>(),
#ifdef AESTRAUI_ENABLE_PREMIUM_EDITORS
        factoryFor<RumblePluginEditor>(),
#endif
        factoryFor<AestraEQEditor>(),
        factoryFor<AestraCompEditor>(),
        factoryFor<AestraVerbEditor>(),
        factoryFor<AestraDelayEditor>(),
        factoryFor<AestraDriftEditor>(),
        factoryFor<AestraLimitEditor>(),
        factoryFor<AestraSatEditor>(),
        factoryFor<AestraFilterEditor>(),
        factoryFor<AestraOTTEditor>(),
        factoryFor<AestraLFOEditor>(),
        factoryFor<AestraTransientEditor>(),
    }};
    static_assert(kFactories.size() == pluginEditorRegistry.size(),
                  "every registry row needs a factory, or pluginEditorFactoryFor returns a null editor");
    return kFactories;
}

} // namespace

const PluginEditorFactory& pluginEditorFactoryFor(const std::string& pluginId) {
    const auto& registry = pluginEditorRegistry;
    const auto& factories = pluginEditorFactories();
    for (size_t i = 0; i < registry.size(); ++i) {
        if (registry[i].pluginId[0] != '\0' && std::strcmp(registry[i].pluginId, pluginId.c_str()) == 0)
            return factories[i];
    }
    return factories.front(); // the generic fallback
}

} // namespace AestraUI
