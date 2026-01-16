// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PluginBrowserPanel.h"
#include "NUIRenderer.h"
#include "NUIDragDrop.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace AestraUI {

// ============================================================================
// Theme colors (inline)
// ============================================================================

namespace Colors {
    static const NUIColor panelBackground = {0.12f, 0.12f, 0.14f, 1.0f};
    static const NUIColor panelBorder = {0.25f, 0.25f, 0.28f, 1.0f};
    static const NUIColor textPrimary = {0.95f, 0.95f, 0.97f, 1.0f};
    static const NUIColor textSecondary = {0.65f, 0.65f, 0.70f, 1.0f};
    static const NUIColor textDisabled = {0.45f, 0.45f, 0.50f, 1.0f};
    static const NUIColor accentPrimary = {0.55f, 0.35f, 0.85f, 1.0f};
    static const NUIColor accentWarning = {0.95f, 0.75f, 0.25f, 1.0f};
    static const NUIColor buttonBackground = {0.22f, 0.22f, 0.25f, 1.0f};
    static const NUIColor inputBackground = {0.08f, 0.08f, 0.10f, 1.0f};
    static const NUIColor listHover = {0.20f, 0.20f, 0.24f, 1.0f};
    static const NUIColor listSelected = {0.35f, 0.25f, 0.55f, 0.5f};
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
    
    renderer.fillRoundedRect(bounds, 8.0f, Colors::panelBackground);
    renderer.strokeRoundedRect(bounds, 8.0f, 1.0f, Colors::panelBorder);
    
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
    
    renderer.drawText("Plugins", {bounds.x + 16, y + 12}, 16.0f, Colors::textPrimary);
    
    NUIRect scanBtn = {bounds.x + bounds.width - 80, y + 8, 64, 24};
    
    // Show different button state when scanning
    if (m_scanning) {
        renderer.fillRoundedRect(scanBtn, 4.0f, Colors::panelBorder); // Dimmed
        renderer.drawText("...", {scanBtn.x + 24, scanBtn.y + 5}, 12.0f, Colors::textDisabled);
    } else {
        renderer.fillRoundedRect(scanBtn, 4.0f, Colors::buttonBackground);
        renderer.drawText("Scan", {scanBtn.x + 16, scanBtn.y + 5}, 12.0f, Colors::textPrimary);
    }
}

void PluginBrowserPanel::renderTabs(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float y = bounds.y + HEADER_HEIGHT;
    
    const char* tabLabels[] = {"All", "FX", "Synth", "VST3", "CLAP", "*"};
    const int tabCount = 6;
    float tabWidth = bounds.width / tabCount;
    
    for (int i = 0; i < tabCount; ++i) {
        NUIRect tabRect = {bounds.x + i * tabWidth, y, tabWidth, TAB_HEIGHT};
        
        if (i == m_activeTab) {
            NUIRect highlight = {tabRect.x + 4, tabRect.y + tabRect.height - 3, 
                                 tabRect.width - 8, 3};
            renderer.fillRoundedRect(highlight, 1.5f, Colors::accentPrimary);
        }
        
        float textWidth = static_cast<float>(strlen(tabLabels[i])) * 7.0f;
        NUIColor tabColor = (i == m_activeTab) ? Colors::textPrimary : Colors::textSecondary;
        renderer.drawText(tabLabels[i], 
                         {tabRect.x + (tabRect.width - textWidth) / 2, tabRect.y + 10}, 
                         11.0f, tabColor);
    }
    
    renderer.drawLine({bounds.x, y + TAB_HEIGHT - 1}, 
                     {bounds.x + bounds.width, y + TAB_HEIGHT - 1}, 
                     1.0f, Colors::panelBorder);
}

void PluginBrowserPanel::renderSearchBar(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float y = bounds.y + HEADER_HEIGHT + TAB_HEIGHT;
    
    NUIRect searchRect = {bounds.x + 8, y + 4, bounds.width - 16, SEARCH_HEIGHT - 8};
    renderer.fillRoundedRect(searchRect, 6.0f, Colors::inputBackground);
    
    if (m_searchQuery.empty()) {
        renderer.drawText("Search plugins...", {searchRect.x + 12, searchRect.y + 7}, 
                         12.0f, Colors::textDisabled);
    } else {
        renderer.drawText(m_searchQuery, {searchRect.x + 12, searchRect.y + 7}, 
                         12.0f, Colors::textPrimary);
    }
}

