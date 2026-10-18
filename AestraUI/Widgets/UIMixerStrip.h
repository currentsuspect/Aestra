// ¶¸ 2025 Aestra Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"

#include "UIMixerFader.h"
#include "UIMixerHeader.h"
#include "MixerDensity.h"
#include "UIParamBar.h"
#include "UIMixerSendList.h"
#include "UIMixerMeter.h"
#include "UIMixerButtonRow.h"
#include "UIMixerInsertList.h"
#include "UIOutputChip.h"
#include "UIMixerFooter.h"

#include <memory>
#include <string>

namespace Aestra {
class MixerViewModel;
struct ChannelViewModel;
namespace Audio {
class MeterSnapshotBuffer;
class ContinuousParamBuffer;
} // namespace Audio
} // namespace Aestra

namespace AestraUI {

/**
 * @brief Minimal mixer channel strip: header + meter + fader.
 *
 * Stores only channelId and looks up ChannelViewModel each frame via MixerViewModel.
 */
class UIMixerStrip : public NUIComponent {
public:
    UIMixerStrip(uint32_t channelId,
                 int trackNumber,
                 Aestra::MixerViewModel* viewModel,
                 std::shared_ptr<Aestra::Audio::MeterSnapshotBuffer> meterSnapshots,
                 std::shared_ptr<Aestra::Audio::ContinuousParamBuffer> continuousParams);

    void onRender(NUIRenderer& renderer) override;
    void onThemeChanged(const NUIThemeProperties& theme) override { cacheThemeColors(); NUIComponent::onThemeChanged(theme); }
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    uint32_t getChannelId() const { return m_channelId; }
    /// How much this strip shows (Wide / Normal / Narrow); the panel sets it with the strip's width.
    void setDensity(MixerDensity density);
    MixerDensity density() const { return m_density; }
    /// What the strip currently shows, for tests and for the panel (it never mutates through these).
    const UIMixerInsertList* insertList() const { return m_insertList.get(); }
    const UIOutputChip* outputChip() const { return m_outputChip.get(); }
    const UIMixerSendList* sendList() const { return m_sendList.get(); }
    const UIParamBar* panBar() const { return m_panBar.get(); }
    /// The meters' own bars (below the clip lamp): the height the shared dB scale is drawn against.
    NUIRect meterBarsRect() const;

    // Platform bridge propagation for cursor warp
    void setPlatformBridge(NUIPlatformBridge* bridge);

    /// Bounds of the insert list (the Add Insert target) in strip-local space.
    NUIRect getFXSummaryBounds() const;

    /// Close this strip's inline fader entry when a press lands outside it.
    void dismissFaderEdit(const NUIPoint& position);

    // Request opening the inspector on the Inserts tab for this channel.
    std::function<void(uint32_t channelId)> onFXClicked;
    /// The output chip was clicked: the owner opens the output picker for this channel.
    std::function<void(uint32_t channelId)> onOutputClicked;

    // Callback fired when fader value changes — for undo/redo wiring
    std::function<void(float newDb)> onFaderChanged;

    // Callbacks for mute/solo/pan undo/redo wiring
    std::function<void(bool muted)> onMuteChanged;
    std::function<void(bool soloed)> onSoloChanged;
    std::function<void(float pan)> onPanChanged;
    std::function<void(bool monitored)> onMonitorToggled;

private:
    uint32_t m_channelId{0};
    MixerDensity m_density{MixerDensity::Normal};
    int m_trackNumber{0};
    Aestra::MixerViewModel* m_viewModel{nullptr};
    std::shared_ptr<Aestra::Audio::MeterSnapshotBuffer> m_meterSnapshots;
    std::shared_ptr<Aestra::Audio::ContinuousParamBuffer> m_continuousParams;

    std::shared_ptr<UIMixerHeader> m_header;
    std::shared_ptr<UIMixerInsertList> m_insertList;
    std::shared_ptr<UIOutputChip> m_outputChip;
    std::shared_ptr<UIParamBar> m_panBar;
    std::shared_ptr<UIMixerSendList> m_sendList;
    std::shared_ptr<UIMixerButtonRow> m_buttons;
    std::shared_ptr<UIMixerMeter> m_meter;
    std::shared_ptr<UIMixerFader> m_fader;
    std::shared_ptr<UIMixerFooter> m_footer;

    // Cached theme colors
    NUIColor m_selectedTint;
    NUIColor m_selectedOutline;
    NUIColor m_selectedTopHighlight;
    NUIColor m_masterBackground;
    NUIColor m_mutedOverlay;
    NUIColor m_stripBg;
    NUIColor m_masterBorder;

    // Static-layer cache (header + fader). The meter stays live.
    uint64_t m_staticCacheId{0};
    bool m_staticCacheInvalidated{true};
    std::string m_cachedName;
    std::string m_cachedRoute;
    uint32_t m_cachedTrackColorArgb{0};
    bool m_cachedSelected{false};
    bool m_cachedMuted{false};
    bool m_cachedSoloed{false};
    bool m_cachedArmed{false};
    bool m_cachedMonitored{false};
    bool m_cachedStripHovered{false};
    bool m_cachedFaderHovered{false};
    float m_cachedFaderDb{0.0f};
    float m_cachedPan{0.0f};
    std::string m_cachedSendSig;
    std::string m_cachedTag;
    int m_cachedFxCount{0};
    std::string m_cachedInsertSig;
    std::string m_cachedOutputLabel;
    int m_cachedOutputColorIndex{-2};
    std::string m_cachedFxStatus;

    /// Master-only readouts, computed in layoutChildren. Split so each number
    /// sits under the column that owns it: observed signal vs applied gain.
    NUIRect m_panValueRect{};
    NUIRect m_masterMeterReadoutRect{};
    NUIRect m_masterGainReadoutRect{};

    void cacheThemeColors();
    void layoutChildren();
    void invalidateStaticCache();
    void renderStaticLayer(NUIRenderer& renderer);
    void renderPanValue(NUIRenderer& renderer, const Aestra::ChannelViewModel& channel);
    void renderMasterReadout(NUIRenderer& renderer, const Aestra::ChannelViewModel& channel);
};

} // namespace AestraUI
