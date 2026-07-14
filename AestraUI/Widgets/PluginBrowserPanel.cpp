// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PluginBrowserPanel.h"
#include "NUIRenderer.h"
#include "NUIDragDrop.h"
#include "NUIContextMenu.h"
#include "NUIThemeSystem.h"
#include "../../AestraCore/include/AestraJSON.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <cstdio>
#include <filesystem>
#include "../../AestraCore/include/AestraLog.h"

namespace AestraUI {

// Theme colors (inline)
// ============================================================================

namespace Colors {
    static const NUIColor panelBackground = NUIThemeManager::getInstance().getColor("backgroundPrimary");
    static const NUIColor panelTop = NUIThemeManager::getInstance().getColor("backgroundSecondary");
    static const NUIColor panelBorder = NUIThemeManager::getInstance().getColor("border").withAlpha(0.40f);
    static const NUIColor textPrimary = NUIThemeManager::getInstance().getColor("textPrimary");
    static const NUIColor textSecondary = NUIThemeManager::getInstance().getColor("textSecondary");
    static const NUIColor textDisabled = NUIThemeManager::getInstance().getColor("textDisabled");
    static const NUIColor accentPrimary = NUIThemeManager::getInstance().getColor("accentPrimary");
    static const NUIColor accentSecondary = NUIThemeManager::getInstance().getColor("accentSecondary");
    static const NUIColor accentWarning = NUIThemeManager::getInstance().getColor("warning");
    static const NUIColor buttonBackground = NUIThemeManager::getInstance().getColor("buttonBgDefault");
    static const NUIColor buttonBackgroundHover = NUIThemeManager::getInstance().getColor("buttonBgHover");
    static const NUIColor inputBackground = NUIThemeManager::getInstance().getColor("inputBgDefault");
    static const NUIColor rowBackground = NUIThemeManager::getInstance().getColor("backgroundSecondary").withAlpha(0.72f);
    static const NUIColor listHover = NUIColor::white().withAlpha(0.045f);
    static const NUIColor listSelected = NUIThemeManager::getInstance().getColor("accentPrimary").withAlpha(0.16f);
    static const NUIColor divider = NUIThemeManager::getInstance().getColor("border").withAlpha(0.48f);

    // Type dot colors
    static const NUIColor typeEffect    = NUIColor(0.376f, 0.647f, 0.980f, 1.0f);  // #60a5fa
    static const NUIColor typeInstrument = NUIColor(0.204f, 0.835f, 0.600f, 1.0f); // #34d399
    static const NUIColor typeAnalyzer  = NUIColor(0.961f, 0.620f, 0.043f, 1.0f);  // #f59e0b
    static const NUIColor typeMidi      = NUIColor(0.957f, 0.447f, 0.714f, 1.0f);  // #f472b6

    // Format badge colors
    static const NUIColor badgeVst3Bg   = NUIColor(0.655f, 0.545f, 0.980f, 0.20f);
    static const NUIColor badgeVst3Text = NUIColor(0.655f, 0.545f, 0.980f, 1.0f);
    static const NUIColor badgeClapBg   = NUIColor(0.204f, 0.835f, 0.600f, 0.20f);
    static const NUIColor badgeClapText = NUIColor(0.204f, 0.835f, 0.600f, 1.0f);
    static const NUIColor badgeIntBg    = NUIColor(1.0f, 1.0f, 1.0f, 0.08f);
    static const NUIColor badgeIntText  = NUIColor(1.0f, 1.0f, 1.0f, 0.40f);

    // Active pill
    static const NUIColor pillActiveBg  = NUIColor(0.486f, 0.227f, 0.929f, 1.0f);  // #7c3aed filled
    static const NUIColor pillActiveText = NUIColor(1.0f, 1.0f, 1.0f, 1.0f);
    static const NUIColor pillInactiveBg = NUIColor(1.0f, 1.0f, 1.0f, 0.055f);
    static const NUIColor pillInactiveBorder = NUIColor(1.0f, 1.0f, 1.0f, 0.15f);
    static const NUIColor pillInactiveText = NUIColor(1.0f, 1.0f, 1.0f, 0.62f);

