// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UIMixerPluginDropdown.h"

#include "../../AestraCore/include/AestraLog.h"
#include "Helpers/MixerPluginListPolicy.h"
#include "NUIIcon.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cctype>

namespace AestraUI {

namespace {

static std::shared_ptr<NUIIcon> getIconCached(const char* name) {
    static std::unordered_map<std::string, std::shared_ptr<NUIIcon>> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;

    static const std::unordered_map<std::string, std::string> icons = {
        {"search",
         R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="7"/><line x1="16" y1="16" x2="21" y2="21"/></svg>)"},
        {"external-link",
         R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 3h7v7"/><path d="M10 14L21 3"/><path d="M21 14v7H3V3h7"/></svg>)"},
        {"chart-bar",
         R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 20V10"/><path d="M10 20V4"/><path d="M16 20v-7"/><path d="M22 20H2"/></svg>)"},
        {"wave-sine",
         R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M2 12c3-10 5 10 8 0s5-10 8 0 5 10 6 0"/></svg>)"},
        {"circles",
         R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="8" cy="12" r="5"/><circle cx="16" cy="12" r="5"/></svg>)"},
        {"clock",
         R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>)"}};
    const auto svg = icons.find(name);
    auto icon = std::make_shared<NUIIcon>(svg != icons.end() ? svg->second : icons.at("circles"));
    cache[name] = icon;
    return icon;
}

bool icontains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }
    );
    return it != haystack.end();
}

} // namespace

UIMixerPluginDropdown::UIMixerPluginDropdown()
{
    cacheThemeColors();
    m_filtered = m_categories;
    setVisible(false);

    // Real text input — same pattern as the library search bar in FileBrowser.
    m_searchInput = std::make_shared<NUITextInput>();
    m_searchInput->setPlaceholderText("Search plugins...");
    m_searchInput->setShowPlaceholderWhenFocused(true);
    m_searchInput->setBackgroundVisible(false);
    m_searchInput->setBorderWidth(0.0f);
    m_searchInput->setTextColor(m_textPrimary);
    m_searchInput->setPlaceholderColor(m_textTertiary);
    m_searchInput->setPadding(8.0f);
    m_searchInput->setMaxLength(512);
    m_searchInput->setBorderRadius(4.0f);
    m_searchInput->setOnTextChange([this](const std::string& text) {
        m_searchQuery = text;
        filter();
        repaint();
    });
    m_searchInput->setOnEscapeKey([this]() {
        dismiss();
    });
    addChild(m_searchInput);
}

void UIMixerPluginDropdown::cacheThemeColors()
{
    auto& theme = NUIThemeManager::getInstance();
    m_bg = theme.getColor("backgroundPrimary");
    m_bgSecondary = theme.getColor("backgroundSecondary");
    m_border = theme.getColor("border").withAlpha(0.50f);
    m_borderTertiary = theme.getColor("borderSubtle").withAlpha(0.50f);
    m_textPrimary = theme.getColor("textPrimary");
    m_textSecondary = theme.getColor("textSecondary");
    m_textTertiary = theme.getColor("textDisabled");
    m_accent = theme.getColor("accentPrimary");
    m_rowHover = theme.getColor("buttonBgHover").withAlpha(0.78f);
    m_searchBg = theme.getColor("backgroundSecondary");
}

void UIMixerPluginDropdown::setPluginEntries(std::vector<Aestra::Components::MixerPluginEntry> entries) {
    // The policy is the authority: mixer-insert filtering, category grouping,
    // icons and ordering all come from MixerPluginListPolicy.h.
    m_categories.clear();
    auto groups = Aestra::Components::groupForMixerDropdown(std::move(entries));
    for (auto& group : groups) {
        Category category;
        category.label = group.label;
        category.items.reserve(group.entries.size());
        for (auto& entry : group.entries) {
            category.items.push_back(PluginItem{entry.id, entry.name, entry.typeName, entry.category, group.icon});
        }
        m_categories.push_back(std::move(category));
    }
    filter();
}