void PluginBrowserPanel::renderPluginList(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float listTop = bounds.y + HEADER_HEIGHT + TAB_HEIGHT + SEARCH_HEIGHT;
    float listHeight = bounds.height - HEADER_HEIGHT - TAB_HEIGHT - SEARCH_HEIGHT;
    
    renderer.setClipRect({bounds.x, listTop, bounds.width, listHeight});
    
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
    NUIRect rowRect = {bounds.x + 4, yOffset, bounds.width - 8, ROW_HEIGHT};
    
    if (index == m_selectedIndex) {
        renderer.fillRoundedRect(rowRect, 4.0f, Colors::listSelected);
    } else if (index == m_hoveredIndex) {
        renderer.fillRoundedRect(rowRect, 4.0f, Colors::listHover);
    }
    
    if (plugin.isFavorite) {
        renderer.drawText("*", {rowRect.x + 8, yOffset + 8}, 14.0f, Colors::accentWarning);
    }
    
    float textX = rowRect.x + (plugin.isFavorite ? 24 : 12);
    
    renderer.drawText(plugin.name, {textX, yOffset + 4}, 12.0f, Colors::textPrimary);
    renderer.drawText(plugin.vendor, {textX, yOffset + 18}, 10.0f, Colors::textSecondary);
    
    float badgeX = rowRect.x + rowRect.width - 36;
    NUIRect badge = {badgeX, yOffset + 6, 28, 14};
    
    NUIColor badgeColor = (plugin.formatStr == "VST3") 
        ? NUIColor{0.2f, 0.4f, 0.8f, 0.3f}
        : NUIColor{0.4f, 0.7f, 0.3f, 0.3f};
    renderer.fillRoundedRect(badge, 3.0f, badgeColor);
    renderer.drawText(plugin.formatStr, {badgeX + 2, yOffset + 8}, 8.0f, Colors::textPrimary);
}