    // Favorite star
    static const NUIColor starActive    = NUIColor(0.655f, 0.545f, 0.980f, 1.0f);  // #a78bfa
    static const NUIColor starGhost     = NUIColor(0.655f, 0.545f, 0.980f, 0.35f);
}

namespace {
std::string fitText(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth) {
    if (text.empty() || renderer.measureText(text, fontSize).width <= maxWidth) {
        return text;
    }

    constexpr const char* ellipsis = "...";
    const float ellipsisW = renderer.measureText(ellipsis, fontSize).width;
    if (ellipsisW >= maxWidth) {
        return ellipsis;
    }

    std::string out = text;
    while (!out.empty() && renderer.measureText(out, fontSize).width + ellipsisW > maxWidth) {
        out.pop_back();
    }
    return out + ellipsis;
}

static std::filesystem::path getFavoritesPath() {
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";
    auto dir = std::filesystem::path(home) / ".config" / "aestra";
    std::filesystem::create_directories(dir);
    return dir / "favorites.json";
}
}

// ============================================================================
// PluginBrowserPanel Implementation
// ============================================================================

PluginBrowserPanel::PluginBrowserPanel() {
    setSize(300, 500);
    loadFavorites();
}

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

void PluginBrowserPanel::saveFavorites() {
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

void PluginBrowserPanel::onRender(NUIRenderer& renderer) {
    std::lock_guard<std::recursive_mutex> lock(m_uiMutex);
    auto bounds = getBounds();

    // Clip everything to the panel. The header/filter bars draw unclipped, so
    // overflowing filter pills used to bleed to the right into the track manager.
    renderer.setClipRect(bounds);

    renderer.fillRect(bounds, Colors::panelBackground);

    renderHeaderBar(renderer);
    renderFilterBar(renderer);
    renderPluginList(renderer);

    if (m_scanning) {
        renderScanProgress(renderer);
    }

    renderer.clearClipRect();
}

void PluginBrowserPanel::renderHeaderBar(NUIRenderer& renderer) {
    auto bounds = getBounds();
    constexpr float headerH = HEADER_BAR_HEIGHT;

    const float headerY = bounds.y + CONTENT_TOP_PAD;
    renderer.fillRect({bounds.x, headerY, bounds.width, headerH}, Colors::panelTop);
    renderer.drawLine({bounds.x, headerY + headerH}, {bounds.right(), headerY + headerH}, 1.0f, Colors::divider);

    // Title, with a muted count once a scan has populated the list.
    renderer.drawText("Plugins", {bounds.x + 14.0f, headerY + 12.0f}, 12.0f, Colors::textPrimary.withAlpha(0.90f));
    if (!m_allPlugins.empty()) {
        float titleW = renderer.measureText("Plugins", 12.0f).width;
        const bool filtered = m_typeFilter != PluginTypeFilter::All || m_formatFilter != PluginFormatFilter::All ||
                              m_showFavoritesOnly || !m_searchQuery.empty();
        std::string count = std::to_string(m_filteredPlugins.size());
        if (filtered) {
            count += " / " + std::to_string(m_allPlugins.size());
        }
        renderer.drawText(count, {bounds.x + 14.0f + titleW + 7.0f, headerY + 13.0f}, 10.0f,
                          Colors::textSecondary.withAlpha(0.45f));
    }

    // Scan control — accent pill, consistent with the filter pills.
    NUIRect scanBtn = getScanButtonRect();
    const float scanRadius = scanBtn.height * 0.5f;
    if (m_scanning) {
        renderer.fillRoundedRect(scanBtn, scanRadius, Colors::accentPrimary.withAlpha(0.10f));
        auto dots = renderer.measureText("\xe2\x80\xa6", 11.0f); // ellipsis glyph
        renderer.drawText("\xe2\x80\xa6", {scanBtn.x + (scanBtn.width - dots.width) * 0.5f, scanBtn.y + 3.0f}, 11.0f,
                          Colors::accentPrimary.withAlpha(0.75f));
    } else {
        renderer.fillRoundedRect(scanBtn, scanRadius, Colors::accentPrimary.withAlpha(0.16f));
        auto label = renderer.measureText("Scan", 10.0f);
        renderer.drawText("Scan", {scanBtn.x + (scanBtn.width - label.width) * 0.5f, scanBtn.y + 4.5f}, 10.0f,
                          Colors::accentPrimary);
    }
}

void PluginBrowserPanel::renderFilterBar(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float barY = bounds.y + CONTENT_TOP_PAD + HEADER_BAR_HEIGHT;
    m_filterPillHits.clear();

    renderer.fillRect({bounds.x, barY, bounds.width, FILTER_BAR_HEIGHT}, Colors::panelTop);
    renderer.drawLine({bounds.x, barY + FILTER_BAR_HEIGHT}, {bounds.right(), barY + FILTER_BAR_HEIGHT}, 1.0f, Colors::divider);

    // Equal-width pills that span the full width in two rows of three. This fills
    // the bar edge-to-edge (no left-clustered dead space) and adapts as the panel
    // is widened — the pills grow with it instead of leaving a gap.
    const float margin = 12.0f;
    const float gap = 6.0f;
    const float pillH = 19.0f;
    const float availW = std::max(0.0f, bounds.width - 2.0f * margin);
    const float pillW = std::max(24.0f, (availW - 2.0f * gap) / 3.0f);
    const float startX = bounds.x + margin;
    const float row1Y = barY + 5.0f;
    const float row2Y = barY + 28.0f;

    auto drawPill = [&](const std::string& label, bool active, FilterPillHit::Type type, int col, float y) {
        NUIRect rect = {startX + col * (pillW + gap), y, pillW, pillH};
        renderer.fillRoundedRect(rect, pillH * 0.5f, active ? Colors::pillActiveBg : Colors::pillInactiveBg);

        NUIColor textColor = active ? Colors::pillActiveText : Colors::pillInactiveText;
        auto measured = renderer.measureText(label, 10.0f);
        renderer.drawText(label, {rect.x + (rect.width - measured.width) * 0.5f, rect.y + 4.0f}, 10.0f, textColor);

        m_filterPillHits.push_back({type, rect});
    };

    // Row 1 — plugin type (primary axis)
    bool allActive = (m_typeFilter == PluginTypeFilter::All && m_formatFilter == PluginFormatFilter::All && !m_showFavoritesOnly);
    drawPill("All", allActive, FilterPillHit::TypeAll, 0, row1Y);
    drawPill("FX", m_typeFilter == PluginTypeFilter::Effects, FilterPillHit::TypeFX, 1, row1Y);
    drawPill("Inst", m_typeFilter == PluginTypeFilter::Instruments, FilterPillHit::TypeInst, 2, row1Y);

    // Row 2 — format + favorites (secondary axis)
    drawPill("VST3", m_formatFilter == PluginFormatFilter::VST3, FilterPillHit::FormatVST3, 0, row2Y);
    drawPill("CLAP", m_formatFilter == PluginFormatFilter::CLAP, FilterPillHit::FormatCLAP, 1, row2Y);
    drawPill("Faves", m_showFavoritesOnly, FilterPillHit::Fav, 2, row2Y);
}

void PluginBrowserPanel::renderPluginList(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float listTop = bounds.y + CONTENT_TOP_PAD + HEADER_BAR_HEIGHT + FILTER_BAR_HEIGHT + 4.0f;
    float listHeight = bounds.height - CONTENT_TOP_PAD - HEADER_BAR_HEIGHT - FILTER_BAR_HEIGHT - 4.0f;

    renderer.setClipRect({bounds.x + 8.0f, listTop, bounds.width - 12.0f, listHeight});

    // Rebuild star rects each render
    m_starRects.assign(m_filteredPlugins.size(), NUIRect{});

    float yOffset = listTop - m_scrollOffset;

    for (size_t i = 0; i < m_filteredPlugins.size(); ++i) {
        if (yOffset + ROW_HEIGHT > listTop && yOffset < listTop + listHeight) {
            renderPluginRow(renderer, m_filteredPlugins[i], static_cast<int>(i), yOffset);
        }
        yOffset += ROW_HEIGHT;
    }

    if (m_filteredPlugins.empty() && !m_scanning) {
        float centerY = listTop + listHeight * 0.5f;
        const bool hasCatalog = !m_allPlugins.empty();
        const std::string title = hasCatalog ? "No matches" : "No plugins found";
        const std::string hint = hasCatalog ? "Adjust filters or search" : "Use Scan above to discover plugins";
        renderer.drawTextCentered(title, {bounds.x + 12.0f, centerY - 16.0f, bounds.width - 24.0f, 18.0f},
                                  13.0f, Colors::textPrimary.withAlpha(0.85f));
        renderer.drawTextCentered(hint, {bounds.x + 12.0f, centerY + 4.0f, bounds.width - 24.0f, 16.0f},
                                  10.0f, Colors::textSecondary.withAlpha(0.52f));
    }

    const float contentHeight = static_cast<float>(m_filteredPlugins.size()) * ROW_HEIGHT;
    const float maxScroll = std::max(0.0f, contentHeight - listHeight);
    if (maxScroll > 0.0f) {
        const float trackY = listTop + 4.0f;
        const float trackH = std::max(0.0f, listHeight - 8.0f);
        const float thumbH = std::max(24.0f, trackH * (listHeight / contentHeight));
        const float travel = std::max(0.0f, trackH - thumbH);
        const float thumbY = trackY + travel * std::clamp(m_scrollOffset / maxScroll, 0.0f, 1.0f);
        const float scrollbarX = bounds.right() - 7.0f;
        renderer.fillRoundedRect({scrollbarX, trackY, 2.0f, trackH}, 1.0f, Colors::pillInactiveBg);
        renderer.fillRoundedRect({scrollbarX, thumbY, 2.0f, thumbH}, 1.0f,
                                 Colors::textSecondary.withAlpha(0.42f));
    }

    renderer.clearClipRect();
}

void PluginBrowserPanel::renderPluginRow(NUIRenderer& renderer,
                                            const PluginListItem& plugin,
                                            int index, float yOffset) {
    auto bounds = getBounds();
    NUIRect rowRect = {bounds.x + 10, yOffset + 2.0f, bounds.width - 20, ROW_HEIGHT - 4.0f};

    // Row background
    if (index == m_selectedIndex) {
        renderer.fillRoundedRect(rowRect, 6.0f, Colors::listSelected);
        renderer.fillRoundedRect({rowRect.x, rowRect.y + 4.0f, 3.0f, rowRect.height - 8.0f},
                                 1.5f,
                                 Colors::accentPrimary.withAlpha(0.85f));
    } else if (index == m_hoveredIndex) {
        renderer.fillRoundedRect(rowRect, 6.0f, Colors::listHover);
    }

    // Star rect (for hit-testing)
    float starX = rowRect.x + 4;
    float starY = rowRect.y + (rowRect.height - 14) * 0.5f;
    float starSize = 14.0f;
    m_starRects[index] = {starX, starY, starSize, starSize};

    // Render star
    if (plugin.isFavorite) {
        renderer.drawText("\xe2\x98\x85", {starX + 1, starY + 1}, 12.0f, Colors::starActive);
    } else if (index == m_hoveredRow) {
        renderer.drawText("\xe2\x98\x85", {starX + 1, starY + 1}, 12.0f, Colors::starGhost);
    }

    // Type dot — with a soft outer halo on hover/selection so it reads as a status pip.
    float dotX = rowRect.x + 22;
    float dotCX = dotX + 4.0f;
    float dotCY = rowRect.y + rowRect.height * 0.5f;
    NUIColor dotColor = Colors::textDisabled.withAlpha(0.3f);
    if (plugin.typeName == "Effect") dotColor = Colors::typeEffect;
    else if (plugin.typeName == "Instrument") dotColor = Colors::typeInstrument;
    else if (plugin.typeName == "Analyzer") dotColor = Colors::typeAnalyzer;
    else if (plugin.typeName == "MIDI") dotColor = Colors::typeMidi;
    if (index == m_selectedIndex || index == m_hoveredIndex) {
        renderer.fillCircle({dotCX, dotCY}, 6.5f, dotColor.withAlpha(0.20f));
    }
    renderer.fillCircle({dotCX, dotCY}, 4.0f, dotColor);

    // Format badge (right-aligned) — only for third-party formats. Internal
    // plugins are the default, so tagging every one of them just adds noise;
    // skipping "Int" declutters the list and gives the name more room.
    const bool external = (plugin.formatStr != "Int" && !plugin.formatStr.empty());
    float contentRightEdge = rowRect.right() - 8.0f;
    if (external) {
        std::string badgeLabel = "VST3";
        NUIColor badgeBg = Colors::badgeVst3Bg;
        NUIColor badgeText = Colors::badgeVst3Text;
        if (plugin.formatStr.find("CLAP") != std::string::npos) {
            badgeLabel = "CLAP";
            badgeBg = Colors::badgeClapBg;
            badgeText = Colors::badgeClapText;
        }
        float badgeLabelW = renderer.measureText(badgeLabel, 9.0f).width;
        NUIRect badgeRect = {rowRect.right() - (badgeLabelW + 12.0f) - 6.0f,
                             rowRect.y + (rowRect.height - 16.0f) * 0.5f,
                             badgeLabelW + 12.0f, 16.0f};
        renderer.fillRoundedRect(badgeRect, 4.0f, badgeBg);
        renderer.drawText(badgeLabel, {badgeRect.x + (badgeRect.width - badgeLabelW) * 0.5f, badgeRect.y + 2.5f}, 9.0f, badgeText);
        contentRightEdge = badgeRect.x - 8.0f;
    }

    // Name
    const bool activeRow = (index == m_selectedIndex);
    float nameX = dotX + 14;
    const float nameMaxW = contentRightEdge - nameX;
    std::string name = fitText(renderer, plugin.name, 13.0f, nameMaxW);
    renderer.drawText(name, {nameX, rowRect.y + 5.0f}, 13.0f,
                      activeRow ? Colors::textPrimary : Colors::textPrimary.withAlpha(0.90f));

    // Vendor · type (muted, second line)
    float vendorMaxW = contentRightEdge - nameX;
    std::string vendorMeta = plugin.vendor;
    if (!plugin.typeName.empty()) vendorMeta += " · " + plugin.typeName;
    vendorMeta = fitText(renderer, vendorMeta, 10.0f, vendorMaxW);
    renderer.drawText(vendorMeta, {nameX, rowRect.y + 20.0f}, 10.0f, Colors::textSecondary.withAlpha(0.58f));
}

void PluginBrowserPanel::renderScanProgress(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float listTop = bounds.y + CONTENT_TOP_PAD + HEADER_BAR_HEIGHT + FILTER_BAR_HEIGHT + 4.0f;

    renderer.fillRect({bounds.x, listTop, bounds.width,
                       bounds.height - CONTENT_TOP_PAD - HEADER_BAR_HEIGHT - FILTER_BAR_HEIGHT - 4.0f},
                      Colors::panelBackground.withAlpha(0.82f));

    float barWidth = bounds.width - 40;
    float barX = bounds.x + 20;
    float barY = listTop + 60;

    renderer.fillRoundedRect({barX, barY, barWidth, 6}, 3.0f, Colors::pillInactiveBg);
    renderer.fillRoundedRect({barX, barY, std::max(6.0f, barWidth * m_scanProgress), 6}, 3.0f, Colors::accentPrimary);

    std::string status = m_scanStatus.empty() ? "Scanning plugins\xe2\x80\xa6" : m_scanStatus;
    status = fitText(renderer, status, 12.0f, barWidth);
    renderer.drawText(status, {barX, barY - 22}, 12.0f, Colors::textPrimary.withAlpha(0.9f));
}

bool PluginBrowserPanel::onMouseEvent(const NUIMouseEvent& event) {
    // Early exit if not visible - don't lock mutex or consume events
    if (!isVisible()) return false;

    std::lock_guard<std::recursive_mutex> lock(m_uiMutex);
    auto bounds = getBounds();
    float mx = event.position.x;
    float my = event.position.y;
    const bool insideBounds = bounds.contains(event.position);

    // Click handling
    if (event.pressed && event.button == NUIMouseButton::Left) {
        // Filter pill clicks
        for (size_t i = 0; i < m_filterPillHits.size(); ++i) {
            if (m_filterPillHits[i].bounds.contains(event.position)) {
                m_searchFocused = false;
                switch (m_filterPillHits[i].type) {
                    case FilterPillHit::TypeAll:
                        m_typeFilter = PluginTypeFilter::All;
                        m_formatFilter = PluginFormatFilter::All;
                        m_showFavoritesOnly = false;
                        break;
                    case FilterPillHit::TypeFX:
                        m_typeFilter = (m_typeFilter == PluginTypeFilter::Effects) ? PluginTypeFilter::All : PluginTypeFilter::Effects;
                        break;
                    case FilterPillHit::TypeInst:
                        m_typeFilter = (m_typeFilter == PluginTypeFilter::Instruments) ? PluginTypeFilter::All : PluginTypeFilter::Instruments;
                        break;
                    case FilterPillHit::FormatVST3:
                        m_formatFilter = (m_formatFilter == PluginFormatFilter::VST3) ? PluginFormatFilter::All : PluginFormatFilter::VST3;
                        break;
                    case FilterPillHit::FormatCLAP:
                        m_formatFilter = (m_formatFilter == PluginFormatFilter::CLAP) ? PluginFormatFilter::All : PluginFormatFilter::CLAP;
                        break;
                    case FilterPillHit::Fav:
                        m_showFavoritesOnly = !m_showFavoritesOnly;
                        break;
                    default:
                        break;
                }
                applyFilters();
                repaint();
                return true;
            }
        }

        // Plugin list clicks (Press down)
        float listTop = bounds.y + CONTENT_TOP_PAD + HEADER_BAR_HEIGHT + FILTER_BAR_HEIGHT + 4.0f;
        if (insideBounds && my >= listTop) {
            int rowIndex = hitTestRow(static_cast<int>(my));
            if (rowIndex >= 0 && rowIndex < static_cast<int>(m_filteredPlugins.size())) {
                // Check star hit first
                if (rowIndex < static_cast<int>(m_starRects.size()) && m_starRects[rowIndex].contains(mx, my)) {
                    toggleFavorite(m_filteredPlugins[rowIndex].id);
                    repaint();
                    return true;
                }
                m_searchFocused = false;
                repaint();
                // Initiate potential drag or click
                m_isPressed = true;
                m_pressedIndex = rowIndex;
                m_dragStartPos = event.position;
                return true;
            }
        }

        // Scan button (ignore if already scanning)
        NUIRect scanBtn = getScanButtonRect();
        if (insideBounds &&
            mx >= scanBtn.x && mx < scanBtn.x + scanBtn.width &&
            my >= scanBtn.y && my < scanBtn.y + scanBtn.height) {
            if (!m_scanning && m_onScanRequested) {
                m_onScanRequested();
            }
            return true;
        }
        if (insideBounds) {
            m_searchFocused = false;
            repaint();
        }
    }
    // Handle Release (Click)
    else if (event.released && event.button == NUIMouseButton::Left) {
        if (m_isPressed) {
            // Was a click
            if (m_pressedIndex >= 0 && m_pressedIndex < static_cast<int>(m_filteredPlugins.size())) {
                // Check for double-click (same index within 300ms)
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now.time_since_epoch()).count() - m_lastClickTime;

                if (m_pressedIndex == m_lastClickIndex && elapsed < 0.3) {
                    // Double-click detected - trigger load
                    if (m_onPluginLoadRequested) {
                        m_onPluginLoadRequested(m_filteredPlugins[m_pressedIndex]);
                    }
                    m_lastClickIndex = -1; // Reset to prevent triple-click triggering
                    m_lastClickTime = 0.0;
                } else {
                    // Single click - select and record for double-click detection
                    m_selectedIndex = m_pressedIndex;
                    if (m_onPluginSelected) {
                        m_onPluginSelected(m_filteredPlugins[m_pressedIndex]);
                    }
                    m_lastClickIndex = m_pressedIndex;
                    m_lastClickTime = std::chrono::duration<double>(now.time_since_epoch()).count();
                }
            }
            m_isPressed = false;
            m_pressedIndex = -1;
            return true;
        }
    }
    // Handle Drag
    else if (!event.pressed && !event.released) { // Mouse Move
        if (m_isPressed) {
            float dx = event.position.x - m_dragStartPos.x;
            float dy = event.position.y - m_dragStartPos.y;
            float dist = std::sqrt(dx*dx + dy*dy);

            if (dist > 5.0f) {
                // Start Drag
                if (m_pressedIndex >= 0 && m_pressedIndex < static_cast<int>(m_filteredPlugins.size())) {
                    const auto& plugin = m_filteredPlugins[m_pressedIndex];

                    AestraUI::DragData data;
                    data.type = AestraUI::DragDataType::Plugin;
                    data.displayName = plugin.name;
                    data.sourceClipIdString = plugin.id;
                    data.customData = plugin;

                    AestraUI::NUIDragDropManager::getInstance().beginDrag(data, m_dragStartPos, this);

                    // Consume interaction
                    m_isPressed = false;
                    m_pressedIndex = -1;
                    return true;
                }
            }
        }
    }

    // Hover tracking
    float listTop = bounds.y + CONTENT_TOP_PAD + HEADER_BAR_HEIGHT + FILTER_BAR_HEIGHT + 4.0f;
    if (insideBounds && my >= listTop) {
        m_hoveredIndex = hitTestRow(static_cast<int>(my));
        m_hoveredRow = m_hoveredIndex;
    } else {
        m_hoveredIndex = -1;
        m_hoveredRow = -1;
    }

    // Scroll handling
    if (insideBounds && event.wheelDelta != 0.0f) {
        float listHeight = bounds.height - CONTENT_TOP_PAD - HEADER_BAR_HEIGHT - FILTER_BAR_HEIGHT - 4.0f;
        float contentHeight = m_filteredPlugins.size() * ROW_HEIGHT;
        float maxScroll = std::max(0.0f, contentHeight - listHeight);

        m_targetScrollOffset -= event.wheelDelta * 40.0f;
        if (m_targetScrollOffset < 0.0f) m_targetScrollOffset = 0.0f;
        if (m_targetScrollOffset > maxScroll) m_targetScrollOffset = maxScroll;
        return true;
    }

    // Consume event if mouse is within our bounds to prevent click-through
    return insideBounds;
}

bool PluginBrowserPanel::onKeyEvent(const NUIKeyEvent& event) {
    if (!isVisible() || !m_searchFocused || !event.pressed) return false;

    std::lock_guard<std::recursive_mutex> lock(m_uiMutex);

    if (event.keyCode == NUIKeyCode::Escape) {
        m_searchFocused = false;
        setFocused(false);
        repaint();
        return true;
    }
    if (event.keyCode == NUIKeyCode::Backspace || event.keyCode == NUIKeyCode::Delete) {
        if (!m_searchQuery.empty()) {
            m_searchQuery.pop_back();
            applyFilters();
            repaint();
        }
        return true;
    }
    if (event.keyCode == NUIKeyCode::Enter) {
        m_searchFocused = false;
        setFocused(false);
        repaint();
        return true;
    }

    if (event.character >= 32 && event.character <= 126) {
        m_searchQuery.push_back(event.character);
        applyFilters();
        repaint();
        return true;
    }
    return false;
}

void PluginBrowserPanel::onFocusLost() {
    NUIComponent::onFocusLost();
    m_searchFocused = false;
    repaint();
}

void PluginBrowserPanel::onUpdate(double deltaTime) {
    float diff = m_targetScrollOffset - m_scrollOffset;
    if (std::abs(diff) > 0.5f) {
        m_scrollOffset += diff * std::min(1.0f, static_cast<float>(deltaTime * 15.0));
    } else {
        m_scrollOffset = m_targetScrollOffset;
    }
}

void PluginBrowserPanel::setPluginList(const std::vector<PluginListItem>& plugins) {
    std::lock_guard<std::recursive_mutex> lock(m_uiMutex);
    m_allPlugins = plugins;
    for (auto& p : m_allPlugins) {
        p.isFavorite = m_favoritesSet.count(p.id) > 0;
    }
    applyFilters();
}

void PluginBrowserPanel::setTypeFilter(PluginTypeFilter filter) {
    m_typeFilter = filter;
    applyFilters();
}

void PluginBrowserPanel::setFormatFilter(PluginFormatFilter filter) {
    m_formatFilter = filter;
    applyFilters();
}

void PluginBrowserPanel::setShowFavoritesOnly(bool show) {
    m_showFavoritesOnly = show;
    applyFilters();
}

void PluginBrowserPanel::setSearchQuery(const std::string& query) {
    m_searchQuery = query;
    applyFilters();
}

void PluginBrowserPanel::applyFilters() {
    std::string selectedId;
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_filteredPlugins.size())) {
        selectedId = m_filteredPlugins[m_selectedIndex].id;
    }

    m_filteredPlugins.clear();

    for (const auto& p : m_allPlugins) {
        // Type filter
        bool passType = true;
        if (m_typeFilter == PluginTypeFilter::Effects) {
            passType = (p.typeName == "Effect");
        } else if (m_typeFilter == PluginTypeFilter::Instruments) {
            passType = (p.typeName == "Instrument");
        }
        if (!passType) continue;

        // Format filter
        bool passFormat = true;
        if (m_formatFilter == PluginFormatFilter::VST3) {
            passFormat = (p.formatStr == "VST3");
        } else if (m_formatFilter == PluginFormatFilter::CLAP) {
            passFormat = (p.formatStr.find("CLAP") != std::string::npos);
        }
        if (!passFormat) continue;

        // Favorites filter
        if (m_showFavoritesOnly && !p.isFavorite) continue;

        // Search filter
        if (!m_searchQuery.empty()) {
            std::string lowerQuery = m_searchQuery;
            std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

            std::string lowerName = p.name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

            std::string lowerVendor = p.vendor;
            std::transform(lowerVendor.begin(), lowerVendor.end(), lowerVendor.begin(), ::tolower);

            if (lowerName.find(lowerQuery) == std::string::npos &&
                lowerVendor.find(lowerQuery) == std::string::npos) {
                continue;
            }
        }

        m_filteredPlugins.push_back(p);
    }

    m_selectedIndex = -1;
    if (!selectedId.empty()) {
        for (size_t i = 0; i < m_filteredPlugins.size(); ++i) {
            if (m_filteredPlugins[i].id == selectedId) {
                m_selectedIndex = static_cast<int>(i);
                break;
            }
        }
    }

    // Indices refer to the previous filtered view. Do not let a quick filter
    // change turn the first click on a different row into a false double-click.
    m_lastClickIndex = -1;
    m_lastClickTime = 0.0;
    m_hoveredIndex = -1;
    m_hoveredRow = -1;
    m_isPressed = false;
    m_pressedIndex = -1;
    m_scrollOffset = 0.0f;
    m_targetScrollOffset = 0.0f;
}

