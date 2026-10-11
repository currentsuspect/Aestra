// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// FileBrowser, split by concern. This file: the library model behind the pane: places,
// favorites, collections, the library root and the background index.
// Split out of FileBrowser.cpp so no one file has to hold the whole browser;
// see Tests/Guards/file_size_ratchet.cmake.
#include "FileBrowserInternal.h"

using namespace Aestra;

namespace AestraUI {

using namespace FileBrowserInternal;

void FileBrowser::initLibraryState(const std::string& statePath) {
    statePath_ = statePath;
    std::error_code ec;
    if (!statePath.empty() && std::filesystem::exists(statePath, ec)) {
        loadState(statePath);
    } else {
        const std::string legacy = legacySettingsPath();
        if (!legacy.empty() && std::filesystem::exists(legacy, ec) && loadState(legacy)) {
            persistState(); // carry the import forward into the new file
        }
    }
    rescanLibrary(); // library-wide search; runs in the background, every launch
}

void FileBrowser::addToFavorites(const std::string& path) {
    const std::string key = mapKeyForPath(path);
    if (key.empty()) return;
    if (std::find(favoritesPaths_.begin(), favoritesPaths_.end(), key) != favoritesPaths_.end()) return;
    favoritesPaths_.push_back(key);
    persistState();
    if (listingKind_ == ListingKind::Favorites) reissueListing();
}

void FileBrowser::removeFromFavorites(const std::string& path) {
    const std::string key = mapKeyForPath(path);
    auto it = std::remove(favoritesPaths_.begin(), favoritesPaths_.end(), key);
    if (it == favoritesPaths_.end()) return;
    favoritesPaths_.erase(it, favoritesPaths_.end());
    persistState();
    if (listingKind_ == ListingKind::Favorites) reissueListing();
}

void FileBrowser::addPlace(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) return;

    const std::string key = canonicalOrNormalized(std::filesystem::path(path)).string();
    if (key.empty() || isPlace(key)) return;

    customPlacePaths_.push_back(key);
    persistState();
    if (libraryIndex_) rescanLibrary();
    invalidateCache();
}

void FileBrowser::removePlace(const std::string& path) {
    const std::string key = mapKeyForPath(canonicalOrNormalized(std::filesystem::path(path)).string());
    auto it = std::remove_if(customPlacePaths_.begin(), customPlacePaths_.end(),
                             [&](const std::string& place) { return mapKeyForPath(place) == key; });
    if (it == customPlacePaths_.end()) return;
    customPlacePaths_.erase(it, customPlacePaths_.end());
    persistState();
    if (libraryIndex_) rescanLibrary();
    invalidateCache();
}

bool FileBrowser::isPlace(const std::string& path) const {
    if (path.empty()) return false;
    const std::string key = mapKeyForPath(canonicalOrNormalized(std::filesystem::path(path)).string());
    return std::any_of(customPlacePaths_.begin(), customPlacePaths_.end(),
                       [&](const std::string& place) { return mapKeyForPath(place) == key; });
}

void FileBrowser::setLibraryRoot(const std::string& path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_directory(path, ec)) return;
    rootPath_ = canonicalOrNormalized(std::filesystem::path(path)).string();
    persistState();
    if (libraryIndex_) rescanLibrary();
    invalidateCache();
}

bool FileBrowser::createCollection(const std::string& rawName) {
    const std::string name = trimName(rawName);
    if (name.empty() || std::find(collections_.begin(), collections_.end(), name) != collections_.end()) {
        return false;
    }
    collections_.push_back(name);
    persistState();
    invalidateCache();
    return true;
}

std::string FileBrowser::createUntitledCollection() {
    std::string name = "New Collection";
    for (int n = 2; std::find(collections_.begin(), collections_.end(), name) != collections_.end(); ++n) {
        name = "New Collection " + std::to_string(n);
    }
    createCollection(name);
    return name;
}

bool FileBrowser::renameCollection(const std::string& from, const std::string& rawTo) {
    const std::string to = trimName(rawTo);
    auto it = std::find(collections_.begin(), collections_.end(), from);
    if (it == collections_.end() || to.empty() || to == from ||
        std::find(collections_.begin(), collections_.end(), to) != collections_.end()) {
        return false;
    }
    *it = to;
    // The collection IS the tag: every member follows the new name.
    for (auto& [pathKey, tags] : tagsByPath_) {
        const bool hadOld = std::find(tags.begin(), tags.end(), from) != tags.end();
        if (!hadOld) continue;
        tags.erase(std::remove(tags.begin(), tags.end(), from), tags.end());
        if (std::find(tags.begin(), tags.end(), to) == tags.end()) tags.push_back(to);
    }
    if (activeTagFilter_ == from) activeTagFilter_ = to;
    if (listingKind_ == ListingKind::Collection && listingTag_ == from) {
        listingTag_ = to;
        listingTitle_ = to;
    }
    persistState();
    invalidateCache();
    return true;
}