void PluginBrowserPanel::renderScanProgress(NUIRenderer& renderer) {
    auto bounds = getBounds();
    float listTop = bounds.y + HEADER_HEIGHT + TAB_HEIGHT + SEARCH_HEIGHT;
    
    renderer.fillRect({bounds.x, listTop, bounds.width, 
                      bounds.height - HEADER_HEIGHT - TAB_HEIGHT - SEARCH_HEIGHT}, 
                     {0.0f, 0.0f, 0.0f, 0.7f});
    
    float barWidth = bounds.width - 40;
    float barX = bounds.x + 20;
    float barY = listTop + 60;
    
    renderer.fillRoundedRect({barX, barY, barWidth, 8}, 4.0f, Colors::panelBorder);
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
    
    // Click handling
    if (event.pressed && event.button == NUIMouseButton::Left) {
        // Tab clicks
        float tabY = bounds.y + HEADER_HEIGHT;
        if (my >= tabY && my < tabY + TAB_HEIGHT) {
            float tabWidth = bounds.width / 6;
            int tabIndex = static_cast<int>((mx - bounds.x) / tabWidth);
            if (tabIndex >= 0 && tabIndex < 6) {
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
        
        // Plugin list clicks (Press down)
        float listTop = bounds.y + HEADER_HEIGHT + TAB_HEIGHT + SEARCH_HEIGHT;
        if (my >= listTop) {
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
        NUIRect scanBtn = {bounds.x + bounds.width - 80, bounds.y + 8, 64, 24};
        if (mx >= scanBtn.x && mx < scanBtn.x + scanBtn.width &&
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
    float listTop = bounds.y + HEADER_HEIGHT + TAB_HEIGHT + SEARCH_HEIGHT;
    if (my >= listTop) {
        m_hoveredIndex = hitTestRow(static_cast<int>(my));
    } else {
        m_hoveredIndex = -1;
    }
    
    // Scroll handling
    if (event.wheelDelta != 0.0f) {
        float listHeight = bounds.height - HEADER_HEIGHT - TAB_HEIGHT - SEARCH_HEIGHT;
        float contentHeight = m_filteredPlugins.size() * ROW_HEIGHT;
        float maxScroll = std::max(0.0f, contentHeight - listHeight);
        
        m_targetScrollOffset -= event.wheelDelta * 40.0f;
        if (m_targetScrollOffset < 0.0f) m_targetScrollOffset = 0.0f;
        if (m_targetScrollOffset > maxScroll) m_targetScrollOffset = maxScroll;
        return true;
    }
    
    // Consume event if mouse is within our bounds to prevent click-through
    return bounds.contains(event.position);
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
                passType = (p.formatStr == "CLAP");
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
    float listTop = bounds.y + HEADER_HEIGHT + TAB_HEIGHT + SEARCH_HEIGHT;
    
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
}

void EffectChainRack::onRender(NUIRenderer& renderer) {
    auto bounds = getBounds();
    
    renderer.fillRoundedRect(bounds, 6.0f, Colors::panelBackground);
    renderer.strokeRoundedRect(bounds, 6.0f, 1.0f, Colors::panelBorder);
    
    // Enable clipping
    renderer.setClipRect(bounds);
    
    for (int i = 0; i < MAX_SLOTS; ++i) {
        renderSlot(renderer, i, bounds.y + 5 + i * SLOT_HEIGHT - m_scrollOffset);
    }
    
    renderer.clearClipRect();
}

void EffectChainRack::renderSlot(NUIRenderer& renderer, int index, float yOffset) {
    auto bounds = getBounds();
    NUIRect slotRect = {bounds.x + 4, yOffset, bounds.width - 8, SLOT_HEIGHT - 2};
    
    const auto& slot = m_slots[index];
    
    NUIColor bgColor;
    if (index == m_hoveredSlot) {
        bgColor = Colors::listHover;
    } else if (!slot.isEmpty) {
        bgColor = {0.15f, 0.15f, 0.18f, 1.0f};
    } else {
        bgColor = {0.0f, 0.0f, 0.0f, 0.1f};
    }
    renderer.fillRoundedRect(slotRect, 4.0f, bgColor);
    
    renderer.drawText(std::to_string(index + 1), {slotRect.x + 4, yOffset + 7}, 9.0f, Colors::textDisabled);
    
    if (slot.isEmpty) {
        renderer.drawText("+ Add Plugin", {slotRect.x + 20, yOffset + 7}, 10.0f, Colors::textDisabled);
    } else {
        NUIColor nameColor = slot.bypassed ? Colors::textDisabled : Colors::textPrimary;
        renderer.drawText(slot.name, {slotRect.x + 20, yOffset + 7}, 10.0f, nameColor);
        
        if (slot.bypassed) {
            NUIRect bypassBadge = {slotRect.x + slotRect.width - 16, yOffset + 6, 12, 12};
            renderer.fillRoundedRect(bypassBadge, 2.0f, Colors::accentWarning);
        }
    }
}

bool EffectChainRack::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;

    auto bounds = getBounds();
    
    // Early exit if mouse is outside our bounds
    if (!bounds.contains(event.position)) {
        // Clear hover state if mouse leaves
        if (m_hoveredSlot != -1) {
            m_hoveredSlot = -1;
            repaint();
        }
        return false;
    }

    // Wheel support - only handle if mouse is over the rack
    if (event.wheelDelta != 0) {
        m_scrollOffset -= event.wheelDelta * 20.0f;
        
        // Clamp scroll
        float contentHeight = MAX_SLOTS * SLOT_HEIGHT + 10;
        float viewHeight = getBounds().height;
        m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, std::max(0.0f, contentHeight - viewHeight));
        
        repaint();
        return true;
    }

    float my = event.position.y;
    
    // Update hover
    m_hoveredSlot = hitTestSlot(my);
    
    // Handle click
    if (event.pressed && event.button == NUIMouseButton::Left) {
        int slot = hitTestSlot(my);
        if (slot >= 0 && slot < MAX_SLOTS) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastClickTime).count();
            
            bool isDoubleClick = (slot == m_lastClickSlot && elapsed < 300);
            
            if (isDoubleClick) {
                if (m_onAddPluginRequested) {
                    m_onAddPluginRequested(slot);
                }
                m_lastClickSlot = -1; // Reset
            } else {
                if (!m_slots[slot].isEmpty) {
                    if (m_onSlotClicked) {
                        m_onSlotClicked(slot);
                    }
                }
                m_lastClickTime = now;
                m_lastClickSlot = slot;
            }
            return true;
        }
    }
    
    // Don't consume events we didn't handle - let them pass through
    return false;
}

void EffectChainRack::setSlot(int index, const EffectSlotInfo& info) {
    if (index >= 0 && index < MAX_SLOTS) {
        m_slots[index] = info;
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

void EffectChainRack::setOnSlotBypassToggled(std::function<void(int, bool)> callback) {
    m_onSlotBypassToggled = std::move(callback);
}

void EffectChainRack::setOnSlotRemoveRequested(std::function<void(int)> callback) {
    m_onSlotRemoveRequested = std::move(callback);
}

void EffectChainRack::setOnAddPluginRequested(std::function<void(int)> callback) {
    m_onAddPluginRequested = std::move(callback);
}

int EffectChainRack::hitTestSlot(float y) const {
    auto bounds = getBounds();
    if (!bounds.contains({bounds.x + 10, y})) {
        return -1;
    }
    
    int index = static_cast<int>((y - bounds.y - 5 + m_scrollOffset) / SLOT_HEIGHT);
    if (index >= 0 && index < MAX_SLOTS) {
        return index;
    }
    return -1;
}

} // namespace AestraUI