std::vector<UIMixerPluginDropdown::Category> UIMixerPluginDropdown::computeDisplayCategories() const {
    // Single source of truth for what the user sees: prefers m_filtered but
    // falls back to m_categories (raw or re-filtered on the fly) when
    // m_filtered is stale-empty, so render, hit-testing and clicks always
    // operate on the same rows.
    if (!m_filtered.empty() || m_categories.empty()) {
        return m_filtered;
    }
    if (m_searchQuery.empty()) {
        return m_categories;
    }
    std::vector<Category> display;
    for (const auto& cat : m_categories) {
        Category filteredCat;
        filteredCat.label = cat.label;
        for (const auto& item : cat.items) {
            if (icontains(item.name, m_searchQuery) || icontains(item.typeLabel, m_searchQuery)) {
                filteredCat.items.push_back(item);
            }
        }
        if (!filteredCat.items.empty()) {
            display.push_back(std::move(filteredCat));
        }
    }
    return display;
}

void UIMixerPluginDropdown::filter()
{
    if (m_searchQuery.empty()) {
        m_filtered = m_categories;
        return;
    }
    m_filtered.clear();
    for (const auto& cat : m_categories) {
        Category filteredCat;
        filteredCat.label = cat.label;
        for (const auto& item : cat.items) {
            if (icontains(item.name, m_searchQuery) || icontains(item.typeLabel, m_searchQuery)) {
                filteredCat.items.push_back(item);
            }
        }
        if (!filteredCat.items.empty()) {
            m_filtered.push_back(std::move(filteredCat));
        }
    }
}

void UIMixerPluginDropdown::showAt(const NUIRect& triggerRect, float panelBottomY, float panelTopY) {
    // Defensive: if the catalog is empty when the user opens the dropdown
    // (because the initial setup-time refresh ran before the async plugin
    // scan completed), ask the host to republish. The dropdown is
    // re-shown on the next frame so the user sees the entries.
    if (m_categories.empty() && onRequestRefresh) {
        AESTRA_LOG_WARNING("[UIMixerPluginDropdown] open with empty categories; requesting refresh");
        onRequestRefresh();
    }
    float contentH = SEARCH_H;
    for (const auto& cat : m_categories) {
        contentH += CAT_HEADER_H;
        contentH += cat.items.size() * ROW_H;
    }
    contentH += FOOTER_H;
    float totalH = std::min(contentH, SEARCH_H + MAX_LIST_H + FOOTER_H);

    float x = triggerRect.x;
    float y = triggerRect.bottom() + 4.0f;
    m_openUpward = false;

    if (y + totalH > panelBottomY) {
        y = triggerRect.y - totalH - 4.0f;
        m_openUpward = true;
        // Panels clip their children: a flipped box that pokes above the
        // panel top is invisible. Pin it inside the panel instead — a menu
        // may overlap the trigger, but it must be on screen.
        const float minTop = panelTopY + 4.0f;
        if (y < minTop) {
            y = minTop;
        }
    }

    setBounds(NUIRect{x, y, DROP_W, totalH});
    m_searchInput->setBounds(NUIRect{x + 32.0f, y + 4.0f, DROP_W - 40.0f, SEARCH_H - 8.0f});
    // Clear the model before updating the child input: setText() emits the
    // input callback synchronously, so clearing m_searchQuery afterwards
    // leaves m_filtered empty while the field is visibly blank.
    m_searchQuery.clear();
    m_filtered = m_categories;
    m_searchInput->setText("");
    m_open = true;
    setVisible(true);
    m_hoveredRow = -1;
    m_hoveredFooter = -1;
    filter();
    m_searchInput->setFocused(true);
    repaint();
}

void UIMixerPluginDropdown::hide()
{
    if (!m_open) return;
    m_open = false;
    setVisible(false);
    setFocused(false);
    m_hoveredRow = -1;
    m_hoveredFooter = -1;
}

void UIMixerPluginDropdown::dismiss()
{
    hide();
    if (onDismissed) onDismissed();
}

std::vector<UIMixerPluginDropdown::FlatRow> UIMixerPluginDropdown::flatten(const std::vector<Category>& cats) const
{
    std::vector<FlatRow> rows;
    float y = SEARCH_H;
    for (size_t ci = 0; ci < cats.size(); ++ci) {
        rows.push_back({true, static_cast<int>(ci), -1, y, CAT_HEADER_H});
        y += CAT_HEADER_H;
        for (size_t ii = 0; ii < cats[ci].items.size(); ++ii) {
            rows.push_back({false, static_cast<int>(ci), static_cast<int>(ii), y, ROW_H});
            y += ROW_H;
        }
    }
    return rows;
}

