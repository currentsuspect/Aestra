// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// FileBrowser, split by concern. This file: the FD-47 chrome above the rows —
// the Sounds | Patterns | Plugins tabs, the search scope chip, the location row
// (back, the place chip that opens Places, breadcrumbs) and the colour-tag chips.
// Everything here is drawn into the static cache; the hit rects it records are
// read by handleChromeHits() and washed on hover by renderHoverOverlays().
#include "FileBrowserInternal.h"

using namespace Aestra;

namespace AestraUI {

using namespace FileBrowserInternal;

namespace {

constexpr float kChipH = 22.0f;

bool isUnder(const std::string& path, const std::string& base) {
    if (base.empty()) return false;
    const std::string p = mapKeyForPath(path);
    std::string b = mapKeyForPath(base);
    if (p == b) return true;
    if (!b.empty() && b.back() != '/' && b.back() != '\\') b += '/';
    return p.rfind(b, 0) == 0;
}

void drawDownChevron(NUIRenderer& renderer, float cx, float cy, const NUIColor& color) {
    renderer.drawLine({cx - 3.0f, cy - 1.5f}, {cx, cy + 1.5f}, 1.3f, color);
    renderer.drawLine({cx, cy + 1.5f}, {cx + 3.0f, cy - 1.5f}, 1.3f, color);
}

} // namespace

std::string FileBrowser::currentPlaceLabel(std::string* placePath) const {
    if (isShowingListing()) {
        if (placePath) placePath->clear();
        return listingTitle_;
    }
    // The longest place the current folder sits under names it.
    std::string bestLabel, bestPath;
    const auto consider = [&](const std::string& label, const std::string& path) {
        if (path.empty() || !isUnder(currentPath_, path)) return;
        if (mapKeyForPath(path).size() > mapKeyForPath(bestPath).size()) {
            bestLabel = label;
            bestPath = path;
        }
    };
    const auto leaf = [](const std::string& path) {
        const std::string name = std::filesystem::path(path).filename().string();
        return name.empty() ? path : name;
    };
    if (!rootPath_.empty()) {
        consider("Library", rootPath_);
        consider("User Library", (std::filesystem::path(rootPath_) / "User Library").string());
        consider("Packs", (std::filesystem::path(rootPath_) / "Packs").string());
    }
    for (const auto& place : systemPlaces_) consider(place.label, place.path);
    for (const auto& place : customPlacePaths_) consider(leaf(place), place);
    for (const auto& fav : favoritesPaths_) consider(leaf(fav), fav);
    if (bestPath.empty()) {
        bestPath = currentPath_;
        bestLabel = currentPath_.empty() ? "Library" : leaf(currentPath_);
    }
    if (placePath) *placePath = bestPath;
    return bestLabel;
}

void FileBrowser::renderKindTabs(NUIRenderer& renderer, const BrowserLayout& layout) {
    auto& theme = NUIThemeManager::getInstance();
    const auto& props = theme.getCurrentTheme();
    const NUIRect& first = layout.kinds[0];
    const NUIRect& last = layout.kinds[2];
    const NUIRect well(first.x - 2.0f, first.y - 2.0f, last.right() - first.x + 4.0f, first.height + 4.0f);
    renderer.fillRoundedRect(well, 4.0f, theme.getColor("backgroundPrimary").darkened(0.35f));
    renderer.strokeRoundedRect(well, 4.0f, 1.0f, theme.getColor("border").withAlpha(0.55f));

    static const char* kNames[3] = {"Sounds", "Patterns", "Plugins"};
    const int active = kindOf(activeNavAction_);
    for (int i = 0; i < 3; ++i) {
        const NUIRect& tab = layout.kinds[static_cast<size_t>(i)];
        const bool on = i == active;
        if (on) renderer.fillRoundedRect(tab, 3.0f, theme.getColor("accentPrimary").withAlpha(0.24f));
        renderer.drawTextCentered(kNames[i], tab, props.fontSizeS,
                                  on ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.80f));
        ChromeHit hit;
        hit.bounds = tab;
        hit.kind = i;
        chromeHits_.push_back(hit);
    }
}

