// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"

#include <cstdint>
#include <string>

namespace Nomad {
class MixerViewModel;
struct ChannelViewModel;
}

namespace NomadUI {

/**
 * @brief Right-side inspector panel for the selected mixer channel.
 *
 * Displays simple tabs (Inserts/Sends/IO). Inserts tab includes an "Add FX"
 * placeholder button.
 */
class UIMixerInspector : public NUIComponent {
public:
    enum class Tab { Inserts = 0, Sends = 1, IO = 2 };

    explicit UIMixerInspector(Nomad::MixerViewModel* viewModel);

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setViewModel(Nomad::MixerViewModel* viewModel) { m_viewModel = viewModel; }
    void setActiveTab(Tab tab);
    Tab getActiveTab() const { return m_activeTab; }

private:
    Nomad::MixerViewModel* m_viewModel{nullptr};
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

    // Sends
    std::vector<std::shared_ptr<class UIMixerSend>> m_sendWidgets;
    void rebuildSendWidgets(const Nomad::ChannelViewModel* channel);

    // Cached header strings (updated only when selection changes)
    uint32_t m_cachedSelectedId{0xFFFFFFFFu};
    std::string m_cachedName;
    std::string m_cachedRoute;
    std::string m_cachedHeaderTitle;
    std::string m_cachedHeaderSubtitle;
    int m_cachedTrackNumber{0};

    std::vector<std::function<void()>> m_deferredActions; // Added m_deferredActions

    void cacheThemeColors();
    void layoutHitRects();
    int hitTestTab(const NUIPoint& p) const;
    int findTrackNumber(uint32_t channelId) const;
    void updateHeaderCache(const Nomad::ChannelViewModel* channel);
};

} // namespace NomadUI