int UIMixerPluginDropdown::hitTestRow(const NUIPoint& p) const
{
    auto b = getBounds();
    if (!b.contains(p)) return -1;
    // Same data source as the render path so hit-testing matches the rows
    // the user can actually see (see onRender for why this fallback exists).
    auto rows = flatten(computeDisplayCategories());
    for (size_t i = 0; i < rows.size(); ++i) {
        float ry = b.y + rows[i].y;
        if (p.y >= ry && p.y < ry + rows[i].h) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool UIMixerPluginDropdown::hitTestFooter(const NUIPoint& p) const
{
    auto b = getBounds();
    float fy = b.y + b.height - FOOTER_H;
    return p.y >= fy && p.y < b.bottom() && p.x >= b.x && p.x < b.right();
}

void UIMixerPluginDropdown::onRender(NUIRenderer& renderer)
{
    if (!m_open) return;
    auto b = getBounds();
    if (b.isEmpty()) return;
    // Defensive: if m_filtered is empty but m_categories has data, rebuild.
    if (m_filtered.empty() && !m_categories.empty()) {
        filter();
    }

    // Container background
    renderer.fillRoundedRect(b, RADIUS, m_bg);

    // Clip to container bounds so nothing bleeds past the rounded corners
    renderer.setClipRect(b);

    // Search section background (text input is transparent)
    NUIRect searchRect{b.x, b.y, b.width, SEARCH_H};
    renderer.fillRect(searchRect, m_searchBg);

    // Search icon
    auto searchIcon = getIconCached("search");
    if (searchIcon) {
        searchIcon->setIconSize(14.0f, 14.0f);
        searchIcon->setColor(m_textTertiary);
        searchIcon->setBounds(NUIRect(b.x + 10.0f, b.y + SEARCH_H * 0.5f - 7.0f, 14.0f, 14.0f));
        searchIcon->onRender(renderer);
    }

    // Render search input child (transparent background, draws text + caret only)
    renderChildren(renderer);

    // Divider between search and list
    renderer.drawLine({b.x, b.y + SEARCH_H}, {b.right(), b.y + SEARCH_H}, 0.5f, m_borderTertiary);

    // ── Plugin list ──
    // Render-time safety net: the data path has been observed to leave
    // m_filtered empty even when m_categories has data. computeDisplayCategories
    // regenerates the filtered view on the fly so the dropdown is never
    // visually empty if m_categories is non-empty.
    const auto displayCategories = computeDisplayCategories();
    auto rows = flatten(displayCategories);
    float listTop = b.y + SEARCH_H;
    float listH = b.height - SEARCH_H - FOOTER_H;

    bool anyItem = false;
    for (size_t ri = 0; ri < rows.size(); ++ri) {
        const auto& row = rows[ri];
        float ry = b.y + row.y;
        if (ry + row.h < listTop || ry > listTop + listH) continue;

        if (row.isCategory) {
            renderer.drawText(displayCategories[row.catIndex].label, {b.x + 12.0f, ry + 6.0f}, 10.0f, m_textTertiary);
        } else {
            anyItem = true;
            const auto& item = displayCategories[row.catIndex].items[row.itemIndex];
            bool hovered = (static_cast<int>(ri) == m_hoveredRow);
            NUIRect rowRect{b.x + 1.0f, ry, b.width - 2.0f, ROW_H};
            if (hovered) {
                renderer.fillRect(rowRect, m_rowHover);
            }

            float cx = b.x + 12.0f;
            float cy = ry + ROW_H * 0.5f;

            // Icon
            auto icon = getIconCached(item.iconName.c_str());
            if (icon) {
                icon->setIconSize(16.0f, 16.0f);
                icon->setColor(m_textSecondary);
                icon->setBounds(NUIRect(cx + 8.0f - 8.0f, cy - 8.0f, 16.0f, 16.0f));
                icon->onRender(renderer);
            }

            // Name + type labels
            float tx = cx + 28.0f;
            renderer.drawText(item.name, {tx, ry + 5.0f}, 13.0f, m_textPrimary);
            renderer.drawText(item.typeLabel, {tx, ry + 18.0f}, 11.0f, m_textTertiary);
        }
    }

    // Empty state
    if (!anyItem && m_searchQuery.empty()) {
        // Shouldn't happen with built-ins, but handle gracefully
    } else if (!anyItem) {
        renderer.drawTextCentered("No plugins found",
                                   NUIRect{b.x, listTop, b.width, listH},
                                   13.0f,
                                   m_textTertiary);
    }

    // ── Footer ──
    float fy = b.y + b.height - FOOTER_H;
    renderer.drawLine({b.x, fy}, {b.right(), fy}, 0.5f, m_borderTertiary);
    renderer.fillRect(NUIRect{b.x, fy, b.width, FOOTER_H}, m_searchBg);

    // Browse link
    NUIColor linkColor = (m_hoveredFooter == 0) ? m_textPrimary : m_textSecondary;
    // Library-aware footer: with no search active it invites the full browser;
    // with a dead-end search it points at the same browser pre-seeded with the
    // query, so no plugin is ever unreachable from the mixer.
    const std::string footerLabel =
        m_searchQuery.empty() ? "Browse all plugins" : "Search all plugins for '" + m_searchQuery + "'";
    renderer.drawText(footerLabel, {b.x + 12.0f, fy + 10.0f}, 12.0f, linkColor);
    auto extIcon = getIconCached("external-link");
    if (extIcon) {
        extIcon->setIconSize(13.0f, 13.0f);
        extIcon->setColor(m_textTertiary);
        extIcon->setBounds(NUIRect(b.right() - 22.0f - 6.5f, fy + FOOTER_H * 0.5f - 6.5f, 13.0f, 13.0f));
        extIcon->onRender(renderer);
    }

    // Clear container clip before drawing border on top
    renderer.clearClipRect();

    // Border stroke on top so it cleanly frames everything
    renderer.strokeRoundedRect(b, RADIUS, 0.5f, m_border);
}

bool UIMixerPluginDropdown::onMouseEvent(const NUIMouseEvent& event)
{
    if (!m_open) return false;

    auto b = getBounds();

    // Click outside dismisses
    if (event.pressed && !b.contains(event.position)) {
        dismiss();
        return true;
    }

    if (!b.contains(event.position)) {
        return false;
    }

    // Give the search input first dibs on events inside its bounds
    if (m_searchInput && m_searchInput->isVisible() && m_searchInput->getBounds().contains(event.position)) {
        if (m_searchInput->onMouseEvent(event)) {
            return true;
        }
    }

    // Update hover
    int newRow = hitTestRow(event.position);
    if (newRow != m_hoveredRow) {
        m_hoveredRow = newRow;
        repaint();
    }
    int newFooter = hitTestFooter(event.position) ? 0 : -1;
    if (newFooter != m_hoveredFooter) {
        m_hoveredFooter = newFooter;
        repaint();
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (m_hoveredFooter == 0) {
            dismiss();
            if (onBrowseAllRequested) onBrowseAllRequested(m_searchQuery);
            return true;
        }
        if (m_hoveredRow >= 0) {
            // Same data source as render/hit-test so a click lands on the
            // row the user actually saw (m_filtered can be stale-empty
            // while the fallback display path shows m_categories).
            const auto displayCategories = computeDisplayCategories();
            auto rows = flatten(displayCategories);
            const auto& row = rows[m_hoveredRow];
            if (!row.isCategory) {
                const auto& item = displayCategories[row.catIndex].items[row.itemIndex];
                dismiss();
                if (onPluginSelected) onPluginSelected(item.id, item.name);
                return true;
            }
        }
    }

    return true; // Consume events while open
}

bool UIMixerPluginDropdown::onKeyEvent(const NUIKeyEvent& event)
{
    if (!m_open) return false;

    if (event.pressed) {
        if (event.keyCode == NUIKeyCode::Escape) {
            dismiss();
            return true;
        }
    }
    return false;
}

} // namespace AestraUI
