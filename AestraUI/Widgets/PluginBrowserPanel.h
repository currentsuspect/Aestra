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
#include <unordered_set>
#include <fstream>

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
    std::string formatStr;       ///< "VST3", "CLAP (Exp.)", or "Int"
    std::string typeName;        ///< "Effect", "Instrument", "Analyzer"
    bool isFavorite = false;     ///< User has marked as favorite
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
 * - Favorites system with persistence
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
    void onFocusLost() override;
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

    enum class PluginTypeFilter { All, Effects, Instruments };
    enum class PluginFormatFilter { All, VST3, CLAP };

    /**
     * @brief Set type filter (All, Effects, Instruments)
     */
    void setTypeFilter(PluginTypeFilter filter);
    PluginTypeFilter getTypeFilter() const { return m_typeFilter; }

    /**
     * @brief Set format filter (All, VST3, CLAP)
     */
    void setFormatFilter(PluginFormatFilter filter);
    PluginFormatFilter getFormatFilter() const { return m_formatFilter; }

    /**
     * @brief Toggle favorites-only filter
     */
    void setShowFavoritesOnly(bool show);
    bool getShowFavoritesOnly() const { return m_showFavoritesOnly; }

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
    std::vector<std::string> getFavorites() const;

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
    void renderHeaderBar(NUIRenderer& renderer);
    void renderFilterBar(NUIRenderer& renderer);
    void renderPluginList(NUIRenderer& renderer);
    void renderPluginRow(NUIRenderer& renderer, const PluginListItem& plugin,
                         int index, float yOffset);
    void renderScanProgress(NUIRenderer& renderer);
    int hitTestRow(int y) const;
    NUIRect getScanButtonRect() const;

    // Favorites persistence
    void loadFavorites();
    void saveFavorites();

    // Filter pill hit testing
    struct FilterPillHit {
        enum Type { None, TypeAll, TypeFX, TypeInst, FormatVST3, FormatCLAP, Fav } type;
        NUIRect bounds;
    };
    std::vector<FilterPillHit> m_filterPillHits;
    int m_hoveredPillIndex = -1;

    // Data
    std::vector<PluginListItem> m_allPlugins;
    std::vector<PluginListItem> m_filteredPlugins;
    std::unordered_set<std::string> m_favoritesSet; // O(1) lookup, persisted to disk

    // Search & Filter
    std::string m_searchQuery;
    PluginTypeFilter m_typeFilter = PluginTypeFilter::All;
    PluginFormatFilter m_formatFilter = PluginFormatFilter::All;
    bool m_showFavoritesOnly = false;

    // UI State
    int m_selectedIndex = -1;
    int m_hoveredIndex = -1;
    int m_hoveredRow = -1;
    bool m_searchFocused = false;
    float m_scrollOffset = 0.0f;
    float m_targetScrollOffset = 0.0f;

    // Star hit rects (parallel to m_filteredPlugins, rebuilt each render)
    std::vector<NUIRect> m_starRects;

    // Scanning
    bool m_scanning = false;
    float m_scanProgress = 0.0f;
    std::string m_scanStatus;

    mutable std::recursive_mutex m_uiMutex; // Safe UI updates from background threads (recursive for callback calls)

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
    static constexpr float ROW_HEIGHT = 38.0f;
    static constexpr float HEADER_BAR_HEIGHT = 38.0f;
    static constexpr float FILTER_BAR_HEIGHT = 52.0f;
    static constexpr float SEARCH_HEIGHT = 0.0f;
    // The panel bounds cover the file browser from just below its search bar; this
    // pushes the panel's own header/filters/list down so they sit clear of the
    // search bar while the panel background still covers the file list behind it.
    static constexpr float CONTENT_TOP_PAD = 4.0f;
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
class NUIPlatformBridge;

class EffectChainRack : public NUIComponent {
public:
    static constexpr int MAX_SLOTS = 10;

    struct EffectSlotInfo {
        std::string name;       ///< Plugin name or "Empty"
        bool bypassed = false;  ///< Slot bypass state
        bool isEmpty = true;    ///< No plugin loaded
        float dryWet = 1.0f;    ///< Dry/Wet mix (0.0 - 1.0), default 1.0 (Wet)
        bool pendingRemoval = false; ///< UI is waiting for engine to confirm removal
        bool nonFiniteOutputFault = false; ///< Auto-bypassed after unsafe plugin output
    };

    EffectChainRack();
    ~EffectChainRack() override; // cancels an active knob capture (see .cpp)

    /** Platform bridge for dry/wet knob cursor capture (may be null in tests). */
    void setPlatformBridge(NUIPlatformBridge* bridge) { m_platformBridge = bridge; }

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
     * @brief Get the screen-space bounds of a given slot (accounts for scroll).
     */
    NUIRect getSlotBounds(int index) const;

    /**
     * @brief Get current scroll offset
     */
    float getScrollOffset() const { return m_scrollOffset; }

private:
    void renderSlot(NUIRenderer& renderer, int index, float slotY);
    int hitTestSlot(float y) const;
    NUIRect slotRectForTop(float slotY) const;

    /// Index of the last slot holding a plugin, or -1 when the rack is empty.
    int lastPopulatedSlot() const;

    /**
     * @brief Rows the rack actually shows: every populated slot plus one
     *        "+ Add insert" row.
     *
     * Drawing all MAX_SLOTS produced a column of identical empty outlines that
     * carried no information. The spare slots are revealed only while a reorder
     * drag is in flight, when numbered drop targets are genuinely useful.
     */
    int visibleSlotCount() const;

    /// Keep m_scrollOffset within the rows actually drawn. The visible row set
    /// shrinks when a plugin is removed or a reorder drag ends, and a stale
    /// offset leaves the rack looking empty.
    void clampScrollToContent();

    NUIPlatformBridge* m_platformBridge = nullptr;
    std::array<EffectSlotInfo, MAX_SLOTS> m_slots;
    std::array<int, MAX_SLOTS> m_bypassOverride; // -1=None, 0=Active, 1=Bypassed
    int m_hoveredSlot = -1;
    float m_scrollOffset = 0.0f;

    // Double-click detection
    std::chrono::steady_clock::time_point m_lastClickTime{};
    int m_lastClickSlot = -1;

    // Drag Interaction State
    int m_activeKnobSlot = -1;
    NUIPoint m_dragStartPos{}; // slot-reorder drag origin (knob drag uses service deltas)

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

    static constexpr float SLOT_HEIGHT = 34.0f;
};

} // namespace AestraUI