const PluginListItem* PluginBrowserPanel::getSelectedPlugin() const {
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_filteredPlugins.size())) {
        return &m_filteredPlugins[m_selectedIndex];
    }
    return nullptr;
}

void PluginBrowserPanel::selectPlugin(const std::string& id) {
    for (size_t i = 0; i < m_filteredPlugins.size(); ++i) {
        if (m_filteredPlugins[i].id == id) {
            m_selectedIndex = static_cast<int>(i);
            return;
        }
    }
}

void PluginBrowserPanel::clearSelection() {
    m_selectedIndex = -1;
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

void PluginBrowserPanel::setOnPluginSelected(std::function<void(const PluginListItem&)> callback) {
    m_onPluginSelected = std::move(callback);
}

void PluginBrowserPanel::setOnPluginLoadRequested(std::function<void(const PluginListItem&)> callback) {
    m_onPluginLoadRequested = std::move(callback);
}

void PluginBrowserPanel::setOnScanRequested(std::function<void()> callback) {
    m_onScanRequested = std::move(callback);
}

void PluginBrowserPanel::setScanning(bool scanning, float progress) {
    std::lock_guard<std::recursive_mutex> lock(m_uiMutex);
    m_scanning = scanning;
    m_scanProgress = progress;
}

void PluginBrowserPanel::setScanStatus(const std::string& status) {
    std::lock_guard<std::recursive_mutex> lock(m_uiMutex);
    m_scanStatus = status;
}

NUIRect PluginBrowserPanel::getScanButtonRect() const {
    auto bounds = getBounds();
    return {bounds.right() - 58.0f, bounds.y + CONTENT_TOP_PAD + 12.0f, 46.0f, 20.0f};
}

int PluginBrowserPanel::hitTestRow(int y) const {
    auto bounds = getBounds();
    float listTop = bounds.y + CONTENT_TOP_PAD + HEADER_BAR_HEIGHT + FILTER_BAR_HEIGHT + 4.0f;

    if (y < listTop) return -1;

    int row = static_cast<int>((y - listTop + m_scrollOffset) / ROW_HEIGHT);
    if (row < 0 || row >= static_cast<int>(m_filteredPlugins.size())) {
        return -1;
    }
    return row;
}

// ============================================================================
// EffectChainRack Implementation
// ============================================================================

EffectChainRack::EffectChainRack() {
    setId("EffectChainRack");

    for (auto& slot : m_slots) {
        slot.name = "Empty";
        slot.isEmpty = true;
        slot.bypassed = false;
    }
    m_bypassOverride.fill(-1);
}

void EffectChainRack::onRender(NUIRenderer& renderer) {
    auto bounds = getBounds();

    renderer.fillRoundedRect(bounds, 10.0f, Colors::panelBackground.withAlpha(0.94f));
    renderer.fillRoundedRect({bounds.x, bounds.y, bounds.width, 28.0f}, 10.0f, Colors::panelTop.withAlpha(0.62f));
    renderer.strokeRoundedRect(bounds, 10.0f, 1.0f, Colors::panelBorder.withAlpha(0.84f));

    // Enable clipping
    renderer.setClipRect(bounds);

    for (int i = 0; i < MAX_SLOTS; ++i) {
        renderSlot(renderer, i, bounds.y + 8 + i * SLOT_HEIGHT - m_scrollOffset);
    }

    renderer.clearClipRect();

    // Render Drag Ghost
    if (m_isDraggingReorder && m_draggingSlotIndex >= 0) {
        float ghostY = m_currentMousePos.y - (SLOT_HEIGHT * 0.5f);
        renderSlot(renderer, m_draggingSlotIndex, ghostY);
    }

}

void EffectChainRack::renderSlot(NUIRenderer& renderer, int index, float yOffset) {
    NUIRect slotRect = slotRectForTop(yOffset);

    const auto& slot = m_slots[index];
    const bool isHovered = (index == m_hoveredSlot);

    // Premium Glass Styling
    NUIColor bgColor;
    NUIColor borderColor;
    // Drag Reorder: If this is the source slot, render faintly
    bool isBeingDragged = (m_isDraggingReorder && index == m_draggingSlotIndex);

    if (slot.isEmpty && !isBeingDragged) {
        // Empty Slot: Subtle transparency or very faint glass
        // Using Aestra "Deep Glass" tokens if available, otherwise manual
        bgColor = isHovered ? Colors::buttonBackgroundHover.withAlpha(0.64f) : Colors::buttonBackground.withAlpha(0.48f);
        borderColor = isHovered ? Colors::accentPrimary.withAlpha(0.30f) : Colors::panelBorder.withAlpha(0.30f);
    } else {
        // Populated: Solid dark glass
        // If bypassed, make it slightly dimmer/transparent
        if (isBeingDragged) {
            bgColor = isHovered ? Colors::buttonBackgroundHover.withAlpha(0.72f) : Colors::buttonBackground.withAlpha(0.62f);
            borderColor = Colors::accentPrimary.withAlpha(0.2f);
        } else if (slot.bypassed) {
             bgColor = Colors::buttonBackground.withAlpha(0.58f);
             borderColor = Colors::panelBorder.withAlpha(0.5f);
        } else {
             bgColor = isHovered ? Colors::buttonBackgroundHover.withAlpha(0.84f) : Colors::buttonBackground.withAlpha(0.72f);
             borderColor = isHovered ? Colors::accentPrimary.withAlpha(0.76f) : Colors::accentPrimary.withAlpha(0.20f);
        }
    }

    renderer.fillRoundedRect(slotRect, 8.0f, bgColor);

    // Dashed border for empty slots to signal droppability
    if (slot.isEmpty && !isBeingDragged) {
        renderer.strokeRoundedRect(slotRect, 8.0f, 1.0f, borderColor);
        renderer.fillRoundedRect({slotRect.x + 6.0f, slotRect.y + slotRect.height * 0.5f - 0.5f,
                                  slotRect.width - 12.0f, 1.0f},
                                 0.5f,
                                 isHovered ? Colors::accentPrimary.withAlpha(0.12f)
                                           : Colors::panelBorder.withAlpha(0.16f));
    } else {
        renderer.strokeRoundedRect(slotRect, 8.0f, 1.0f, borderColor);
    }

    // DEBUG: Visual indicator for pending removal
    if (slot.pendingRemoval) {
        renderer.strokeRoundedRect(slotRect, 4.0f, 2.0f, NUIColor(1.0f, 0.0f, 0.0f, 0.8f));
    }

    // Shared vertical midline for the slot row — center everything around it
    const float slotMid = slotRect.y + slotRect.height * 0.5f;

    // Slot Number (Left side, stylistic) — centered on slotMid.
    // Populated slots get a solid index chip (it shows chain order). Empty slots
    // stay quiet — a faint bare number revealed only on hover — so an empty rack
    // reads as a column of available slots rather than a numbered debug table.
    char numBuf[8];
    std::snprintf(numBuf, sizeof(numBuf), "%d", index + 1);
    const float chipH = 14.0f;
    const NUIRect indexChip{slotRect.x + 8.0f, slotMid - chipH * 0.5f, 18.0f, chipH};
    if (!slot.isEmpty) {
        renderer.fillRoundedRect(indexChip, 7.0f, Colors::buttonBackgroundHover.withAlpha(0.76f));
        renderer.strokeRoundedRect(indexChip, 7.0f, 1.0f, Colors::panelBorder.withAlpha(0.35f));
        renderer.drawTextCentered(numBuf, indexChip, 9.0f, Colors::textDisabled.withAlpha(0.68f));
    } else if (isHovered) {
        renderer.drawTextCentered(numBuf, indexChip, 9.0f, Colors::textDisabled.withAlpha(0.55f));
    }

    // Available text area to the right of the chip.
    // Use the same vertical extent as the chip so drawTextCentered aligns identically.
    const float textX = slotRect.x + 36.0f;
    const float textW = slot.isEmpty ? slotRect.width - 44.0f : slotRect.width - 76.0f;
    const float rowH = chipH; // 14 px — same height as the index chip

    if (slot.isEmpty) {
        // Empty rows surface the affordance only on hover; the recessed row and
        // subtle centre line already signal an available drop target, so the
        // redundant em-dash placeholder is gone.
        if (isHovered) {
            const NUIRect textRect{textX, slotMid - rowH * 0.5f, textW, rowH};
            renderer.drawTextCentered("+ Add Insert", textRect, 10.0f, Colors::textPrimary);
        }
    } else {
        // A user bypass simply greys the slot — no text label. The dimmed name
        // (plus the recessed row background) carries the state. Auto-quarantine
        // is a safety fault, not a user choice, so it still gets its warning.
        const bool autoFaulted = slot.bypassed && slot.nonFiniteOutputFault;
        NUIColor nameColor = slot.bypassed ? Colors::textDisabled.withAlpha(0.6f) : Colors::textPrimary;
        if (autoFaulted) {
            const NUIRect nameRect{textX, slotMid - rowH, textW, rowH};
            renderer.drawText(fitText(renderer, slot.name, 10.5f, nameRect.width),
                              {nameRect.x, nameRect.y + 2.0f},
                              10.5f,
                              nameColor);
            const NUIRect statusRect{textX, slotMid, textW, rowH};
            renderer.drawText("Output fault", {statusRect.x, statusRect.y + 2.0f}, 8.5f,
                              NUIThemeManager::getInstance().getColor("warning").withAlpha(0.92f));
        } else {
            renderer.drawTextCentered(fitText(renderer, slot.name, 10.5f, textW),
                                      {textX, slotRect.y, textW, slotRect.height},
                                      10.5f,
                                      nameColor);
        }

        // Active indicator / Bypass toggle
        float rightEdge = slotRect.x + slotRect.width;
        float knobSize = 18.0f;
        float knobX = rightEdge - knobSize - 4.0f;
        float knobY = yOffset + (SLOT_HEIGHT - 4 - knobSize) * 0.5f;

        // Dry/Wet Knob Rendering
        NUIRect knobRect = {knobX, knobY, knobSize, knobSize};

        // Helper to draw arc
        auto drawArcPoly = [&](float startAngle, float endAngle, float width, NUIColor col) {
            NUIPoint center = knobRect.center();
            float radius = knobSize * 0.5f - 2.0f;
            std::vector<NUIPoint> points;
            int segments = 16;
            for(int i=0; i<=segments; ++i) {
                float t = (float)i / segments;
                float ang = startAngle + (endAngle - startAngle) * t;
                points.push_back({
                    center.x + std::cos(ang) * radius,
                    center.y + std::sin(ang) * radius
                });
            }
            if (!points.empty())
                renderer.drawPolyline(points.data(), (int)points.size(), width, col);
        };

        // Background Arc
        drawArcPoly(0.75f * 3.14159f, 2.25f * 3.14159f, 2.0f, Colors::textDisabled.withAlpha(0.2f));

        // Value Arc (Dim if bypassed)
        float startAng = 0.75f * 3.14159f;
        float range = 1.5f * 3.14159f;
        float endAng = startAng + range * slot.dryWet;
        NUIColor arcColor = slot.bypassed ? Colors::textDisabled.withAlpha(0.3f) : Colors::accentPrimary;
        drawArcPoly(startAng, endAng, 2.0f, arcColor);

        // Bypass Indicator (Dot left of knob)
        // Center vertically better
        float dotSize = 6.0f;
        float dotY = slotRect.y + (slotRect.height - dotSize) * 0.5f;
        NUIRect statusDot = {knobX - 12, dotY, dotSize, dotSize};

        if (!slot.bypassed) {
            // Active: LED On
        renderer.fillRoundedRect(statusDot, 3.0f, Colors::accentPrimary);
             // Glow
             renderer.fillRoundedRect({statusDot.x - 2, statusDot.y - 2, 10, 10}, 5.0f, Colors::accentPrimary.withAlpha(0.4f));
        } else {
             // Bypassed: LED Off (Dark/Stroked)
             renderer.strokeRoundedRect(statusDot, 3.0f, 1.0f, Colors::textDisabled.withAlpha(0.5f));
        }
    }
}

NUIRect EffectChainRack::slotRectForTop(float slotY) const {
    auto bounds = getBounds();
    return {bounds.x + 8.0f, slotY, bounds.width - 16.0f, SLOT_HEIGHT - 6.0f};
}

NUIRect EffectChainRack::getSlotBounds(int index) const {
    auto bounds = getBounds();
    return slotRectForTop(bounds.y + 8.0f + index * SLOT_HEIGHT - m_scrollOffset);
}

bool EffectChainRack::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;

    m_currentMousePos = event.position;

    auto bounds = getBounds();

    // Early exit if mouse is outside our bounds and not dragging
    // Need to allow events if we are capturing mouse (like dragging knob or slot)
    bool isCapturing = (m_activeKnobSlot != -1 || m_draggingSlotIndex != -1);
    if (!bounds.contains(event.position) && !isCapturing) {
        if (m_hoveredSlot != -1) {
            m_hoveredSlot = -1;
            repaint();
        }
        return false;
    }

    // Wheel support (Scroll)
    if (std::abs(event.wheelDelta) > 0.001f && m_activeKnobSlot == -1) {
        m_scrollOffset -= event.wheelDelta * 20.0f;

        // Clamp scroll
        float contentHeight = MAX_SLOTS * SLOT_HEIGHT + 10;
        float viewHeight = getBounds().height;
        m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, std::max(0.0f, contentHeight - viewHeight));

        repaint();
        return true;
    }

    float my = event.position.y;
    float mx = event.position.x;

    // Update hover
    m_hoveredSlot = hitTestSlot(my);

    // Hit Testing Helpers
    auto isOverKnob = [&](int index) {
        if (index < 0) return false;
        NUIRect slotRect = getSlotBounds(index);
        float knobX = slotRect.x + slotRect.width - 22.0f;
        return (mx >= knobX - 2 && mx <= knobX + 22) && (my >= slotRect.y + 2 && my <= slotRect.y + 26);
    };

    auto isOverBypass = [&](int index) {
        if (index < 0) return false;
        NUIRect slotRect = getSlotBounds(index);
        float knobX = slotRect.x + slotRect.width - 22.0f;
        return (mx >= knobX - 20 && mx <= knobX - 2) && (my >= slotRect.y + 2 && my <= slotRect.y + 26);
    };

    // RELEASED Event Handling (Must be checked before general Drag handling to allow drops)
    if (event.released && event.button == NUIMouseButton::Left) {
        if (m_activeKnobSlot != -1) {
            m_activeKnobSlot = -1;
            return true;
        }

        // Handle Reorder Drop or Click
        if (m_isDraggingReorder && m_draggingSlotIndex != -1) {
             // Fix: Must account for scroll offset to map visual position back to slot index
             float contentY = event.position.y - (bounds.y + 8) + m_scrollOffset;
             int currentTarget = static_cast<int>(contentY / SLOT_HEIGHT);

             if (currentTarget >= 0 && currentTarget < MAX_SLOTS && currentTarget != m_draggingSlotIndex) {
                 if (m_onSlotMoveRequested) {
                     m_onSlotMoveRequested(m_draggingSlotIndex, currentTarget);
                 }
             }
        }
        else {
             // Single Click Action
             // Do nothing (User requested Double Click to open)
        }

        m_draggingSlotIndex = -1;
        m_isDraggingReorder = false;
        repaint();
        return true;
    }

    // ONGOING DRAG Handling
    // If we are in dragging mode, consume events (Move/Drag)
    if (m_draggingSlotIndex != -1) {
        // Slot Reorder Drag Check
        m_currentMousePos = event.position;
        float dist = std::abs(event.position.y - m_dragStartPos.y);
        if (dist > 5.0f && !m_isDraggingReorder) {
            m_isDraggingReorder = true;
        }

        // Allow Move/Drag/None buttons to update the drag
        if (event.type == NUIMouseEventType::Drag || event.button == NUIMouseButton::Left || event.button == NUIMouseButton::None) {
             repaint();
             return true;
        }
    }

    // KNOB DRAG Handling
    if (m_activeKnobSlot != -1) {
        const float dx = event.position.x - m_dragStartPos.x;
        const float dy = m_dragStartPos.y - event.position.y; // Up is positive (Values go up as mouse goes up)
        const float dragDelta = dx + dy;

        float sensitivity = 0.005f;
        if (event.modifiers & NUIModifiers::Shift) sensitivity *= 0.1f;

        float newValue = std::clamp(m_dragStartValue + dragDelta * sensitivity, 0.0f, 1.0f);

        if (std::abs(newValue - m_slots[m_activeKnobSlot].dryWet) > 0.001f) {
            m_slots[m_activeKnobSlot].dryWet = newValue;
            if (m_onSlotMixChanged) {
                m_onSlotMixChanged(m_activeKnobSlot, newValue);
            }
            repaint();
        }
        return true;
    }

    // CLICK (PRESSED) Event Handling
    int slotIdx = hitTestSlot(my);

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (slotIdx >= 0 && slotIdx < MAX_SLOTS) {
             // 1. Knob Hit Test
             if (isOverKnob(slotIdx)) {
                 if (!m_slots[slotIdx].isEmpty) {
                     m_activeKnobSlot = slotIdx;
                     m_dragStartValue = m_slots[slotIdx].dryWet;
                     m_dragStartPos = event.position;
                     return true;
                 }
             }
             // 2. Bypass Hit Test
             else if (isOverBypass(slotIdx)) {
                 if (!m_slots[slotIdx].isEmpty) {
                     bool newState = !m_slots[slotIdx].bypassed;
                     m_slots[slotIdx].bypassed = newState;
                     m_bypassOverride[slotIdx] = newState ? 1 : 0;
                     if (m_onSlotBypassToggled) m_onSlotBypassToggled(slotIdx, newState);
                     repaint();
                     return true;
                 }
             }
             // 3. Slot Click (Selection / Drag Start)
             else {
                 // Double Click Check
                 auto now = std::chrono::steady_clock::now();
                 auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastClickTime).count();
                 bool isDoubleClick = (slotIdx == m_lastClickSlot && elapsed < 300);

                  m_lastClickTime = now;
                  m_lastClickSlot = slotIdx;

                  if (isDoubleClick) {
                     if (!m_slots[slotIdx].isEmpty) {
                        if (m_onSlotClicked) m_onSlotClicked(slotIdx);
                     } else {
                        if (m_onAddPluginRequested) m_onAddPluginRequested(slotIdx);
                     }
                     m_lastClickSlot = -1; // Reset to avoid triple-click issues
                 } else {
                     // Single click - Prepare for Drag
                     if (!m_slots[slotIdx].isEmpty) {
                         m_draggingSlotIndex = slotIdx;
                         m_dragStartPos = event.position;
                     }
                  }
                  return true;
              }
        }
    }
    else if (event.pressed && event.button == NUIMouseButton::Right) {
         if (slotIdx >= 0 && slotIdx < MAX_SLOTS && !m_slots[slotIdx].isEmpty) {
            // Close existing menu properly if it exists
            if (m_contextMenu) {
                if (m_contextMenu->getParent()) m_contextMenu->getParent()->removeChild(m_contextMenu);
                // Also try local remove just in case
                removeChild(m_contextMenu);
                m_contextMenu = nullptr;
            }

            m_contextMenuSlot = slotIdx;
            m_contextMenu = std::make_shared<NUIContextMenu>();

            // DELETE ACTION
            auto deleteItem = std::make_shared<NUIContextMenuItem>("Delete", NUIContextMenuItem::Type::Normal);
            deleteItem->setOnClick([this]() {
                if (m_onSlotRemoveRequested) {
                    if (m_contextMenuSlot >= 0) {
                        m_onSlotRemoveRequested(m_contextMenuSlot);
                    }
                } else {
                    Aestra::Log::warning("[Rack] m_onSlotRemoveRequested is NULL! Callback not bound.");
                }
                if (m_contextMenu) {
                    if (m_contextMenu->getParent()) m_contextMenu->getParent()->removeChild(m_contextMenu);
                    removeChild(m_contextMenu);
                    m_contextMenu = nullptr;
                }
                m_contextMenuSlot = -1;
            });
            m_contextMenu->addItem(deleteItem);

            // BYPASS ACTION
            bool currentBypass = m_slots[slotIdx].bypassed;
            auto bypassItem = std::make_shared<NUIContextMenuItem>(
                currentBypass ? "Enable" : "Bypass",
                NUIContextMenuItem::Type::Normal
            );
            bypassItem->setOnClick([this, slotIdx, currentBypass]() {
                bool newState = !currentBypass;
                m_slots[slotIdx].bypassed = newState;
                m_bypassOverride[slotIdx] = newState ? 1 : 0;
                if (m_onSlotBypassToggled) {
                    m_onSlotBypassToggled(slotIdx, newState);
                }
                if (m_contextMenu) {
                    if (m_contextMenu->getParent()) m_contextMenu->getParent()->removeChild(m_contextMenu);
                    removeChild(m_contextMenu);
                    m_contextMenu = nullptr;
                }
                repaint();
            });
            m_contextMenu->addItem(bypassItem);

            // ADD TO ROOT (The only robust way to handle context menus to avoid clipping and coordinate hell)
            NUIComponent* root = this;
            while (root->getParent()) {
                root = root->getParent();
            }

            if (root) {
                root->addChild(m_contextMenu);
                // NUIContextMenu::showAt calls setPosition.
                // Since we are adding to Root, Absolute Position == Relative Position.
                // So passing event.position (Absolute) is correct.
                m_contextMenu->showAt(event.position);
                root->repaint(); // Ensure root repaints to show the new overlay
            } else {
                // Fallback (Should never happen in valid hierarchy)
                addChild(m_contextMenu);
                m_contextMenu->showAt(event.position);
            }

            repaint();
            return true;
        }
    }

    // If we are hovering a valid slot, consume the event to prevent 'fall-through' to parent
    // which might think we are hovering "Add Send" or other overlapped widgets.
    if (m_hoveredSlot != -1) {
        return true;
    }

    return false;
}