void FileBrowser::renderSearchScope(NUIRenderer& renderer, const BrowserLayout& layout) {
    auto& theme = NUIThemeManager::getInstance();
    const auto& props = theme.getCurrentTheme();
    // The field: a recessed well from the icon to the end of the band.
    const NUIRect field(layout.searchBar.x + 10.0f, layout.search.y, std::max(0.0f, layout.searchBar.width - 20.0f),
                        layout.search.height);
    renderer.fillRoundedRect(field, 4.0f, theme.getColor("backgroundPrimary").darkened(0.35f));
    renderer.strokeRoundedRect(field, 4.0f, 1.0f, theme.getColor("border").withAlpha(0.55f));
    if (kindOf(activeNavAction_) != 0) return; // Patterns and Plugins search their own list

    const std::string label = searchWholeLibrary_ ? "Everywhere" : "Here";
    const float w = renderer.measureText(label, props.fontSizeXS).width + 22.0f;
    const NUIRect chip(field.right() - w - 4.0f, field.y + (field.height - 20.0f) * 0.5f, w, 20.0f);
    renderer.strokeRoundedRect(chip, 3.0f, 1.0f, theme.getColor("border").withAlpha(0.70f));
    renderer.drawText(label, {chip.x + 7.0f, std::round(renderer.calculateTextY(chip, props.fontSizeXS))}, props.fontSizeXS,
                      theme.getColor("textSecondary"));
    drawDownChevron(renderer, chip.right() - 8.0f, chip.y + chip.height * 0.5f, theme.getColor("textSecondary"));
    ChromeHit hit;
    hit.bounds = chip;
    hit.scopeChip = true;
    chromeHits_.push_back(hit);
}

void FileBrowser::renderLocationRow(NUIRenderer& renderer, const BrowserLayout& layout) {
    if (layout.location.isEmpty()) return;
    auto& theme = NUIThemeManager::getInstance();
    const auto& props = theme.getCurrentTheme();
    const NUIColor text = theme.getColor("textPrimary");
    const NUIColor muted = theme.getColor("textSecondary");
    const float font = props.fontSizeS;

    // Back
    {
        const NUIRect& b = layout.backButton;
        const float cx = b.x + b.width * 0.5f, cy = b.y + b.height * 0.5f;
        const NUIColor c = muted.withAlpha(navHistoryIndex_ > 0 ? 0.85f : 0.28f);
        renderer.drawLine({cx + 2.0f, cy - 4.0f}, {cx - 2.0f, cy}, 1.5f, c);
        renderer.drawLine({cx - 2.0f, cy}, {cx + 2.0f, cy + 4.0f}, 1.5f, c);
    }

    // Place chip: the place this folder sits in, opening Places.
    std::string placePath;
    const std::string place = currentPlaceLabel(&placePath);
    const NUIRect& area = layout.pathLabel;
    const float maxChip = std::max(60.0f, area.width * 0.55f);
    const std::string chipText = ellipsizeEnd(renderer, place, font, maxChip - 30.0f);
    const float chipW = renderer.measureText(chipText, font).width + 30.0f;
    const NUIRect chip(area.x, area.y, chipW, kChipH);
    if (placesOpen_) renderer.fillRoundedRect(chip, 3.0f, theme.getColor("accentPrimary").withAlpha(0.20f));
    renderer.strokeRoundedRect(chip, 3.0f, 1.0f, theme.getColor("border").withAlpha(0.75f));
    const bool favourite = !placePath.empty() &&
                           std::find(favoritesPaths_.begin(), favoritesPaths_.end(), placePath) != favoritesPaths_.end();
    renderer.fillCircle({chip.x + 9.0f, chip.y + chip.height * 0.5f}, 3.0f,
                        favourite ? theme.getColor("warning") : theme.getColor("accentPrimary"));
    renderer.drawText(chipText, {chip.x + 17.0f, std::round(renderer.calculateTextY(chip, font))}, font, text);
    drawDownChevron(renderer, chip.right() - 8.0f, chip.y + chip.height * 0.5f, muted);
    {
        ChromeHit hit;
        hit.bounds = chip;
        hit.placeChip = true;
        chromeHits_.push_back(hit);
    }

    // Breadcrumbs from the place down to here; the last is bold-white, older ones
    // fold into "…" from the left when the row runs out.
    if (isShowingListing() || placePath.empty()) return;
    std::vector<std::pair<std::string, std::string>> crumbs; // label, path
    {
        std::filesystem::path p(currentPath_);
        const std::string base = mapKeyForPath(placePath);
        while (!p.empty() && mapKeyForPath(p.string()) != base && p.has_parent_path() && p.parent_path() != p) {
            crumbs.emplace_back(p.filename().string(), p.string());
            p = p.parent_path();
        }
        std::reverse(crumbs.begin(), crumbs.end());
    }
    if (crumbs.empty()) return;
    const float sepW = 14.0f;
    float room = area.right() - chip.right() - 4.0f;
    std::vector<float> widths;
    for (const auto& c : crumbs) widths.push_back(renderer.measureText(c.first, font).width);
    size_t first = 0;
    float total = 0.0f;
    for (float w : widths) total += w + sepW;
    while (first + 1 < crumbs.size() && total > room) {
        total -= widths[first] + sepW;
        ++first;
    }
    float x = chip.right() + 4.0f;
    const float cy = chip.y + chip.height * 0.5f;
    if (first > 0) {
        renderer.drawText("\xe2\x80\xa6", {x + 3.0f, std::round(renderer.calculateTextY(chip, font))}, font, muted);
        x += sepW;
        room -= sepW;
    }
    for (size_t i = first; i < crumbs.size(); ++i) {
        // separator
        renderer.drawLine({x + 5.0f, cy - 3.0f}, {x + 8.0f, cy}, 1.2f, muted.withAlpha(0.45f));
        renderer.drawLine({x + 8.0f, cy}, {x + 5.0f, cy + 3.0f}, 1.2f, muted.withAlpha(0.45f));
        x += sepW;
        const bool lastCrumb = i + 1 == crumbs.size();
        const float avail = std::max(0.0f, area.right() - x);
        const std::string label = ellipsizeEnd(renderer, crumbs[i].first, font, avail);
        renderer.drawText(label, {x, std::round(renderer.calculateTextY(chip, font))}, font,
                          lastCrumb ? text : muted.withAlpha(0.85f));
        const float w = renderer.measureText(label, font).width;
        if (!lastCrumb) {
            ChromeHit hit;
            hit.bounds = NUIRect(x - 2.0f, chip.y, w + 4.0f, chip.height);
            hit.path = crumbs[i].second;
            chromeHits_.push_back(hit);
        }
        x += w;
        if (x >= area.right()) break;
    }
}

