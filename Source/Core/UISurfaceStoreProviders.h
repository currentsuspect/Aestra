// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Callbacks over the app's one UISurfaceStoreFile (found through ServiceLocator), for
// AestraUI widgets that persist a preference but must not include the store (V8-C14).
// A missing store reads as "nothing stored" and writes nothing.

#include "../App/ServiceLocator.h"
#include "UISurfaceStore.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {

inline std::function<std::optional<std::vector<std::string>>()> storedListLoader(std::string key) {
    return [key = std::move(key)]() -> std::optional<std::vector<std::string>> {
        auto* store = ServiceLocator::get<UISurfaceStoreFile>();
        return store ? store->listPreference(key) : std::nullopt;
    };
}

inline std::function<void(const std::vector<std::string>&)> storedListSaver(std::string key) {
    return [key = std::move(key)](const std::vector<std::string>& value) {
        if (auto* store = ServiceLocator::get<UISurfaceStoreFile>()) {
            store->setListPreference(key, value);
        }
    };
}

} // namespace Aestra
