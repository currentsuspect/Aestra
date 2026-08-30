// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Helpers/MixerPluginListPolicy.h"
#include "NUIComponent.h"
#include "NUITextInput.h"
#include "NUITypes.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace AestraUI {

/**
 * @brief Compact plugin finder dropdown anchored to an Add Insert button.
 *
 * Shows built-in plugins grouped by category with real-time search.
 * Fires onPluginSelected when a row is clicked.
 */
class UIMixerPluginDropdown : public NUIComponent {
public:
    UIMixerPluginDropdown();

    void onRender(NUIRenderer& renderer) override;
    void onThemeChanged(const NUIThemeProperties& theme) override { cacheThemeColors(); NUIComponent::onThemeChanged(theme); }
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;

    // Position the dropdown anchored to the trigger rect.
    // If flipIfOverflow is true and the dropdown would extend past panelBottomY,
    // it opens upward instead of downward.
    void showAt(const NUIRect& triggerRect, float panelBottomY);
    void hide();
    bool isOpen() const { return m_open; }

    // Callback: plugin ID, display name
    std::function<void(const std::string& pluginId, const std::string& pluginName)> onPluginSelected;
    // Callback: user clicked "Browse all plugins" — current search query is
    // passed so the host can pre-seed the full browser with the same terms
    // and the user lands on the right results, not a blank search.
    std::function<void(const std::string& searchQuery)> onBrowseAllRequested;
    // Callback: the host should re-publish the current plugin catalog
    // (used when the dropdown opens with an empty catalog because the
    // initial setup-time refresh ran before the async scan completed).
    std::function<void()> onRequestRefresh;
    // Callback: dropdown was dismissed
    std::function<void()> onDismissed;

    /// Replace the dropdown's catalog (app layer injects from the plugin
    /// scanner: internal registry + VST3/CLAP). Grouping, mixer-insert
    /// filtering and ordering are the policy's job (MixerPluginListPolicy.h).
    void setPluginEntries(std::vector<Aestra::Components::MixerPluginEntry> entries);

private:
    struct PluginItem {
        std::string id;
        std::string name;
        std::string typeLabel;
        std::string category;
        std::string iconName;
    };

    struct Category {
        std::string label;
        std::vector<PluginItem> items;
    };

    static constexpr float DROP_W = 240.0f;
    static constexpr float MAX_LIST_H = 280.0f;
    static constexpr float SEARCH_H = 38.0f;
    static constexpr float CAT_HEADER_H = 22.0f;
    static constexpr float ROW_H = 34.0f;
    static constexpr float FOOTER_H = 36.0f;
    static constexpr float PAD = 8.0f;
    static constexpr float RADIUS = 8.0f;

    bool m_open{false};
    bool m_openUpward{false};

    std::string m_searchQuery;
    int m_hoveredRow{-1};     // -1 = none, 0+ = flat index across all visible rows
    int m_hoveredFooter{-1};  // -1 = none, 0 = browse link
    std::vector<Category> m_categories;
    std::vector<Category> m_filtered;

    NUIColor m_bg;
    NUIColor m_bgSecondary;
    NUIColor m_border;
    NUIColor m_borderTertiary;
    NUIColor m_textPrimary;
    NUIColor m_textSecondary;
    NUIColor m_textTertiary;
    NUIColor m_accent;
    NUIColor m_rowHover;
    NUIColor m_searchBg;
    std::shared_ptr<NUITextInput> m_searchInput;

    void cacheThemeColors();
    void filter();
    void dismiss();

    // Flat row access for hit testing / rendering
    struct FlatRow { bool isCategory; int catIndex; int itemIndex; float y; float h; };
    std::vector<FlatRow> flatten(const std::vector<Category>& cats) const;
    int hitTestRow(const NUIPoint& p) const;
    bool hitTestFooter(const NUIPoint& p) const;
};

} // namespace AestraUI