void FileBrowser::renderTagChips(NUIRenderer& renderer, const BrowserLayout& layout) {
    if (layout.chips.isEmpty()) return;
    auto& theme = NUIThemeManager::getInstance();
    const auto& props = theme.getCurrentTheme();
    const float font = props.fontSizeXS;
    float x = layout.chips.x + 10.0f;
    const float y = layout.chips.y + (layout.chips.height - 20.0f) * 0.5f;
    const auto drawChip = [&](const std::string& label, const std::string& tag, bool dot) {
        const float w = renderer.measureText(label, font).width + (dot ? 28.0f : 18.0f);
        if (x + w > layout.chips.right() - 10.0f) return false;
        const NUIRect r(x, y, w, 20.0f);
        const bool on = activeTagFilter_ == tag;
        if (on) {
            renderer.fillRoundedRect(r, 10.0f, theme.getColor("textPrimary"));
        } else {
            renderer.strokeRoundedRect(r, 10.0f, 1.0f, theme.getColor("border").withAlpha(0.75f));
        }
        float tx = r.x + 9.0f;
        if (dot) {
            renderer.fillCircle({tx + 3.0f, r.y + r.height * 0.5f}, 3.5f, collectionColor(tag));
            tx += 10.0f;
        }
        renderer.drawText(label, {tx, std::round(renderer.calculateTextY(r, font))}, font,
                          on ? theme.getColor("backgroundPrimary") : theme.getColor("textSecondary"));
        ChromeHit hit;
        hit.bounds = r;
        hit.hasTag = true;
        hit.tag = tag;
        chromeHits_.push_back(hit);
        x += w + 4.0f;
        return true;
    };
    drawChip("All", "", false);
    for (const auto& name : collections_) {
        if (!drawChip(name, name, true)) break;
    }
}