void FileBrowser::deleteCollection(const std::string& name) {
    auto it = std::find(collections_.begin(), collections_.end(), name);
    if (it == collections_.end()) return;
    collections_.erase(it);
    for (auto tagIt = tagsByPath_.begin(); tagIt != tagsByPath_.end();) {
        auto& tags = tagIt->second;
        tags.erase(std::remove(tags.begin(), tags.end(), name), tags.end());
        tagIt = tags.empty() ? tagsByPath_.erase(tagIt) : std::next(tagIt);
    }
    if (activeTagFilter_ == name) activeTagFilter_.clear();
    persistState();
    if (listingKind_ == ListingKind::Collection && listingTag_ == name) {
        exitListing();
    }
    invalidateCache();
}

void FileBrowser::beginCollectionRename(const std::string& name) {
    if (name.empty() || std::find(collections_.begin(), collections_.end(), name) == collections_.end()) return;
    if (navEditor_) finishCollectionRename(true);

    auto& themeManager = NUIThemeManager::getInstance();
    auto editor = std::make_shared<NUITextInput>();
    editor->setMaxLength(48);
    editor->setText(name);
    editor->setTextColor(themeManager.getColor("textPrimary"));
    editor->setBackgroundColor(themeManager.getColor("backgroundPrimary"));
    editor->setJustification(NUITextInput::Justification::Left);
    editor->setPadding(4.0f);
    editor->setBorderRadius(4.0f);
    editor->setOnReturnKey([this]() { finishCollectionRename(true); });
    editor->setOnEscapeKey([this]() { finishCollectionRename(false); });
    editor->setOnFocusLost([this]() { finishCollectionRename(true); });
    navEditorTarget_ = name;
    navEditor_ = editor;
    addChild(editor);
    // House order for a transient editor: focus, caret at end, then select-all
    // so typing replaces the name.
    editor->setFocused(true);
    editor->setCaretPosition(static_cast<int>(name.size()));
    editor->selectAll();
    invalidateCache(); // the nav paint places the editor over the row
}

void FileBrowser::finishCollectionRename(bool accept) {
    if (!navEditor_) return; // re-entry: unfocusing below fires onFocusLost again
    std::shared_ptr<NUITextInput> editor = std::move(navEditor_);
    navEditor_.reset();
    const std::string target = std::move(navEditorTarget_);
    navEditorTarget_.clear();
    const std::string text = editor->getText();

    editor->setVisible(false);
    if (editor->isFocused()) editor->setFocused(false);
    // Still executing inside one of its callbacks: detach and free it later.
    retiredNavEditors_.push_back(std::move(editor));

    if (accept) renameCollection(target, text);
    invalidateCache();
}

void FileBrowser::showCollectionContextMenu(const std::string& name, const NUIPoint& position) {
    if (!popupMenu_) return;
    popupMenu_->clear();
    popupMenuTargetPath_.clear();
    popupMenuTargetIsDirectory_ = false;

    popupMenu_->addItem("Open", [this, name]() {
        activeNavAction_ = BrowserNavAction::Collection;
        showCollection(name);
    });
    popupMenu_->addItem("Rename...", [this, name]() { beginCollectionRename(name); });
    popupMenu_->addSeparator();
    int members = 0;
    for (const auto& [_, tags] : tagsByPath_) {
        if (std::find(tags.begin(), tags.end(), name) != tags.end()) ++members;
    }
    // Deleting a collection only ungroups: no file is touched. Say so.
    const std::string label = members > 0 ? "Delete Collection (ungroups " + std::to_string(members) +
                                                (members == 1 ? " item)" : " items)")
                                          : "Delete Collection";
    popupMenu_->addItem(label, [this, name]() { deleteCollection(name); });

    attachAndShowPopupMenu(this, popupMenu_, position);
    invalidateCache();
}

void FileBrowser::rescanLibrary() {
    if (!libraryIndex_) libraryIndex_ = std::make_unique<BrowserLibraryIndex>();
    libraryIndex_->rebuild(indexRoots());
}

bool FileBrowser::isLibraryIndexing() const {
    return libraryIndex_ && libraryIndex_->isCrawling();
}

void FileBrowser::setSearchWholeLibrary(bool wholeLibrary) {
    if (searchWholeLibrary_ == wholeLibrary) return;
    searchWholeLibrary_ = wholeLibrary;
    persistState();
    if (isFilterActive()) applyFilter();
}

void FileBrowser::showFavorites() {
    beginListing(ListingKind::Favorites, "Favorites", favoritesPaths_);
}

void FileBrowser::showCollection(const std::string& tag) {
    if (tag.empty()) return;
    listingTag_ = tag;
    beginListing(ListingKind::Collection, tag, pathsWithTag(tag));
}

bool FileBrowser::isFavorite(const std::string& path) const {
    const std::string key = mapKeyForPath(path);
    return !key.empty() && (std::find(favoritesPaths_.begin(), favoritesPaths_.end(), key) != favoritesPaths_.end());
}

void FileBrowser::toggleFavorite(const std::string& path) {
    if (isFavorite(path)) {
        removeFromFavorites(path);
    } else {
        addToFavorites(path);
    }
    invalidateCache();
}

} // namespace AestraUI
