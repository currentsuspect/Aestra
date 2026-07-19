// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIDropdown.h"
#include "NUISegmentedControl.h"
#include "UIRoutingMap.h"

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
class NUIPlatformBridge;

class UIMixerInspector : public NUIComponent {
public:
    /** Forward the platform bridge to the insert rack (dry/wet knob capture). */
    void setPlatformBridge(NUIPlatformBridge* bridge);

    enum class Tab { Inserts = 0, Sends = 1, IO = 2 };

    explicit UIMixerInspector(Aestra::MixerViewModel* viewModel);

    void onRender(NUIRenderer& renderer) override;
    void onThemeChanged(const NUIThemeProperties& theme) override { cacheThemeColors(); NUIComponent::onThemeChanged(theme); }
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setViewModel(Aestra::MixerViewModel* viewModel) { m_viewModel = viewModel; }
    void setActiveTab(Tab tab);
    Tab getActiveTab() const { return m_activeTab; }

    std::shared_ptr<EffectChainRack> getEffectRack() const { return m_effectRack; }
    std::shared_ptr<UIRoutingMap> getRoutingMap() const { return m_routingMap; }

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

    // Routing map minimap
    std::shared_ptr<UIRoutingMap> m_routingMap;

    // Cached header strings (updated only when selection changes)
    uint32_t m_cachedSelectedId{0xFFFFFFFFu};
    std::string m_cachedName;
    std::string m_cachedRoute;
    std::string m_cachedHeaderTitle;
    std::string m_cachedHeaderSubtitle;
    int m_cachedTrackNumber{0};
    uint32_t m_cachedMainOutputId{0xFFFFFFFFu};
    bool m_cachedMasterSendEnabled{true};
    size_t m_cachedSendsCount{0};
    size_t m_cachedInsertsCount{0};
    int m_cachedFxCount{0};

    std::vector<std::function<void()>> m_deferredActions; // Added m_deferredActions
    float m_scrollOffset{0.0f};
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
