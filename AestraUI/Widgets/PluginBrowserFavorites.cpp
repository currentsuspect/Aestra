// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Plugin browser: favorites persistence and recents (V8-S5), split out of PluginBrowserPanel.cpp
// to keep that file under the size cap.

#include "PluginBrowserPanel.h"

#include "AestraJSON.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace AestraUI {

namespace {
std::filesystem::path getFavoritesPath() {
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";
    auto dir = std::filesystem::path(home) / ".config" / "aestra";
    std::filesystem::create_directories(dir);
    return dir / "favorites.json";
}
} // namespace

void PluginBrowserPanel::loadFavorites() {
    m_favoritesSet.clear();
    auto path = getFavoritesPath();
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (content.empty()) return;
    try {
        Aestra::JSON j = Aestra::JSON::parse(content);
        if (j.has("favorites") && j["favorites"].isArray()) {
            auto arr = j["favorites"].asArray();
            for (size_t i = 0; i < arr.size(); ++i) {
                if (arr[i].isString()) {
                    m_favoritesSet.insert(arr[i].asString());
                }
            }
        }
    } catch (...) {}
}

void PluginBrowserPanel::setFavoritesProvider(FavoritesLoader load, FavoritesSaver save) {
    m_favoritesSaver = std::move(save);
    if (const auto stored = load ? load() : std::nullopt) {
        m_favoritesSet = std::unordered_set<std::string>(stored->begin(), stored->end());
        for (auto* list : {&m_allPlugins, &m_filteredPlugins}) {
            for (auto& p : *list) p.isFavorite = m_favoritesSet.count(p.id) > 0;
        }
        applyFilters();
    } else if (m_favoritesSaver && !m_favoritesSet.empty()) {
        saveFavorites(); // one-way import of favorites.json into the store
    }
}

void PluginBrowserPanel::saveFavorites() {
    if (m_favoritesSaver) {
        std::vector<std::string> ids(m_favoritesSet.begin(), m_favoritesSet.end());
        std::sort(ids.begin(), ids.end()); // a stable order on disk
        m_favoritesSaver(ids);
        return;
    }
    Aestra::JSON j = Aestra::JSON::object();
    j.set("version", Aestra::JSON(1.0));
    Aestra::JSON arr = Aestra::JSON::array();
    for (const auto& id : m_favoritesSet) {
        arr.push(Aestra::JSON(id));
    }
    j.set("favorites", arr);
    std::ofstream f(getFavoritesPath());
    if (f.is_open()) f << j.toString(2);
}

void PluginBrowserPanel::toggleFavorite(const std::string& pluginId) {
    if (m_favoritesSet.count(pluginId)) {
        m_favoritesSet.erase(pluginId);
    } else {
        m_favoritesSet.insert(pluginId);
    }
    bool isFav = m_favoritesSet.count(pluginId) > 0;
    auto patch = [&](std::vector<PluginListItem>& list) {
        for (auto& p : list)
            if (p.id == pluginId) { p.isFavorite = isFav; break; }
    };
    patch(m_allPlugins);
    patch(m_filteredPlugins);
    saveFavorites();
    applyFilters();
}

std::vector<std::string> PluginBrowserPanel::getFavorites() const {
    return std::vector<std::string>(m_favoritesSet.begin(), m_favoritesSet.end());
}


// --- Recents -----------------------------------------------------------------

int PluginBrowserPanel::recentRank(const std::string& id) const {
    const auto it = std::find(m_recents.begin(), m_recents.end(), id);
    return it == m_recents.end() ? -1 : static_cast<int>(it - m_recents.begin());
}

void PluginBrowserPanel::recordRecent(const std::string& pluginId) {
    if (pluginId.empty()) return;
    m_recents.erase(std::remove(m_recents.begin(), m_recents.end(), pluginId), m_recents.end());
    m_recents.insert(m_recents.begin(), pluginId);
    if (m_recents.size() > kMaxRecents) m_recents.resize(kMaxRecents);
    if (m_recentsSaver) m_recentsSaver(m_recents);
}

void PluginBrowserPanel::setShowRecentOnly(bool show) {
    m_showRecentOnly = show;
    applyFilters();
}

void PluginBrowserPanel::setRecentsProvider(FavoritesLoader load, FavoritesSaver save) {
    m_recentsSaver = std::move(save);
    if (const auto stored = load ? load() : std::nullopt) {
        m_recents = *stored;
        if (m_recents.size() > kMaxRecents) m_recents.resize(kMaxRecents);
        if (m_showRecentOnly) applyFilters();
    }
}

void PluginBrowserPanel::requestLoad(const PluginListItem& plugin) {
    recordRecent(plugin.id);
    if (m_onPluginLoadRequested) m_onPluginLoadRequested(plugin);
}

} // namespace AestraUI