void EffectChainRack::setSlot(int index, const EffectSlotInfo& info) {
    if (index >= 0 && index < MAX_SLOTS) {
        m_slots[index] = info;

        // Apply Override Logic
        if (m_bypassOverride[index] != -1) {
            bool forcedState = (m_bypassOverride[index] == 1);

            // If backend matches override, we are synced -> Clear override
            if (info.bypassed == forcedState) {
                m_bypassOverride[index] = -1;
            } else {
                // Otherwise force UI to keep user choice
                m_slots[index].bypassed = forcedState;
            }
        }
        repaint();
    }
}

const EffectChainRack::EffectSlotInfo& EffectChainRack::getSlot(int index) const {
    static EffectSlotInfo empty;
    if (index >= 0 && index < MAX_SLOTS) {
        return m_slots[index];
    }
    return empty;
}

void EffectChainRack::setOnSlotClicked(std::function<void(int)> callback) {
    m_onSlotClicked = std::move(callback);
}

void EffectChainRack::setOnSlotMoveRequested(std::function<void(int, int)> callback) {
    m_onSlotMoveRequested = std::move(callback);
}

void EffectChainRack::setOnSlotBypassToggled(std::function<void(int, bool)> callback) {
    m_onSlotBypassToggled = std::move(callback);
}

void EffectChainRack::setOnSlotRemoveRequested(std::function<void(int)> callback) {
    m_onSlotRemoveRequested = std::move(callback);
}

void EffectChainRack::setOnAddPluginRequested(std::function<void(int)> callback) {
    m_onAddPluginRequested = std::move(callback);
}

void EffectChainRack::setOnSlotMixChanged(std::function<void(int, float)> callback) {
    m_onSlotMixChanged = std::move(callback);
}

int EffectChainRack::hitTestSlot(float y) const {
    auto bounds = getBounds();
    float relativeY = y - bounds.y - 8 + m_scrollOffset;
    if (relativeY < 0.0f) {
        return -1;
    }

    int index = static_cast<int>(std::floor(relativeY / SLOT_HEIGHT));
    if (index >= 0 && index < MAX_SLOTS) {
        return index;
    }
    return -1;
}

} // namespace AestraUI
