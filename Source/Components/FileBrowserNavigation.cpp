// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// FileBrowser, split by concern. This file: the navigation pane: places, favorites and
// collections rows, their layout, drawing, hit-testing and context menus.
// Split out of FileBrowser.cpp so no one file has to hold the whole browser;
// see Tests/Guards/file_size_ratchet.cmake.
#include "FileBrowserInternal.h"

using namespace Aestra;

namespace AestraUI {

using namespace FileBrowserInternal;

bool FileBrowser::usesCompactNavigation() const {
    return getNavPaneWidth() < 112.0f;
}

void FileBrowser::selectNavAction(BrowserNavAction action) {
    activeNavAction_ = action;
    activeNavPath_.clear();
    updateContentViews();
    if (onNavActionSelected_) {
        onNavActionSelected_(action);
    }
    invalidateCache();
}

void FileBrowser::renderNavigationPane(NUIRenderer& renderer, const BrowserLayout& layout) {
    auto& themeManager = NUIThemeManager::getInstance();
    navHits_.clear();

    const NUIColor paneBg = themeManager.getColor("backgroundSecondary");
    const NUIColor sectionColor = themeManager.getColor("textSecondary").withAlpha(0.58f);  // Stronger section headers
    const NUIColor rowText = themeManager.getColor("textPrimary").withAlpha(0.78f);
    const NUIColor muted = themeManager.getColor("textSecondary").withAlpha(0.48f);
    const NUIColor selectedBg = themeManager.getColor("accentPrimary").withAlpha(0.05f);
    const NUIColor divider = themeManager.getColor("divider");
    const bool compact = layout.navWidth < 112.0f;

    renderer.fillRect(layout.navPane, paneBg);
    renderer.setClipRect(layout.navPane);
    renderer.drawLine({layout.navPane.right(), layout.navPane.y},
                      {layout.navPane.right(), layout.navPane.bottom()},
                      1.0f, divider.withAlpha(0.58f));

    // One shared, compact navigation row. The browser no longer spends a full
    // second row on item counts and duplicate structural rules.
    const NUIRect& navHeader = layout.navHeader;
    renderer.fillRect(navHeader, themeManager.getColor("backgroundSecondary").darkened(0.03f));
    renderer.drawLine({navHeader.x, navHeader.bottom()}, {navHeader.right(), navHeader.bottom()},
                      1.0f, themeManager.getColor("border").withAlpha(0.42f));

    // Folder name at top (like breadcrumb on the right)
    const auto& themeProps = themeManager.getCurrentTheme();
    std::string folderName = "Browse";
    if (!currentPath_.empty()) {
        std::filesystem::path p(currentPath_);
        folderName = p.filename().string();
        if (folderName.empty()) folderName = currentPath_;
    }
    if (!compact) {
        renderer.drawText(
            folderName,
            {navHeader.x + themeProps.spacingM,
             std::round(renderer.calculateTextY(NUIRect(navHeader.x, navHeader.y + 5.0f, navHeader.width, 20.0f),
                                                themeProps.fontSizeXS))},
            themeProps.fontSizeXS, themeManager.getColor("textPrimary").withAlpha(0.76f));
    } else {
        const std::string compactLabel = "Library";
        const auto labelSize = renderer.measureText(compactLabel, 8.5f);
        renderer.drawText(compactLabel,
                          {navHeader.x + (navHeader.width - labelSize.width) * 0.5f,
                           std::round(renderer.calculateTextY(
                               NUIRect(navHeader.x, navHeader.y + 5.0f, navHeader.width, 20.0f), 8.5f))},
                          8.5f, themeManager.getColor("textSecondary").withAlpha(0.54f));
    }

    // Scrollable content region below the fixed folder-name header. Short
    // windows keep the tail rows reachable without moving the header. Start at the
    // full header height so the first nav row lines up with the first file row (the
    // list header is BROWSER_LIST_HEADER_H tall).
    const float navContentTop = layout.navViewport.y;
    navViewportHeight_ = layout.navViewport.height;
    const float navMaxScroll = std::max(0.0f, navContentHeight_ - navViewportHeight_);
    navScrollOffset_ = std::clamp(navScrollOffset_, 0.0f, navMaxScroll);
    renderer.setClipRect(layout.navViewport); // the same rect hit-testing uses

    // Start nav content at the same Y as the right column labels
    float y = navContentTop - navScrollOffset_;
    int hitIndex = 0;

    auto collectionCount = [&](const std::string& tag) {
        int count = 0;
        for (const auto& [_, tags] : tagsByPath_) {
            if (std::find(tags.begin(), tags.end(), tag) != tags.end()) {
                ++count;
            }
        }
        return count;
    };

    auto drawSection = [&](const std::string& label) {
        if (compact) {
            // No rule for label-less compact sections. The explicit drawDivider()
            // between groups already separates them; a section rule here doubled the
            // divider at group boundaries and drew a lone line that isolated the
            // first icon (the star) from the header.
            return;
        }
        std::string upper = label;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        NUIRect labelRect(layout.navPane.x + themeProps.spacingM, y, layout.navPane.width - themeProps.spacingM * 2.0f, 22.0f);
        renderer.drawText(upper, {labelRect.x, std::round(renderer.calculateTextY(labelRect, themeProps.fontSizeXS))}, themeProps.fontSizeXS, sectionColor);
        if (label == "Collections") {
            // Small up-chevron replacing "^" text glyph
            const float upCx = layout.navPane.right() - 14.0f;
            const float upCy = labelRect.y + labelRect.height * 0.5f;
            const float upS = 2.5f;
            renderer.drawLine({upCx - upS, upCy + upS * 0.5f}, {upCx, upCy - upS * 0.5f}, 1.3f, sectionColor.withAlpha(0.72f));
            renderer.drawLine({upCx, upCy - upS * 0.5f}, {upCx + upS, upCy + upS * 0.5f}, 1.3f, sectionColor.withAlpha(0.72f));
        }
        y += 22.0f;
    };

    auto drawIcon = [&](const NUIRect& rect, BrowserNavAction action, bool selected, const std::string& label) {
        const NUIColor iconColor = selected ? themeManager.getColor("textPrimary").withAlpha(0.86f) : muted.withAlpha(0.70f);
        const float cx = compact ? rect.x + rect.width * 0.5f : rect.x + 13.0f;
        const float cy = rect.y + rect.height * 0.5f;

        auto drawSvgIcon = [&](const char* svg) {
            // Keyed by the SVG literal: one action (SystemPlace) draws several glyphs.
            static std::unordered_map<const char*, std::shared_ptr<NUIIcon>> iconCache;
            const char* key = svg;
            auto it = iconCache.find(key);
            if (it == iconCache.end()) {
                auto icon = std::make_shared<NUIIcon>(svg);
                it = iconCache.emplace(key, icon).first;
            }
            auto& icon = it->second;
            icon->setBounds({std::round(cx - 8.0f), std::round(cy - 8.0f), 16.0f, 16.0f});
            icon->setColor(iconColor);
            icon->onRender(renderer);
        };

        switch (action) {
            case BrowserNavAction::Favorites:
                // The Aestra spark (the brand's five-point star with its cut), solid:
                // a thin stroked outline aliased badly at 16px, and a stock star
                // said nothing about whose favourites these are.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 1.9L14.65 9.46L22.65 9.64L15.79 13.8Q11.28 18.94 4.93 21.47L7.72 14.49L1.35 9.64L9.35 9.46Z"/><path d="M16.77 15.18L18.58 22.16L10.47 20.56Q13.97 18.37 16.77 15.18Z"/></svg>)");
                break;
            case BrowserNavAction::Collection:
                renderer.fillRoundedRect({cx - 4.0f, cy - 4.0f, 8.0f, 8.0f}, 4.0f,
                                         collectionColor(label).withAlpha(selected ? 0.98f : 0.82f));
                break;
            case BrowserNavAction::AddCollection:
                // A plus in a dashed-feeling ring: one cross subpath (evenodd rule).
                drawSvgIcon(R"(<svg viewBox="0 0 24 24"><path fill="currentColor" d="M11 6.5H13V11H17.5V13H13V17.5H11V13H6.5V11H11Z"/></svg>)");
                break;
            // The rail rasterizes every glyph at exactly 16x16 (see setBounds
            // above). These were all 1.8px stroked outlines, several of them
            // stacking six to twelve subpaths, which is the size at which thin
            // strokes stop resolving and a glyph turns into a smudge. They are
            // solid silhouettes now, with internal contrast cut out via
            // fill-rule="evenodd" so the background shows through instead of
            // being drawn as more competing lines.
            case BrowserNavAction::Sounds:
                // Solid eighth note.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="currentColor"><circle cx="8.4" cy="16.8" r="3.9"/><path d="M10.4 3.2h2.1v13.6h-2.1z"/><path d="M12.5 3.2c3.5 1.2 5.5 3.1 5.7 6.1-1.2-2.3-3.1-3.4-5.7-3.7z"/></svg>)");
                break;
            case BrowserNavAction::Drums:
                // Four solid pads — a pad grid rather than four outlined boxes.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="currentColor"><rect x="3.4" y="3.8" width="7.6" height="7.6" rx="1.8"/><rect x="13" y="3.8" width="7.6" height="7.6" rx="1.8"/><rect x="3.4" y="13" width="7.6" height="7.6" rx="1.8"/><rect x="13" y="13" width="7.6" height="7.6" rx="1.8"/></svg>)");
                break;
            case BrowserNavAction::Instruments:
                // Same keyboard treatment as the piano-roll glyph: black keys as
                // negative space, because in one flat colour a bright "black key"
                // is indistinguishable from a bright key divider.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M3 6H21V18H3Z M7.7 6H10.3V13H7.7Z M13.7 6H16.3V13H13.7Z M8.55 13H9.45V18H8.55Z M14.55 13H15.45V18H14.55Z"/></svg>)");
                break;
            case BrowserNavAction::AudioEffects:
                // Processor sliders: solid tracks with chunky handles.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="currentColor"><rect x="4.6" y="3.2" width="1.8" height="17.6" rx="0.9"/><rect x="11.1" y="3.2" width="1.8" height="17.6" rx="0.9"/><rect x="17.6" y="3.2" width="1.8" height="17.6" rx="0.9"/><rect x="2.4" y="6.4" width="6.2" height="3.6" rx="1.6"/><rect x="8.9" y="13" width="6.2" height="3.6" rx="1.6"/><rect x="15.4" y="8.6" width="6.2" height="3.6" rx="1.6"/></svg>)");
                break;
            case BrowserNavAction::Plugins:
                // A jack plug with its cable — you plug a plugin in. The old
                // glyph stacked eight pins and two nested outlines, twelve
                // subpaths fighting inside a 16px box.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24"><rect x="9.6" y="2.4" width="4.8" height="6.2" rx="1.4" fill="currentColor"/><rect x="6.8" y="8" width="10.4" height="7" rx="2.2" fill="currentColor"/><path d="M12 15.4v2.1a3.3 3.3 0 0 0 3.3 3.3h4.5" fill="none" stroke="currentColor" stroke-width="2.3" stroke-linecap="round" stroke-linejoin="round"/></svg>)");
                break;
            case BrowserNavAction::Patterns:
                // A step grid with an actual pattern in it, not a uniform block.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M4 5H20A2 2 0 0 1 22 7V17A2 2 0 0 1 20 19H4A2 2 0 0 1 2 17V7A2 2 0 0 1 4 5Z M5 8.2H8.2V11H5Z M10.4 8.2H13.6V11H10.4Z M15.8 8.2H19V11H15.8Z M5 13H8.2V15.8H5Z M15.8 13H19V15.8H15.8Z"/></svg>)");
                break;
            case BrowserNavAction::Clips:
                // A clip block with the play triangle cut out of it.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M5.6 5H18.4A2 2 0 0 1 20.4 7V17A2 2 0 0 1 18.4 19H5.6A2 2 0 0 1 3.6 17V7A2 2 0 0 1 5.6 5Z M10.2 8.7L16 12L10.2 15.3Z"/></svg>)");
                break;
            case BrowserNavAction::Samples:
                // Same mirrored peak envelope as the .wav file glyph, so a
                // sample reads the same wherever it appears.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24" fill="currentColor"><rect x="2.4" y="9" width="2.2" height="6" rx="1.1"/><rect x="6.8" y="5.4" width="2.2" height="13.2" rx="1.1"/><rect x="11.2" y="7.8" width="2.2" height="8.4" rx="1.1"/><rect x="15.6" y="4" width="2.2" height="16" rx="1.1"/><rect x="20" y="8.6" width="2.2" height="6.8" rx="1.1"/></svg>)");
                break;
            case BrowserNavAction::Packs:
                // Stacked sample cards, the front one carrying a waveform: a
                // pack is a set of sounds. (It was a gift box, which said
                // "present", and before that an isometric cube that read as a
                // scribble.) Every strip is at least 2.5 units tall so nothing
                // drops below a pixel at 16 px.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M7.4 1.9H16.6A1.3 1.3 0 0 1 17.9 3.2V4.4H6.1V3.2A1.3 1.3 0 0 1 7.4 1.9Z M5.4 5.4H18.6A1.3 1.3 0 0 1 19.9 6.7V7.9H4.1V6.7A1.3 1.3 0 0 1 5.4 5.4Z M4.2 9.2H19.8A1.7 1.7 0 0 1 21.5 10.9V19.3A1.7 1.7 0 0 1 19.8 21H4.2A1.7 1.7 0 0 1 2.5 19.3V10.9A1.7 1.7 0 0 1 4.2 9.2Z M6.4 13.9H8V16.1H6.4Z M9.6 12.3H11.2V17.7H9.6Z M12.8 13.2H14.4V16.8H12.8Z M16 12.8H17.6V17.2H16Z"/></svg>)");
                break;
            case BrowserNavAction::UserLibrary:
                // You, knocked out of the same tile the project file uses: the
                // library that belongs to this person.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M5 2.5H19A2.5 2.5 0 0 1 21.5 5V19A2.5 2.5 0 0 1 19 21.5H5A2.5 2.5 0 0 1 2.5 19V5A2.5 2.5 0 0 1 5 2.5Z M12 5.6A3.3 3.3 0 1 0 12.01 5.6Z M5.6 19.4C6.3 15.9 8.8 13.9 12 13.9S17.7 15.9 18.4 19.4Z"/></svg>)");
                break;
            case BrowserNavAction::CurrentProject:
            case BrowserNavAction::CustomPlace:
                // Same folder the file list uses: the cut front lip is what keeps
                // it from reading as the stock Material folder.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M2.5 6.3A1.8 1.8 0 0 1 4.3 4.5H9.2L11.2 6.5H19.7A1.8 1.8 0 0 1 21.5 8.3V17.7A1.8 1.8 0 0 1 19.7 19.5H4.3A1.8 1.8 0 0 1 2.5 17.7Z M4.5 9.3H19.5V10.8H4.5Z"/></svg>)");
                break;
            case BrowserNavAction::SystemPlace: {
                // Solid silhouettes, 16px house rules (evenodd cut-outs, no strokes).
                static const char* kHomeSvg =
                    R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M12 2.6L22 11.2H19.2V20.6H4.8V11.2H2Z M10 14H14V20.6H10Z"/></svg>)";
                static const char* kDownloadsSvg =
                    R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M10.6 2.8H13.4V10.4H17.2L12 15.8L6.8 10.4H10.6Z"/><path d="M3 15.6H5.8V18.4H18.2V15.6H21V21.2H3Z"/></svg>)";
                static const char* kDesktopSvg =
                    R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M3.8 3.6H20.2A1.8 1.8 0 0 1 22 5.4V15.4A1.8 1.8 0 0 1 20.2 17.2H3.8A1.8 1.8 0 0 1 2 15.4V5.4A1.8 1.8 0 0 1 3.8 3.6Z M4.4 6H19.6V14.8H4.4Z M10.4 17.2H13.6V19H16.8V21H7.2V19H10.4Z"/></svg>)";
                static const char* kMusicSvg =
                    R"(<svg viewBox="0 0 24 24" fill="currentColor"><circle cx="8.4" cy="16.8" r="3.9"/><path d="M10.4 3.2h2.1v13.6h-2.1z"/><path d="M12.5 3.2c3.5 1.2 5.5 3.1 5.7 6.1-1.2-2.3-3.1-3.4-5.7-3.7z"/></svg>)";
                static const char* kFolderSvg =
                    R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M2.5 6.3A1.8 1.8 0 0 1 4.3 4.5H9.2L11.2 6.5H19.7A1.8 1.8 0 0 1 21.5 8.3V17.7A1.8 1.8 0 0 1 19.7 19.5H4.3A1.8 1.8 0 0 1 2.5 17.7Z M4.5 9.3H19.5V10.8H4.5Z"/></svg>)";
                if (label == "Home") drawSvgIcon(kHomeSvg);
                else if (label == "Downloads") drawSvgIcon(kDownloadsSvg);
                else if (label == "Desktop") drawSvgIcon(kDesktopSvg);
                else if (label == "Music") drawSvgIcon(kMusicSvg);
                else drawSvgIcon(kFolderSvg);
                break;
            }
            case BrowserNavAction::AddFolder:
                // Folder with the plus cut out. The plus is one cross-shaped
                // subpath, not two overlapping bars — under evenodd, overlapping
                // holes cancel and would leave a filled square at the crossing.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M2.5 6.3A1.8 1.8 0 0 1 4.3 4.5H9.2L11.2 6.5H19.7A1.8 1.8 0 0 1 21.5 8.3V17.7A1.8 1.8 0 0 1 19.7 19.5H4.3A1.8 1.8 0 0 1 2.5 17.7Z M4.5 9.3H19.5V10.8H4.5Z M11.2 12.9H12.8V14.6H14.5V16.2H12.8V17.9H11.2V16.2H9.5V14.6H11.2Z"/></svg>)");
                break;
            default:
                // Generic list card.
                drawSvgIcon(R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M5 5H19A2 2 0 0 1 21 7V17A2 2 0 0 1 19 19H5A2 2 0 0 1 3 17V7A2 2 0 0 1 5 5Z M7.5 9H16.5V10.6H7.5Z M7.5 13.4H16.5V15H7.5Z"/></svg>)");
                break;
        }
    };

    auto drawRow = [&](BrowserNavAction action, const std::string& label, int count = -1, std::string path = {}) {
        NUIRect row(layout.navPane.x + 5.0f, y, std::max(0.0f, layout.navPane.width - 10.0f), BROWSER_NAV_ROW_H);
        const bool isPlaceRow = action == BrowserNavAction::CustomPlace || action == BrowserNavAction::SystemPlace;
        const bool placeActive = activeNavAction_ == BrowserNavAction::CustomPlace ||
                                 activeNavAction_ == BrowserNavAction::SystemPlace;
        bool selected = activeNavAction_ == action;
        if (isPlaceRow) {
            selected = placeActive && !isShowingListing() && !path.empty() &&
                       mapKeyForPath(activeNavPath_) == mapKeyForPath(path);
        } else if (action == BrowserNavAction::Collection) {
            selected = listingKind_ == ListingKind::Collection && listingTag_ == path;
        }
        if (selected) {
            renderer.fillRoundedRect(row, themeProps.radiusS, selectedBg);
            renderer.fillRoundedRect({row.x, row.y + 4.0f, 2.0f, row.height - 8.0f}, 1.0f,
                                     themeManager.getColor("accentPrimary").withAlpha(0.92f));
        }
        // Nav hover wash also lives in renderHoverOverlays(), outside the cache.
        drawIcon(row, action, selected, label);
        if (!compact) {
            renderer.drawText(label, {row.x + 33.0f, std::round(renderer.calculateTextY(row, themeProps.fontSizeS))},
                              themeProps.fontSizeS,
                              selected ? themeManager.getColor("textPrimary").withAlpha(0.90f) : rowText);
        }
        if (!compact && count > 0) {
            const std::string countText = std::to_string(count);
            const auto countSize = renderer.measureText(countText, themeManager.getFontSize("s"));
            renderer.drawText(countText,
                              {row.right() - countSize.width - 8.0f, std::round(renderer.calculateTextY(row, 11.0f))},
                              11.0f,
                              muted.withAlpha(selected ? 0.82f : 0.54f));
        }
        navHits_.push_back({action, row, label, std::move(path)});
        y += BROWSER_NAV_ROW_H;
        ++hitIndex;
    };

    auto drawDivider = [&]() {
        y += compact ? 3.0f : 5.0f;
        // Narrow rail: smaller inset so the rule reads as a divider, not a stub.
        const float divInset = compact ? 8.0f : 14.0f;
        renderer.drawLine({layout.navPane.x + divInset, y},
                          {layout.navPane.right() - divInset, y},
                          1.0f, divider.withAlpha(0.45f));
        y += compact ? 4.0f : 8.0f;
    };

    drawSection("Collections");
    y += 2.0f; // small gap so first nav row aligns with first file row
    drawRow(BrowserNavAction::Favorites, "Favorites", static_cast<int>(favoritesPaths_.size()));
    for (const auto& name : collections_) {
        drawRow(BrowserNavAction::Collection, name, collectionCount(name), name);
    }
    drawRow(BrowserNavAction::AddCollection, "+ New Collection");
    // The inline rename editor sits over its collection's row (rows move with
    // the nav scroll, so it is re-placed on every nav paint).
    if (navEditor_) {
        for (const auto& hit : navHits_) {
            if (hit.action == BrowserNavAction::Collection && hit.path == navEditorTarget_) {
                navEditor_->setBounds(NUIRect(hit.bounds.x + 28.0f, hit.bounds.y + 3.0f,
                                              std::max(40.0f, hit.bounds.width - 34.0f), hit.bounds.height - 6.0f));
                break;
            }
        }
    }
    drawDivider();
    drawSection("Places");
    drawRow(BrowserNavAction::Packs, "Packs");
    drawRow(BrowserNavAction::UserLibrary, "User Library");
    drawRow(BrowserNavAction::CurrentProject, "Current Project");
    for (const auto& place : customPlacePaths_) {
        std::filesystem::path p(place);
        std::string label = p.filename().string();
        if (label.empty()) {
            label = place;
        }
        drawRow(BrowserNavAction::CustomPlace, label, -1, place);
    }
    drawRow(BrowserNavAction::AddFolder, "+ Add Folder...");

    // Folders come before Categories: at ordinary window heights anything
    // after the eight category rows is below the fold, and Places/Computer
    // are how the user reaches their own sounds.
    if (!systemPlaces_.empty()) {
        drawSection("Computer");
        for (const auto& place : systemPlaces_) {
            drawRow(BrowserNavAction::SystemPlace, place.label, -1, place.path);
        }
    }
    drawDivider();
    drawSection("Categories");
    drawRow(BrowserNavAction::Sounds, "Sounds");
    drawRow(BrowserNavAction::Drums, "Drums");
    drawRow(BrowserNavAction::Instruments, "Instruments");
    drawRow(BrowserNavAction::AudioEffects, "Effects");
    drawRow(BrowserNavAction::Plugins, "Plugins");
    drawRow(BrowserNavAction::Patterns, "Patterns");
    drawRow(BrowserNavAction::Clips, "Clips");
    drawRow(BrowserNavAction::Samples, "Samples");

    // Drag-over visual for Places section
    if (m_isDragOverPlaces) {
        // Find the bounds of the Places section (from Packs to AddFolder)
        float placesTop = 0, placesBottom = 0;
        for (const auto& hit : navHits_) {
            if (hit.action == BrowserNavAction::Packs) placesTop = hit.bounds.y;
            if (hit.action == BrowserNavAction::AddFolder) placesBottom = hit.bounds.bottom();
        }
        if (placesTop > 0 && placesBottom > placesTop) {
            NUIRect placesOverlay = {layout.navPane.x, placesTop, layout.navPane.width, placesBottom - placesTop};
            renderer.fillRect(placesOverlay, themeManager.getColor("dragTarget"));
            renderer.strokeRoundedRect(placesOverlay, themeManager.getRadius("s"), 1.0f,
                                       themeManager.getColor("focusRing"));
        }
    }

    // Measure content for next frame's scroll clamp (y is post-offset screen
    // space; add the offset back to recover the intrinsic content height).
    navContentHeight_ = (y + navScrollOffset_) - navContentTop;

    // Restore the full-pane clip and draw a thin scroll thumb when content
    // overflows the available viewport.
    renderer.setClipRect(layout.navPane);
    const float navOverflow = navContentHeight_ - navViewportHeight_;
    if (navOverflow > 0.5f && navViewportHeight_ > 0.0f) {
        // Same painter and same gutter width as the file list beside it, so the
        // Collections rail is not a third scrollbar dialect (spec 2 §2).
        const float sbW = AestraUI::kOverlayScrollbarThickness;
        const NUIRect gutter(layout.navPane.right() - sbW - 2.0f, navContentTop, sbW, navViewportHeight_);
        const float thumbH = std::max(AestraUI::kOverlayScrollbarMinThumb,
                                      navViewportHeight_ * (navViewportHeight_ / navContentHeight_));
        const float thumbY = navContentTop + (navScrollOffset_ / navOverflow) * (navViewportHeight_ - thumbH);
        AestraUI::drawOverlayScrollbar(renderer, gutter, NUIRect(gutter.x, thumbY, sbW, thumbH),
                                       AestraUI::ScrollbarPaintState{});
    }

    renderer.clearClipRect();
}

bool FileBrowser::isPointOverPlacesSection(float x, float y) const {
    // Check if point falls within the Places section of the nav pane
    for (const auto& hit : navHits_) {
        if (hit.action == BrowserNavAction::Packs ||
            hit.action == BrowserNavAction::UserLibrary ||
            hit.action == BrowserNavAction::CurrentProject ||
            hit.action == BrowserNavAction::CustomPlace ||
            hit.action == BrowserNavAction::AddFolder) {
            if (hit.bounds.contains(x, y)) return true;
        }
    }
    return false;
}

void FileBrowser::onDropFileToPlaces(const std::string& path) {
    addPlace(path);
}

void FileBrowser::showPlaceContextMenu(const BrowserNavHit& hit, const NUIPoint& position) {
    if (!popupMenu_ || hit.path.empty()) return;

    popupMenu_->clear();
    popupMenuTargetPath_.clear();
    popupMenuTargetIsDirectory_ = false;

    const std::string path = hit.path;
    popupMenu_->addItem("Open", [this, path]() {
        navigateTo(path);
        activeNavAction_ = BrowserNavAction::CustomPlace;
        activeNavPath_ = currentPath_;
        invalidateCache();
    });
    popupMenu_->addItem(isFavorite(path) ? "Remove from Favorites" : "Add to Favorites",
                        [this, path]() { toggleFavorite(path); });
    if (hit.action == BrowserNavAction::CustomPlace) {
        popupMenu_->addSeparator();
        popupMenu_->addItem("Remove from Places", [this, path]() { removePlace(path); });
    }
    popupMenu_->addSeparator();
    popupMenu_->addItem("Copy Path", [path]() {
        if (auto* utils = Aestra::Platform::getUtils()) utils->setClipboardText(path);
    });

    attachAndShowPopupMenu(this, popupMenu_, position);
    invalidateCache();
}

bool FileBrowser::handleNavigationMouseEvent(const NUIMouseEvent& event, const BrowserLayout& layout) {
    if (event.cursorCaptured) return false;

    // Ignore the fixed folder-name header band: rows scrolled up under it are
    // visually clipped, so they must not be clickable there either. navViewport is
    // the rect the renderer clips to, so the two cannot disagree.
    const bool insideNav = layout.navViewport.contains(event.position);
    int newHovered = -1;
    if (insideNav) {
        for (int i = 0; i < static_cast<int>(navHits_.size()); ++i) {
            if (navHits_[i].bounds.contains(event.position)) {
                newHovered = i;
                break;
            }
        }
    }

    if (newHovered != hoveredNavIndex_) {
        hoveredNavIndex_ = newHovered;
        setDirty(true); // hover overlay only — no cache rebuild
    }

    if (usesCompactNavigation() && newHovered >= 0 && newHovered < static_cast<int>(navHits_.size())) {
        NUIPoint tooltipPosition = event.position;
        tooltipPosition.x = layout.navPane.right() + 8.0f;
        NUIComponent::showRemoteTooltip(navHits_[newHovered].label, tooltipPosition, this);
    } else if (layout.navPane.contains(event.position)) {
        // Only dismiss the navigation tooltip while the pointer is still in
        // this region. The file list shares this component as its tooltip
        // owner and will manage its own tooltip outside the navigation pane.
        NUIComponent::hideRemoteTooltip(this);
    }

    if (event.pressed && event.button == NUIMouseButton::Right && newHovered >= 0 &&
        newHovered < static_cast<int>(navHits_.size())) {
        const BrowserNavHit hit = navHits_[newHovered];
        switch (hit.action) {
            case BrowserNavAction::CustomPlace:
            case BrowserNavAction::SystemPlace:
                showPlaceContextMenu(hit, event.position);
                return true;
            case BrowserNavAction::Favorites:
                showFavoritesMenu(); // quick-jump list + Clear
                return true;
            case BrowserNavAction::AddFolder:
                showAddFolderMenu();
                return true;
            case BrowserNavAction::Collection:
                showCollectionContextMenu(hit.path, event.position);
                return true;
            default:
                return false;
        }
    }

    if (!event.pressed || event.button != NUIMouseButton::Left || newHovered < 0 ||
        newHovered >= static_cast<int>(navHits_.size())) {
        return false;
    }

    const BrowserNavAction previousAction = activeNavAction_;
    const BrowserNavAction action = navHits_[newHovered].action;
    activeNavAction_ = action;
    activeNavPath_ = navHits_[newHovered].path;
    updateContentViews();
    auto setFilter = [this](QuickFilter filter) {
        if (activeQuickFilter_ != filter) {
            activeQuickFilter_ = filter;
            applyFilter();
        } else {
            invalidateCache();
        }
    };

    switch (action) {
        // Favorites and Collections list their items from wherever they live,
        // not just the ones inside the folder currently on screen.
        case BrowserNavAction::Favorites:
            activeQuickFilter_ = QuickFilter::All;
            showFavorites();
            break;
        case BrowserNavAction::Collection:
            activeQuickFilter_ = QuickFilter::All;
            showCollection(navHits_[newHovered].path);
            break;
        case BrowserNavAction::AddCollection:
            activeNavAction_ = previousAction; // an action, not a destination
            updateContentViews();
            beginCollectionRename(createUntitledCollection());
            break;
        // Sounds narrows whatever is on screen to audio. Samples is a place:
        // the User Library folder for one-shots and loops, like Drums.
        case BrowserNavAction::Sounds:
            activeTagFilter_.clear();
            setFilter(QuickFilter::Audio);
            break;
        case BrowserNavAction::Samples: {
            auto path = std::filesystem::path(rootPath_) / "User Library" / "Samples";
            std::error_code ec;
            std::filesystem::create_directories(path, ec);
            activeTagFilter_.clear();
            activeQuickFilter_ = QuickFilter::Audio;
            navigateTo(path.string());
            applyFilter();
            break;
        }
        case BrowserNavAction::Drums: {
            auto path = std::filesystem::path(rootPath_) / "User Library" / "Drums";
            std::error_code ec;
            std::filesystem::create_directories(path, ec);
            activeTagFilter_.clear();
            activeQuickFilter_ = QuickFilter::Audio;
            activeNavAction_ = BrowserNavAction::Drums;
            navigateTo(path.string());
            applyFilter();
            break;
        }
        case BrowserNavAction::Instruments: {
            auto path = std::filesystem::path(rootPath_) / "User Library" / "Instruments";
            std::error_code ec;
            std::filesystem::create_directories(path, ec);
            activeTagFilter_.clear();
            activeQuickFilter_ = QuickFilter::All;
            activeNavAction_ = BrowserNavAction::Instruments;
            navigateTo(path.string());
            applyFilter();
            break;
        }
        case BrowserNavAction::AudioEffects: {
            auto path = std::filesystem::path(rootPath_) / "User Library" / "Effects";
            std::error_code ec;
            std::filesystem::create_directories(path, ec);
            activeTagFilter_.clear();
            activeQuickFilter_ = QuickFilter::All;
            activeNavAction_ = BrowserNavAction::AudioEffects;
            navigateTo(path.string());
            applyFilter();
            break;
        }
        case BrowserNavAction::Patterns:
            activeTagFilter_.clear();
            setFilter(QuickFilter::All);
            break;
        case BrowserNavAction::Clips: {
            auto path = std::filesystem::path(rootPath_) / "User Library" / "Clips";
            std::error_code ec;
            std::filesystem::create_directories(path, ec);
            activeTagFilter_.clear();
            activeQuickFilter_ = QuickFilter::All;
            activeNavAction_ = BrowserNavAction::Clips;
            navigateTo(path.string());
            applyFilter();
            break;
        }
        case BrowserNavAction::CurrentProject:
            // The audio the open project uses — it used to open the library root.
            activeQuickFilter_ = QuickFilter::All;
            showCurrentProject();
            break;
        case BrowserNavAction::UserLibrary:
        case BrowserNavAction::Packs: {
            std::filesystem::path base = rootPath_.empty() ? std::filesystem::path(currentPath_) : std::filesystem::path(rootPath_);
            std::filesystem::path target = base / (action == BrowserNavAction::Packs ? "Packs" : "User Library");
            std::error_code ec;
            std::filesystem::create_directories(target, ec);
            navigateTo(target.string());
            activeTagFilter_.clear();
            setFilter(QuickFilter::All);
            break;
        }
        case BrowserNavAction::CustomPlace:
        case BrowserNavAction::SystemPlace:
            if (!navHits_[newHovered].path.empty()) {
                navigateTo(navHits_[newHovered].path);
            }
            activeTagFilter_.clear();
            setFilter(QuickFilter::All);
            break;
        case BrowserNavAction::AddFolder:
            showAddFolderMenu();
            break;
        default:
            activeTagFilter_.clear();
            setFilter(QuickFilter::All);
            break;
    }

    // Returning from an embedded view (Plugins/Patterns) to a file-backed view:
    // while embedded, search-text changes route to the embedded view rather than
    // applyFilter(), so the file list still reflects the pre-embedded query. The
    // per-case setFilter() above only re-filters when the quick filter changed, so
    // reapply here to honor the current search text on the way back to files.
    const bool leavingEmbeddedView =
        (previousAction == BrowserNavAction::Plugins || previousAction == BrowserNavAction::Patterns);
    const bool enteringFileView =
        (action != BrowserNavAction::Plugins && action != BrowserNavAction::Patterns);
    if (leavingEmbeddedView && enteringFileView) {
        applyFilter();
    }

    if (onNavActionSelected_) {
        onNavActionSelected_(action);
    }

    invalidateCache();
    return true;
}

} // namespace AestraUI
