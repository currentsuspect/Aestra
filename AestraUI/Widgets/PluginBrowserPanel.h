// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "NUICoreWidgets.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

namespace AestraUI {

// Forward declarations
class NUIContextMenu;

/**
 * @brief Plugin list item for display in the browser
 */
struct PluginListItem {
    std::string id;              ///< Unique plugin ID
    std::string name;            ///< Plugin display name
    std::string vendor;          ///< Plugin vendor/manufacturer
    std::string version;         ///< Version string
    std::string category;        ///< Category string (Effect, Instrument, etc.)
    std::string formatStr;       ///< "VST3" or "CLAP"
    std::string typeName;        ///< "Effect", "Instrument", "Analyzer"
    bool isFavorite = false;     ///< User has marked as favorite
};

/**
 * @brief Filter options for the plugin browser
 */
enum class PluginFilterType {
    All,         ///< Show all plugins
    Effects,     ///< Show effects only
    Instruments, ///< Show instruments only
    VST3,        ///< Show VST3 only
    CLAP,        ///< Show CLAP only
    Favorites    ///< Show favorites only
};

/**
 * @brief Comprehensive plugin browser panel
 * 
 * Features:
 * - Category tabs (All, Effects, Instruments, VST3, CLAP)
 * - Search bar with real-time filtering
 * - Scrollable plugin list
 * - Plugin info display on hover
 * - Double-click to load callback
 * - Favorites system
 * 
 * Usage:
 * @code
 *   auto browser = std::make_shared<PluginBrowserPanel>();
 *   browser->setPluginList(plugins);
 *   browser->setOnPluginSelected([](const PluginListItem& p) {
 *       // Load the plugin
 *   });
 * @endcode
 */
class PluginBrowserPanel : public NUIComponent {
public:
    PluginBrowserPanel();
    ~PluginBrowserPanel() override = default;
    
    // ==============================
    // NUIComponent Overrides
    // ==============================
    
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;
    void onUpdate(double deltaTime) override;
    
    // ==============================
    // Plugin Data
    // ==============================
    
    /**
     * @brief Set the list of available plugins
     */
    void setPluginList(const std::vector<PluginListItem>& plugins);
    
    /**
     * @brief Get current plugin list (may be filtered)
     */
    const std::vector<PluginListItem>& getPluginList() const { return m_filteredPlugins; }
    
    /**
     * @brief Get all plugins (unfiltered)
     */
    const std::vector<PluginListItem>& getAllPlugins() const { return m_allPlugins; }
    
    // ==============================
    // Filtering & Search
    // ==============================
    
    /**
     * @brief Set filter type
     */
    void setFilter(PluginFilterType filter);
    PluginFilterType getFilter() const { return m_filterType; }
    
    /**
     * @brief Set search query
     */
    void setSearchQuery(const std::string& query);
    const std::string& getSearchQuery() const { return m_searchQuery; }
    
    // ==============================
    // Selection
    // ==============================
    
    /**
     * @brief Get currently selected plugin (if any)
     */
    const PluginListItem* getSelectedPlugin() const;
    
    /**
     * @brief Select plugin by ID
     */
    void selectPlugin(const std::string& id);
    
    /**
     * @brief Clear selection
     */
    void clearSelection();
    
    // ==============================
    // Favorites
    // ==============================
    
    /**
     * @brief Toggle favorite status for a plugin
     */
    void toggleFavorite(const std::string& pluginId);
    
    /**
     * @brief Get favorite plugin IDs
     */
    const std::vector<std::string>& getFavorites() const { return m_favorites; }
    
    /**
     * @brief Set favorite plugin IDs (for persistence)
     */
    void setFavorites(const std::vector<std::string>& favorites);
    
    // ==============================
    // Callbacks
    // ==============================
    
    /**
     * @brief Set callback for plugin selection (single click)
     */
    void setOnPluginSelected(std::function<void(const PluginListItem&)> callback);
    
    /**
     * @brief Set callback for plugin load (double click)
     */
    void setOnPluginLoadRequested(std::function<void(const PluginListItem&)> callback);
    
    /**
     * @brief Set callback for scan request
     */
    void setOnScanRequested(std::function<void()> callback);
    
    // ==============================
    // Scanning State
    // ==============================
    
    /**
     * @brief Set scanning state (shows progress indicator)
     */
    void setScanning(bool scanning, float progress = 0.0f);
    bool isScanning() const { return m_scanning; }
    
    /**
     * @brief Set scanning status text
     */
    void setScanStatus(const std::string& status);

private:
    // Internal methods
    void applyFilters();
    void renderHeader(NUIRenderer& renderer);
    void renderTabs(NUIRenderer& renderer);
    void renderSearchBar(NUIRenderer& renderer);
    void renderPluginList(NUIRenderer& renderer);
    void renderPluginRow(NUIRenderer& renderer, const PluginListItem& plugin, 
                         int index, float yOffset);
    void renderScanProgress(NUIRenderer& renderer);
    int hitTestRow(int y) const;
    
