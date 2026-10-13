// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Components/FileBrowser.h"
#include "Models/TrackManager.h"
#include "Panels/ArsenalPanel.h"

#include <functional>

namespace Aestra {

/**
 * @brief Connects the Arsenal's sound drawer to the library's collections (V81-AR2, FD-46): a sound set IS a
 * collection, so the sets a user already keeps (right-click > Add to Collection) are the ones a row can walk.
 * Choosing a set for a unit is project data (a name on the unit); the set itself stays in the library.
 */
inline void wireArsenalSoundSets(Audio::ArsenalPanel& panel, const std::shared_ptr<AestraUI::FileBrowser>& browser,
                                 const std::shared_ptr<Audio::TrackManager>& trackManager, std::function<void()> markDirty) {
    std::weak_ptr<AestraUI::FileBrowser> weakBrowser = browser;
    panel.setSoundSetProviders(
        [weakBrowser]() { auto b = weakBrowser.lock(); return b ? b->getCollections() : std::vector<std::string>{}; },
        [weakBrowser](const std::string& name) { auto b = weakBrowser.lock(); return b ? b->getCollectionMembers(name) : std::vector<std::string>{}; },
        [trackManager, markDirty](Audio::UnitID unit, const std::string& name) {
            trackManager->getUnitManager().setUnitSoundSet(unit, name);
            if (markDirty) markDirty();
        });
}

} // namespace Aestra
