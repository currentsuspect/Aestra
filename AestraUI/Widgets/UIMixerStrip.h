// ¶¸ 2025 Aestra Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"

#include "UIMixerFader.h"
#include "UIMixerHeader.h"
#include "UIMixerKnob.h"
#include "UIMixerMeter.h"
#include "UIMixerButtonRow.h"
#include "UIMixerFXSummary.h"
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

    // Platform bridge propagation for cursor warp
    void setPlatformBridge(NUIPlatformBridge* bridge);

    /// Bounds of the FX summary (Add Insert) button in strip-local space.
    NUIRect getFXSummaryBounds() const;

    // Request opening the inspector on the Inserts tab for this channel.
    std::function<void(uint32_t channelId)> onFXClicked;

    // Callback fired when fader value changes — for undo/redo wiring
    std::function<void(float newDb)> onFaderChanged;

    // Callbacks for mute/solo/pan undo/redo wiring
    std::function<void(bool muted)> onMuteChanged;
    std::function<void(bool soloed)> onSoloChanged;
    std::function<void(float pan)> onPanChanged;

private:
    uint32_t m_channelId{0};
    int m_trackNumber{0};
    Aestra::MixerViewModel* m_viewModel{nullptr};
    std::shared_ptr<Aestra::Audio::MeterSnapshotBuffer> m_meterSnapshots;
    std::shared_ptr<Aestra::Audio::ContinuousParamBuffer> m_continuousParams;

    std::shared_ptr<UIMixerHeader> m_header;
    std::shared_ptr<UIMixerKnob> m_trimKnob;
    std::shared_ptr<UIMixerFXSummary> m_fxSummary;
    std::shared_ptr<UIMixerKnob> m_panKnob;
    std::shared_ptr<UIMixerKnob> m_widthKnob; 
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
    bool m_cachedTrimHovered{false};
    bool m_cachedPanHovered{false};
    bool m_cachedWidthHovered{false}; // New!
    float m_cachedFaderDb{0.0f};
    float m_cachedTrimDb{0.0f};
    float m_cachedPan{0.0f};
    float m_cachedWidth{1.0f}; // New!
    int m_cachedFxCount{0};
    std::string m_cachedFxStatus;

    /// Master-only: labelled Peak / LUFS / Gain block, computed in layoutChildren.
    NUIRect m_masterReadoutRect{};

    void cacheThemeColors();
    void layoutChildren();
    void invalidateStaticCache();
    void renderStaticLayer(NUIRenderer& renderer);
    void renderMasterReadout(NUIRenderer& renderer, const Aestra::ChannelViewModel& channel);
};

} // namespace AestraUI
