// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIDropdown.h"
#include "NUISegmentedControl.h"
#include "UIParamBar.h"

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
    /// The inspector is one column now (V81-R2): this scrolls to the named section (Inserts, Sends, Output/Input).
    void setActiveTab(Tab tab);
    Tab getActiveTab() const { return m_activeTab; }

    /// Trim edited (dB): `commit` is false while dragging (apply live) and true once at the end (one undo step).
    std::function<void(uint32_t channelId, float db, bool commit)> onTrimEdited;
    /// Stereo width edited (1.0 = unchanged).
    std::function<void(uint32_t channelId, float width)> onWidthEdited;
    /// "Show in routing graph →": open the full routing graph focused on this channel.
    std::function<void(uint32_t channelId)> onShowRoutingGraph;

    std::shared_ptr<EffectChainRack> getEffectRack() const { return m_effectRack; }

    /// What the column currently lays out, for tests and for the panel.
    NUIRect showInGraphRect() const { return m_linkRect; }
    NUIRect addSendRect() const { return m_addFxRect; }
    NUIRect viewportRect() const { return m_viewportRect; }
    float scrollOffset() const { return m_scrollOffset; }
    float maxScrollOffset() const { return m_maxScrollOffset; }
    std::vector<std::string> fedByNames() const {
        std::vector<std::string> names;
        for (const auto& chip : m_fedChips) names.push_back(chip.name);
        return names;
    }
    bool trimBarVisible() const { return m_trimBar && m_trimBar->isVisible(); }

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
    NUIRect m_addSidechainRect{};

    int m_hoveredTab{-1};
    bool m_addHovered{false};
    bool m_addPressed{false};
    bool m_addSidechainHovered{false};
    bool m_addSidechainPressed{false};

    // Inserts
    std::shared_ptr<EffectChainRack> m_effectRack;

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

    // Channel block: the controls you rarely touch, moved off every strip (V81-R2, FD-28 point 6).
    std::shared_ptr<UIParamBar> m_trimBar;
    std::shared_ptr<UIParamBar> m_widthBar;


    // Cached header strings (updated only when selection changes)
    uint32_t m_cachedSelectedId{0xFFFFFFFFu};
    std::string m_cachedName;
    std::string m_cachedRoute;
    std::string m_cachedHeaderTitle;
    std::string m_cachedHeaderSubtitle;
    uint32_t m_cachedMainOutputId{0xFFFFFFFFu};
    bool m_cachedMasterSendEnabled{true};
    size_t m_cachedSendsCount{0};
    size_t m_cachedInsertsCount{0};
    int m_cachedFxCount{0};

    std::vector<std::function<void()>> m_deferredActions; // Added m_deferredActions
    float m_scrollOffset{0.0f};
    float m_targetScrollOffset{0.0f};
    float m_maxScrollOffset{0.0f};

    // One-column layout (V81-R2, FD-25): every section stacked, one scroll for the lot, header pinned.
    struct FedChip { NUIRect rect; std::string name; NUIColor color; };
    NUIRect m_headerRect{};
    NUIRect m_viewportRect{};      ///< where the scrolling sections show
    NUIRect m_outputHeadRect{};
    NUIRect m_outputRowRect{};     ///< the dropdown (or the master's fixed text)
    NUIRect m_linkRect{};          ///< "Show in routing graph →"
    NUIRect m_fedHeadRect{};
    std::vector<FedChip> m_fedChips;
    NUIRect m_insertsHeadRect{};
    NUIRect m_rackRect{};
    NUIRect m_sendsHeadRect{};
    NUIRect m_warningRect{};
    NUIRect m_inputHeadRect{};
    NUIRect m_inputRowRect{};
    NUIRect m_inputMetaRect{};
    NUIRect m_inputMeterRect{};
    float m_sectionOffset[3]{0.0f, 0.0f, 0.0f}; ///< column offsets of Inserts / Sends / Output (for scrolling to)
    bool m_linkHovered{false};
    void computeColumn(const Aestra::ChannelViewModel* channel);

    void cacheThemeColors();
    void layoutHitRects();
    void updateHeaderCache(const Aestra::ChannelViewModel* channel);
    void clampScrollOffsets();
};

} // namespace AestraUI
