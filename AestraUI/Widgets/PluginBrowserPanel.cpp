// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PluginBrowserPanel.h"
#include "NUIRenderer.h"
#include "NUIDragDrop.h"
#include "NUIContextMenu.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <cstdio>
#include "../../AestraCore/include/AestraLog.h"

namespace AestraUI {

// Theme colors (inline)
// Theme colors (inline)
// ============================================================================

namespace Colors {
    static const NUIColor panelBackground = NUIThemeManager::getInstance().getColor("backgroundPrimary");
    static const NUIColor panelTop = NUIThemeManager::getInstance().getColor("backgroundSecondary").withAlpha(0.92f);
    static const NUIColor panelBorder = NUIThemeManager::getInstance().getColor("border").withAlpha(0.40f);
    static const NUIColor textPrimary = NUIThemeManager::getInstance().getColor("textPrimary");
    static const NUIColor textSecondary = NUIThemeManager::getInstance().getColor("textSecondary");
    static const NUIColor textDisabled = NUIThemeManager::getInstance().getColor("textDisabled");
    static const NUIColor accentPrimary = NUIThemeManager::getInstance().getColor("accentPrimary");
    static const NUIColor accentSecondary = NUIThemeManager::getInstance().getColor("accentSecondary");
    static const NUIColor accentWarning = NUIThemeManager::getInstance().getColor("warning");
    static const NUIColor buttonBackground = NUIThemeManager::getInstance().getColor("buttonBgDefault").withAlpha(0.94f);
    static const NUIColor buttonBackgroundHover = NUIThemeManager::getInstance().getColor("buttonBgHover").withAlpha(0.84f);
    static const NUIColor inputBackground = NUIThemeManager::getInstance().getColor("inputBgDefault");
    static const NUIColor rowBackground = NUIThemeManager::getInstance().getColor("buttonBgDefault").withAlpha(0.82f);
    static const NUIColor listHover = NUIThemeManager::getInstance().getColor("buttonBgHover").withAlpha(0.78f);
    static const NUIColor listSelected = NUIThemeManager::getInstance().getColor("accentPrimary").withAlpha(0.20f);
}

// ============================================================================
// PluginBrowserPanel Implementation
// ============================================================================

PluginBrowserPanel::PluginBrowserPanel() {
    setSize(300, 500);
}

void PluginBrowserPanel::onRender(NUIRenderer& renderer) {
    std::lock_guard<std::recursive_mutex> lock(m_uiMutex);
    auto bounds = getBounds();
    
    renderer.drawShadow(bounds, 0.0f, 8.0f, 18.0f, NUIColor(0, 0, 0, 0.12f));
    renderer.fillRoundedRect(bounds, 12.0f, Colors::panelBackground);
    renderer.fillRoundedRect({bounds.x, bounds.y, bounds.width, 52.0f}, 12.0f, Colors::panelTop);
    renderer.strokeRoundedRect(bounds, 12.0f, 1.0f, Colors::panelBorder);
    
    renderHeader(renderer);
    renderTabs(renderer);
    renderSearchBar(renderer);
    renderPluginList(renderer);
    
    if (m_scanning) {
        renderScanProgress(renderer);
    }
}

void PluginBrowserPanel::renderHeader(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float y = bounds.y;
    
    renderer.drawText("Plugins", {bounds.x + 16, y + 15}, 16.0f, Colors::textPrimary);
    
    NUIRect scanBtn = {bounds.x + bounds.width - 74, y + 11, 54, 22};
    
    if (m_scanning) {
        renderer.fillRoundedRect(scanBtn, 10.0f, Colors::buttonBackground);
        renderer.strokeRoundedRect(scanBtn, 10.0f, 1.0f, Colors::panelBorder);
        auto dots = renderer.measureText("...", 12.0f);
        renderer.drawText("...", {scanBtn.x + (scanBtn.width - dots.width) * 0.5f, scanBtn.y + 4}, 12.0f, Colors::textDisabled);
    } else {
        renderer.drawShadow(scanBtn, 0.0f, 4.0f, 10.0f, NUIColor(0, 0, 0, 0.10f));
        renderer.fillRoundedRect(scanBtn, 10.0f, Colors::buttonBackground);
        renderer.strokeRoundedRect(scanBtn, 10.0f, 1.0f, Colors::panelBorder);
        auto label = renderer.measureText("Scan", 11.0f);
        renderer.drawText("Scan", {scanBtn.x + (scanBtn.width - label.width) * 0.5f, scanBtn.y + 4}, 11.0f, Colors::textSecondary.withAlpha(0.92f));
    }
}

void PluginBrowserPanel::renderTabs(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float y = bounds.y + HEADER_HEIGHT + 4.0f;
    
    const char* tabLabels[] = {"All", "FX", "Synth", "VST3", "CLAP", "*"};
    const int tabCount = 6;
    const float gap = 4.0f;
    const float totalWidth = bounds.width - 20.0f;
    float tabWidth = (totalWidth - gap * (tabCount - 1)) / tabCount;
    
    for (int i = 0; i < tabCount; ++i) {
        NUIRect tabRect = {bounds.x + 10.0f + i * (tabWidth + gap), y, tabWidth, TAB_HEIGHT - 6.0f};
        
        if (i == m_activeTab) {
            renderer.fillRoundedRect(tabRect, 10.0f, Colors::buttonBackgroundHover);
            renderer.strokeRoundedRect(tabRect, 10.0f, 1.0f, Colors::accentPrimary.withAlpha(0.26f));
            renderer.fillRoundedRect({tabRect.x + 8.0f, tabRect.bottom() - 3.0f, tabRect.width - 16.0f, 2.0f}, 1.0f, Colors::accentPrimary);
        } else {
            renderer.fillRoundedRect(tabRect, 10.0f, Colors::buttonBackground.withAlpha(0.96f));
            renderer.strokeRoundedRect(tabRect, 10.0f, 1.0f, Colors::panelBorder.withAlpha(0.70f));
        }
        
        auto measured = renderer.measureText(tabLabels[i], 10.0f);
        NUIColor tabColor = (i == m_activeTab) ? Colors::textPrimary : Colors::textSecondary;
        renderer.drawText(tabLabels[i], 
                         {tabRect.x + (tabRect.width - measured.width) * 0.5f, tabRect.y + 7}, 
                         10.0f, tabColor);
    }
}

void PluginBrowserPanel::renderSearchBar(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float y = bounds.y + HEADER_HEIGHT + TAB_HEIGHT + 4.0f;
    
    NUIRect searchRect = {bounds.x + 10, y + 2, bounds.width - 20, SEARCH_HEIGHT - 12};
    renderer.fillRoundedRect(searchRect, 10.0f, Colors::inputBackground);
    renderer.strokeRoundedRect(searchRect, 10.0f, 1.0f, Colors::panelBorder);
    
    if (m_searchQuery.empty()) {
        renderer.drawText("Search plugins...", {searchRect.x + 12, searchRect.y + 5}, 
                         12.0f, Colors::textDisabled);
    } else {
        renderer.drawText(m_searchQuery, {searchRect.x + 12, searchRect.y + 5}, 
                         12.0f, Colors::textPrimary);
    }
}

void PluginBrowserPanel::renderPluginList(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float listTop = bounds.y + HEADER_HEIGHT + TAB_HEIGHT + SEARCH_HEIGHT + 4.0f;
    float listHeight = bounds.height - HEADER_HEIGHT - TAB_HEIGHT - SEARCH_HEIGHT - 4.0f;
    
    renderer.setClipRect({bounds.x + 8.0f, listTop, bounds.width - 16.0f, listHeight});
    
    float yOffset = listTop - m_scrollOffset;
    
    for (size_t i = 0; i < m_filteredPlugins.size(); ++i) {
        if (yOffset + ROW_HEIGHT > listTop && yOffset < listTop + listHeight) {
            renderPluginRow(renderer, m_filteredPlugins[i], static_cast<int>(i), yOffset);
        }
        yOffset += ROW_HEIGHT;
    }
    
    if (m_filteredPlugins.empty() && !m_scanning) {
        renderer.drawText("No plugins found", 
                         {bounds.x + bounds.width / 2 - 50, listTop + listHeight / 2 - 10}, 
                         12.0f, Colors::textDisabled);
    }
    
    renderer.clearClipRect();
}

void PluginBrowserPanel::renderPluginRow(NUIRenderer& renderer, 
                                          const PluginListItem& plugin,
                                          int index, float yOffset) {
    auto bounds = getBounds();
    NUIRect rowRect = {bounds.x + 10, yOffset + 5.0f, bounds.width - 20, ROW_HEIGHT - 10.0f};
    
    if (index == m_selectedIndex) {
        renderer.fillRoundedRect(rowRect, 10.0f, Colors::listSelected);
        renderer.strokeRoundedRect(rowRect, 10.0f, 1.0f, Colors::accentPrimary.withAlpha(0.26f));
        renderer.fillRoundedRect({rowRect.x, rowRect.y + 6.0f, 3.0f, rowRect.height - 12.0f}, 1.5f, Colors::accentPrimary);
    } else if (index == m_hoveredIndex) {
        renderer.fillRoundedRect(rowRect, 10.0f, Colors::listHover);
    } else {
        renderer.fillRoundedRect(rowRect, 10.0f, Colors::rowBackground);
    }
    
    if (plugin.isFavorite) {
        renderer.drawText("*", {rowRect.x + 8, rowRect.y + 7}, 14.0f, Colors::accentWarning);
    }
    
    float textX = rowRect.x + (plugin.isFavorite ? 24 : 12);
    
    std::string name = plugin.name;
    if (name.length() > 20) {
        name = name.substr(0, 17) + "...";
    }
    std::string vendorMeta = plugin.vendor;
    if (!plugin.typeName.empty()) {
        vendorMeta += " • " + plugin.typeName;
    }
    if (vendorMeta.length() > 24) {
        vendorMeta = vendorMeta.substr(0, 21) + "...";
    }
    renderer.drawText(name, {textX, rowRect.y + 5}, 12.0f, Colors::textPrimary);
    renderer.drawText(vendorMeta, {textX, rowRect.y + 22}, 9.0f, Colors::textSecondary.withAlpha(0.88f));
    
    // Badge width: VST3 = 30, CLAP (Exp.) = 48
    const bool isCLAP = (plugin.formatStr.find("CLAP") != std::string::npos);
    const float badgeWidth = isCLAP ? 48.0f : 30.0f;
    float badgeX = rowRect.x + rowRect.width - badgeWidth - 8.0f;
    NUIRect badge = {badgeX, rowRect.y + 7, badgeWidth, 14};
    
    NUIColor badgeColor = (plugin.formatStr == "VST3") 
        ? Colors::accentPrimary.withAlpha(0.22f)
        : Colors::accentSecondary.withAlpha(0.22f);
    renderer.fillRoundedRect(badge, 5.0f, badgeColor);
    renderer.strokeRoundedRect(badge, 5.0f, 1.0f, Colors::panelBorder);
    renderer.drawText(plugin.formatStr, {badgeX + 4, rowRect.y + 9}, 8.0f, Colors::textPrimary.withAlpha(0.94f));
}

void PluginBrowserPanel::renderScanProgress(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float listTop = bounds.y + HEADER_HEIGHT + TAB_HEIGHT + SEARCH_HEIGHT;
    
    renderer.fillRect({bounds.x, listTop, bounds.width,
                      bounds.height - HEADER_HEIGHT - TAB_HEIGHT - SEARCH_HEIGHT},
                     Colors::panelBackground.withAlpha(0.82f));
    
    float barWidth = bounds.width - 40;
    float barX = bounds.x + 20;
    float barY = listTop + 60;
    
    renderer.fillRoundedRect({barX, barY, barWidth, 8}, 4.0f, Colors::buttonBackground);
    renderer.fillRoundedRect({barX, barY, barWidth * m_scanProgress, 8}, 4.0f, Colors::accentPrimary);
    
    std::string status = m_scanStatus.empty() ? "Scanning plugins..." : m_scanStatus;
    renderer.drawText(status, {barX, barY - 20}, 12.0f, Colors::textPrimary);
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
        // Tab clicks
        float tabY = bounds.y + HEADER_HEIGHT + 4.0f;
        const float gap = 4.0f;
        const float totalWidth = bounds.width - 20.0f;
        const float tabWidth = (totalWidth - gap * 5.0f) / 6.0f;
        if (insideBounds && my >= tabY && my < tabY + TAB_HEIGHT - 6.0f) {
            for (int tabIndex = 0; tabIndex < 6; ++tabIndex) {
                NUIRect tabRect = {bounds.x + 10.0f + tabIndex * (tabWidth + gap), tabY, tabWidth, TAB_HEIGHT - 6.0f};
                if (tabRect.contains(event.position)) {
                    m_activeTab = tabIndex;
                    
                    PluginFilterType filters[] = {
                        PluginFilterType::All,
                        PluginFilterType::Effects,
                        PluginFilterType::Instruments,
                        PluginFilterType::VST3,
                        PluginFilterType::CLAP,
                        PluginFilterType::Favorites
                    };
                    setFilter(filters[tabIndex]);
                    return true;
                }
            }
        }
        
        // Plugin list clicks (Press down)
        float listTop = bounds.y + HEADER_HEIGHT + TAB_HEIGHT + SEARCH_HEIGHT + 4.0f;
        if (insideBounds && my >= listTop) {
            int rowIndex = hitTestRow(static_cast<int>(my));
            if (rowIndex >= 0 && rowIndex < static_cast<int>(m_filteredPlugins.size())) {
                // Initiate potential drag or click
                m_isPressed = true;
                m_pressedIndex = rowIndex;
                m_dragStartPos = event.position;
                return true;
            }
        }
        
        // Scan button (ignore if already scanning)
        NUIRect scanBtn = {bounds.x + bounds.width - 74, bounds.y + 11, 54, 22};
        if (insideBounds &&
            mx >= scanBtn.x && mx < scanBtn.x + scanBtn.width &&
            my >= scanBtn.y && my < scanBtn.y + scanBtn.height) {
            if (!m_scanning && m_onScanRequested) {
                m_onScanRequested();
            }
            return true;
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
    float listTop = bounds.y + HEADER_HEIGHT + TAB_HEIGHT + SEARCH_HEIGHT + 4.0f;
    if (insideBounds && my >= listTop) {
        m_hoveredIndex = hitTestRow(static_cast<int>(my));
    } else {
        m_hoveredIndex = -1;
    }
    
    // Scroll handling
    if (insideBounds && event.wheelDelta != 0.0f) {
        float listHeight = bounds.height - HEADER_HEIGHT - TAB_HEIGHT - SEARCH_HEIGHT;
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
    (void)event;
    return false;
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
        p.isFavorite = std::find(m_favorites.begin(), m_favorites.end(), p.id) != m_favorites.end();
    }
    applyFilters();
}

void PluginBrowserPanel::setFilter(PluginFilterType filter) {
    m_filterType = filter;
    applyFilters();
}

void PluginBrowserPanel::setSearchQuery(const std::string& query) {
    m_searchQuery = query;
    applyFilters();
}

void PluginBrowserPanel::applyFilters() {
    m_filteredPlugins.clear();
    
    for (const auto& p : m_allPlugins) {
        bool passType = true;
        switch (m_filterType) {
            case PluginFilterType::Effects:
                passType = (p.typeName == "Effect");
                break;
            case PluginFilterType::Instruments:
                passType = (p.typeName == "Instrument");
                break;
            case PluginFilterType::VST3:
                passType = (p.formatStr == "VST3");
                break;
            case PluginFilterType::CLAP:
                passType = (p.formatStr.find("CLAP") != std::string::npos);
                break;
            case PluginFilterType::Favorites:
                passType = p.isFavorite;
                break;
            default:
                break;
        }
        
        if (!passType) continue;
        
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
    
    if (m_selectedIndex >= static_cast<int>(m_filteredPlugins.size())) {
        m_selectedIndex = -1;
    }
    
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
    auto it = std::find(m_favorites.begin(), m_favorites.end(), pluginId);
    if (it != m_favorites.end()) {
        m_favorites.erase(it);
    } else {
        m_favorites.push_back(pluginId);
    }
    
    for (auto& p : m_allPlugins) {
        if (p.id == pluginId) {
            p.isFavorite = !p.isFavorite;
            break;
        }
    }
    for (auto& p : m_filteredPlugins) {
        if (p.id == pluginId) {
            p.isFavorite = !p.isFavorite;
            break;
        }
    }
}

void PluginBrowserPanel::setFavorites(const std::vector<std::string>& favorites) {
    m_favorites = favorites;
    for (auto& p : m_allPlugins) {
        p.isFavorite = std::find(m_favorites.begin(), m_favorites.end(), p.id) != m_favorites.end();
    }
    applyFilters();
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

int PluginBrowserPanel::hitTestRow(int y) const {
    auto bounds = getBounds();
    float listTop = bounds.y + HEADER_HEIGHT + TAB_HEIGHT + SEARCH_HEIGHT + 4.0f;
    
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
    
    renderer.fillRoundedRect(bounds, 10.0f, Colors::panelBackground);
    renderer.fillRoundedRect({bounds.x, bounds.y, bounds.width, 34.0f}, 10.0f, Colors::panelTop);
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
        bgColor = isHovered ? Colors::buttonBackgroundHover.withAlpha(0.72f) : Colors::buttonBackground.withAlpha(0.74f);
        borderColor = isHovered ? Colors::accentPrimary.withAlpha(0.26f) : Colors::panelBorder.withAlpha(0.42f);
    } else {
        // Populated: Solid dark glass
        // If bypassed, make it slightly dimmer/transparent
        if (isBeingDragged) {
            bgColor = isHovered ? Colors::buttonBackgroundHover.withAlpha(0.72f) : Colors::buttonBackground.withAlpha(0.62f);
            borderColor = Colors::accentPrimary.withAlpha(0.2f);
        } else if (slot.bypassed) {
             bgColor = Colors::buttonBackground.withAlpha(0.64f);
             borderColor = Colors::panelBorder.withAlpha(0.5f);
        } else {
             bgColor = isHovered ? Colors::buttonBackgroundHover.withAlpha(0.84f) : Colors::buttonBackground.withAlpha(0.80f);
             borderColor = isHovered ? Colors::accentPrimary.withAlpha(0.82f) : Colors::panelBorder;
        }
    }

    renderer.fillRoundedRect(slotRect, 8.0f, bgColor);

    // Dashed border for empty slots to signal droppability
    if (slot.isEmpty && !isBeingDragged) {
        const float dash = 4.0f;
        const float gap = 3.0f;
        const float seg = dash + gap;
        const float margin = 2.0f;
        const float lx = slotRect.x + margin;
        const float rx = slotRect.x + slotRect.width - margin;
        const float ty = slotRect.y + margin;
        const float by = slotRect.y + slotRect.height - margin;
        const NUIColor dashCol = isHovered ? Colors::accentPrimary.withAlpha(0.35f) : Colors::panelBorder.withAlpha(0.35f);
        // Top edge
        for (float x = lx; x < rx; x += seg) {
            renderer.drawLine({x, ty}, {std::min(x + dash, rx), ty}, 1.0f, dashCol);
        }
        // Bottom edge
        for (float x = lx; x < rx; x += seg) {
            renderer.drawLine({x, by}, {std::min(x + dash, rx), by}, 1.0f, dashCol);
        }
        // Left edge
        for (float y = ty; y < by; y += seg) {
            renderer.drawLine({lx, y}, {lx, std::min(y + dash, by)}, 1.0f, dashCol);
        }
        // Right edge
        for (float y = ty; y < by; y += seg) {
            renderer.drawLine({rx, y}, {rx, std::min(y + dash, by)}, 1.0f, dashCol);
        }
    } else {
        renderer.strokeRoundedRect(slotRect, 8.0f, 1.0f, borderColor);
    }

    // DEBUG: Visual indicator for pending removal
    if (slot.pendingRemoval) {
        renderer.strokeRoundedRect(slotRect, 4.0f, 2.0f, NUIColor(1.0f, 0.0f, 0.0f, 0.8f));
    }

    // Shared vertical midline for the slot row — center everything around it
    const float slotMid = slotRect.y + slotRect.height * 0.5f;

    // Slot Number (Left side, stylistic) — centered on slotMid
    char numBuf[8];
    std::snprintf(numBuf, sizeof(numBuf), "%d", index + 1);
    const float chipH = 14.0f;
    const NUIRect indexChip{slotRect.x + 8.0f, slotMid - chipH * 0.5f, 18.0f, chipH};
    renderer.fillRoundedRect(indexChip, 7.0f, Colors::buttonBackgroundHover.withAlpha(slot.isEmpty ? 0.62f : 0.76f));
    renderer.strokeRoundedRect(indexChip, 7.0f, 1.0f, Colors::panelBorder.withAlpha(0.35f));
    renderer.drawTextCentered(numBuf, indexChip, 9.0f, Colors::textDisabled.withAlpha(0.68f));

    // Available text area to the right of the chip
    const float textX = slotRect.x + 36.0f;
    const float textW = slotRect.width - 36.0f - 8.0f;

    if (slot.isEmpty) {
        const float textSize = isHovered ? 10.0f : 9.5f;
        const NUIColor textColor = isHovered
            ? Colors::textPrimary
            : Colors::textDisabled.withAlpha(0.56f);
        // Single line centered on slotMid — same mechanism as the chip text
        const NUIRect textRect{textX, slotMid - 10.0f, textW, 20.0f};
        renderer.drawTextCentered(isHovered ? "+ Add Insert" : "Empty slot",
                                  textRect, textSize, textColor);
    } else {
        // Two lines centered around slotMid
        NUIColor nameColor = slot.bypassed ? Colors::textDisabled.withAlpha(0.6f) : Colors::textPrimary;
        const NUIRect nameRect{textX, slotMid - 11.0f, textW, 11.0f};
        renderer.drawTextCentered(slot.name, nameRect, 10.5f, nameColor);

        const NUIRect statusRect{textX, slotMid, textW, 10.0f};
        renderer.drawTextCentered(slot.bypassed ? "Bypassed" : "Active",
                                  statusRect, 8.5f,
                                  slot.bypassed ? Colors::textDisabled.withAlpha(0.72f) : Colors::accentPrimary.withAlpha(0.78f));
        
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