    // Data
    std::vector<PluginListItem> m_allPlugins;
    std::vector<PluginListItem> m_filteredPlugins;
    std::vector<std::string> m_favorites;
    
    // Search & Filter
    std::string m_searchQuery;
    PluginFilterType m_filterType = PluginFilterType::All;
    
    // UI State
    int m_selectedIndex = -1;
    int m_hoveredIndex = -1;
    float m_scrollOffset = 0.0f;
    float m_targetScrollOffset = 0.0f;
    
    // Scanning
    bool m_scanning = false;
    float m_scanProgress = 0.0f;
    std::string m_scanStatus;
    
    mutable std::recursive_mutex m_uiMutex; // Safe UI updates from background threads (recursive for callback calls)
    
    // Tab state
    int m_activeTab = 0;
    
    // Callbacks
    std::function<void(const PluginListItem&)> m_onPluginSelected;
    std::function<void(const PluginListItem&)> m_onPluginLoadRequested;
    std::function<void()> m_onScanRequested;
    
    // Double-click detection
    double m_lastClickTime = 0.0;
    int m_lastClickIndex = -1;
    
    // Drag initiation
    bool m_isPressed = false;
    int m_pressedIndex = -1;
    NUIPoint m_dragStartPos;
    
    // Layout constants
    static constexpr float ROW_HEIGHT = 32.0f;
    static constexpr float HEADER_HEIGHT = 40.0f;
    static constexpr float TAB_HEIGHT = 36.0f;
    static constexpr float SEARCH_HEIGHT = 36.0f;
};

/**
 * @brief Effect chain rack UI for mixer channel inserts
 * 
 * Displays up to 10 effect slots with:
 * - Drag-and-drop reordering
 * - Bypass toggles per slot
 * - Click to open plugin editor
 * - Slot context menu
 */
class EffectChainRack : public NUIComponent {
public:
    static constexpr int MAX_SLOTS = 10;
    
    struct EffectSlotInfo {
        std::string name;       ///< Plugin name or "Empty"
        bool bypassed = false;  ///< Slot bypass state
        bool isEmpty = true;    ///< No plugin loaded
        float dryWet = 1.0f;    ///< Dry/Wet mix (0.0 - 1.0), default 1.0 (Wet)
        bool pendingRemoval = false; ///< UI is waiting for engine to confirm removal
    };
    
    EffectChainRack();
    ~EffectChainRack() override = default;
    
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    
    /**
     * @brief Update slot info
     */
    void setSlot(int index, const EffectSlotInfo& info);
    const EffectSlotInfo& getSlot(int index) const;
    
    /**
     * @brief Callbacks
     */
    void setOnSlotClicked(std::function<void(int slot)> callback);
    void setOnSlotBypassToggled(std::function<void(int slot, bool bypassed)> callback);
    void setOnSlotRemoveRequested(std::function<void(int slot)> callback);
    void setOnAddPluginRequested(std::function<void(int slot)> callback);
    void setOnSlotMixChanged(std::function<void(int slot, float mix)> callback);
    void setOnSlotMoveRequested(std::function<void(int from, int to)> callback);
    
    /**
     * @brief Get current scroll offset
     */
    float getScrollOffset() const { return m_scrollOffset; }
    
private:
    void renderSlot(NUIRenderer& renderer, int index, float slotY);
    int hitTestSlot(float y) const;
    
    std::array<EffectSlotInfo, MAX_SLOTS> m_slots;
    std::array<int, MAX_SLOTS> m_bypassOverride; // -1=None, 0=Active, 1=Bypassed
    int m_hoveredSlot = -1;
    float m_scrollOffset = 0.0f;
    
    // Double-click detection
    std::chrono::steady_clock::time_point m_lastClickTime{};
    int m_lastClickSlot = -1;
    
    // Drag Interaction State
    int m_activeKnobSlot = -1;
    NUIPoint m_dragStartPos{};
    float m_dragStartValue = 0.0f;
    
    std::function<void(int)> m_onSlotClicked;
    std::function<void(int, bool)> m_onSlotBypassToggled;
    std::function<void(int)> m_onSlotRemoveRequested;
    std::function<void(int)> m_onAddPluginRequested;
    std::function<void(int, float)> m_onSlotMixChanged;
    std::function<void(int, int)> m_onSlotMoveRequested; // from, to
    
    // Drag Reorder State
    int m_draggingSlotIndex = -1;
    bool m_isDraggingReorder = false;
    NUIPoint m_currentMousePos{};
    
    // Context Menu State
    std::shared_ptr<NUIContextMenu> m_contextMenu;
    int m_contextMenuSlot = -1;
    
    static constexpr float SLOT_HEIGHT = 28.0f;
};

} // namespace AestraUI