void FileBrowser::renderListHeader(NUIRenderer& renderer, const BrowserLayout& layout) {
    auto& themeManager = NUIThemeManager::getInstance();
    const NUIColor border = themeManager.getColor("border").withAlpha(0.38f);
    const NUIColor muted = themeManager.getColor("textSecondary");
    const NUIColor accent = themeManager.getColor("accentPrimary");
    const auto& themeProps = themeManager.getCurrentTheme();

    // FD-47: spaced caps with a count, as the mixer and Arsenal label their lists.
    int sounds = 0, folders = 0;
    for (const auto* item : getActiveView()) {
        if (!item || item->isPlaceholder || item->depth != 0) continue;
        if (item->isDirectory) ++folders; else ++sounds;
    }
    float x = layout.listHeader.x + 12.0f;
    const float font = themeProps.fontSizeXS;
    const auto label = [&](const std::string& name, int count) {
        renderer.drawText(name, {x, std::round(renderer.calculateTextY(layout.listHeader, font))}, font,
                          muted.withAlpha(0.80f));
        x += renderer.measureText(name, font).width + 5.0f;
        const std::string n = std::to_string(count);
        renderer.drawText(n, {x, std::round(renderer.calculateTextY(layout.listHeader, font))}, font,
                          muted.withAlpha(0.45f));
        x += renderer.measureText(n, font).width + 14.0f;
    };
    if (folders > 0) label("FOLDERS", folders);
    label("SOUNDS", sounds);

    renderer.drawLine({layout.listHeader.x, layout.listHeader.bottom()},
                      {layout.listHeader.right(), layout.listHeader.bottom()},
                      1.0f, border);

    const bool filterActive = isFilterActive();
    const NUIColor controlColor = filterActive ? accent.withAlpha(0.94f) : muted.withAlpha(0.62f);
    const float filterCx = layout.filterButton.x + layout.filterButton.width * 0.5f;
    const float filterCy = layout.filterButton.y + layout.filterButton.height * 0.5f;
    renderer.drawLine({filterCx - 5.0f, filterCy - 4.0f}, {filterCx + 5.0f, filterCy - 4.0f}, 1.2f, controlColor);
    renderer.drawLine({filterCx - 3.0f, filterCy}, {filterCx + 3.0f, filterCy}, 1.2f, controlColor);
    renderer.drawLine({filterCx - 1.0f, filterCy + 4.0f}, {filterCx + 1.0f, filterCy + 4.0f}, 1.2f, controlColor);
    if (filterActive) renderer.fillCircle({layout.filterButton.right() - 5.0f, layout.filterButton.y + 5.0f}, 2.0f, accent);

    const NUIColor sortColor = muted.withAlpha(0.62f);
    if (layout.sortButton.width > 30.0f) {
        const char* sortLabel = "Name";
        switch (sortMode_) {
            case SortMode::Name: sortLabel = "Name"; break;
            case SortMode::Type: sortLabel = "Type"; break;
            case SortMode::Size: sortLabel = "Size"; break;
            case SortMode::Modified: sortLabel = "Date"; break;
            case SortMode::Length: sortLabel = "Length"; break;
            case SortMode::Bpm: sortLabel = "BPM"; break;
        }
        const NUIRect labelRect(layout.sortButton.x + 6.0f, layout.sortButton.y, layout.sortButton.width - 28.0f,
                                layout.sortButton.height);
        renderer.drawText(sortLabel,
                          {labelRect.x, std::round(renderer.calculateTextY(labelRect, themeProps.fontSizeXS))},
                          themeProps.fontSizeXS, muted.withAlpha(0.86f));
    }
    const float sortCx = layout.sortButton.right() - 11.0f;
    const float sortCy = layout.sortButton.y + layout.sortButton.height * 0.5f;
    renderer.drawLine({sortCx - 5.0f, sortCy - 4.0f}, {sortCx + 2.0f, sortCy - 4.0f}, 1.2f, sortColor);
    renderer.drawLine({sortCx - 5.0f, sortCy}, {sortCx, sortCy}, 1.2f, sortColor);
    renderer.drawLine({sortCx - 5.0f, sortCy + 4.0f}, {sortCx - 2.0f, sortCy + 4.0f}, 1.2f, sortColor);
    renderer.drawLine({sortCx + 5.0f, sortCy - 4.0f}, {sortCx + 5.0f, sortCy + 4.0f}, 1.2f, sortColor);
    const float arrowDirection = sortAscending_ ? -1.0f : 1.0f;
    renderer.drawLine({sortCx + 2.5f, sortCy + arrowDirection * 1.5f},
                      {sortCx + 5.0f, sortCy + arrowDirection * 4.0f}, 1.2f, sortColor);
    renderer.drawLine({sortCx + 7.5f, sortCy + arrowDirection * 1.5f},
                      {sortCx + 5.0f, sortCy + arrowDirection * 4.0f}, 1.2f, sortColor);
}

bool FileBrowser::handleChromeHits(const NUIMouseEvent& event) {
    if (event.cursorCaptured) return false;
    int hovered = -1;
    for (int i = 0; i < static_cast<int>(chromeHits_.size()); ++i) {
        if (chromeHits_[static_cast<size_t>(i)].bounds.contains(event.position)) {
            hovered = i;
            break;
        }
    }
    if (hovered != hoveredChromeHit_) {
        hoveredChromeHit_ = hovered;
        setDirty(true); // hover overlay only
    }
    if (hovered < 0 || !event.pressed || event.button != NUIMouseButton::Left) return false;

    const ChromeHit hit = chromeHits_[static_cast<size_t>(hovered)];
    setFocused(true); // so Esc closes Places and arrows move through the rows
    if (hit.kind >= 0) {
        selectKind(hit.kind);
    } else if (hit.placeChip) {
        setPlacesOpen(!placesOpen_);
    } else if (hit.scopeChip) {
        setSearchWholeLibrary(!searchWholeLibrary_);
        invalidateCache();
    } else if (hit.hasTag) {
        setPlacesOpen(false);
        activeTagFilter_ = hit.tag;
        applyFilter();
        invalidateCache();
    } else if (!hit.path.empty()) {
        setPlacesOpen(false);
        navigateTo(hit.path);
    }
    return true;
}

} // namespace AestraUI
