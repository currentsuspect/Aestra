// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIDropdown.h"
#include "NUISegmentedControl.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
class MixerViewModel;
struct ChannelViewModel;
}

namespace AestraUI {
class EffectChainRack;

/**
 * @brief Right-side inspector panel for the selected mixer channel.
 *
 * Displays simple tabs (Inserts/Sends/IO). Inserts tab includes an "Add FX"
 * placeholder button.
 */
class UIMixerInspector : public NUIComponent {
public:
    enum class Tab { Inserts = 0, Sends = 1, IO = 2 };

    explicit UIMixerInspector(Aestra::MixerViewModel* viewModel);

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setViewModel(Aestra::MixerViewModel* viewModel) { m_viewModel = viewModel; }
    void setActiveTab(Tab tab);
    Tab getActiveTab() const { return m_activeTab; }

    std::shared_ptr<EffectChainRack> getEffectRack() const { return m_effectRack; }

private:
    Aestra::MixerViewModel* m_viewModel{nullptr};
    Tab m_activeTab{Tab::Inserts};

    // Cached theme colors
    NUIColor m_bg;
    NUIColor m_border;
    NUIColor m_text;
    NUIColor m_textSecondary;
    NUIColor m_tabBg;
    NUIColor m_tabActive;
    NUIColor m_tabHover;
    NUIColor m_addBg;
    NUIColor m_addHover;
    NUIColor m_addText;

    // Hit rectangles
    NUIRect m_tabRects[3]{};
    NUIRect m_addFxRect{};

    int m_hoveredTab{-1};
    bool m_addHovered{false};
    bool m_addPressed{false};

    // Inserts
    std::shared_ptr<EffectChainRack> m_effectRack;
    std::shared_ptr<NUISegmentedControl> m_tabControl;

    // I/O
    std::shared_ptr<NUIDropdown> m_ioInputDropdown;
    std::shared_ptr<NUIDropdown> m_mainOutputDropdown;
    std::vector<std::string> m_cachedInputNames;
    std::vector<int> m_cachedInputDeviceIds;
    std::vector<std::string> m_cachedOutputNames;
    std::vector<int> m_cachedOutputIds;

    // Sends
    std::vector<std::shared_ptr<class UIMixerSend>> m_sendWidgets;
    void rebuildSendWidgets(const Aestra::ChannelViewModel* channel);
    void rebuildInsertRack(const Aestra::ChannelViewModel* channel);

    // Cached header strings (updated only when selection changes)
    uint32_t m_cachedSelectedId{0xFFFFFFFFu};
    std::string m_cachedName;
    std::string m_cachedRoute;
    std::string m_cachedHeaderTitle;
    std::string m_cachedHeaderSubtitle;
    /**
 * Cached track number for the currently selected channel.
 *
 * Refreshed when the inspector's selection changes to avoid repeated lookups.
 */
int m_cachedTrackNumber{0};
    uint32_t m_cachedMainOutputId{0xFFFFFFFFu};
    bool m_cachedMasterSendEnabled{true};
    /**
 * Cached count of send slots for the currently selected mixer channel.
 *
 * This value mirrors the number of sends present on the selected channel and is used
 * to quickly detect changes in the channel's send configuration to avoid unnecessary UI rebuilds.
 */
size_t m_cachedSendsCount{0};

    std::vector<std::function<void()>> m_deferredActions; // Added m_deferredActions
    float m_scrollOffset{0.0f};
    /**
 * Populate cached color values used by the inspector (backgrounds, borders, text, tabs, and "Add FX" colors).
 */
 
/**
 * Compute and store hit-test rectangles for tabs and the "Add FX" area based on the current layout and size.
 */
 
/**
 * Determine which tab contains the given point.
 * @param p Point in inspector-local coordinates to test.
 * @returns The tab index (0 = Inserts, 1 = Sends, 2 = IO) if the point lies inside a tab, `-1` otherwise.
 */
 
/**
 * Look up the track number associated with a channel identifier.
 * @param channelId Channel identifier to query.
 * @returns The 1-based track number for the channel, or `-1` if the channel is not found.
 */
 
/**
 * Refresh cached header values (name, route, title, subtitle, track number and IO/send-related cached fields) from the provided channel.
 * @param channel Channel view model to read header information from; may be `nullptr` to clear cached values.
 */
 
/**
 * Constrain scroll-related offsets (`m_scrollOffset`, `m_targetScrollOffset`) to valid bounds using `m_maxScrollOffset`.
 */
float m_targetScrollOffset{0.0f};
    float m_maxScrollOffset{0.0f};

    void cacheThemeColors();
    void layoutHitRects();
    int hitTestTab(const NUIPoint& p) const;
    int findTrackNumber(uint32_t channelId) const;
    void updateHeaderCache(const Aestra::ChannelViewModel* channel);
    void clampScrollOffsets();
};

} // namespace AestraUI
