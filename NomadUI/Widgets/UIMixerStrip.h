// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUITypes.h"

#include "UIMixerFader.h"
#include "UIMixerHeader.h"
#include "UIMixerKnob.h"
#include "UIMixerMeter.h"
#include "UIMixerButtonRow.h"
#include "UIMixerFXSummary.h"
#include "UIMixerFooter.h"

#include <memory>
#include <string>

namespace Nomad {
class MixerViewModel;
namespace Audio {
class MeterSnapshotBuffer;
class ContinuousParamBuffer;
} // namespace Audio
} // namespace Nomad

namespace NomadUI {

/**
 * @brief Minimal mixer channel strip: header + meter + fader.
 *
 * Stores only channelId and looks up ChannelViewModel each frame via MixerViewModel.
 */
class UIMixerStrip : public NUIComponent {
public:
    UIMixerStrip(uint32_t channelId,
                 int trackNumber,
                 Nomad::MixerViewModel* viewModel,
                 std::shared_ptr<Nomad::Audio::MeterSnapshotBuffer> meterSnapshots,
                 std::shared_ptr<Nomad::Audio::ContinuousParamBuffer> continuousParams);

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    // Request opening the inspector on the Inserts tab for this channel.
    std::function<void(uint32_t channelId)> onFXClicked;

private:
    uint32_t m_channelId{0};
    int m_trackNumber{0};
    Nomad::MixerViewModel* m_viewModel{nullptr};
    std::shared_ptr<Nomad::Audio::MeterSnapshotBuffer> m_meterSnapshots;
    std::shared_ptr<Nomad::Audio::ContinuousParamBuffer> m_continuousParams;

    std::shared_ptr<UIMixerHeader> m_header;
    std::shared_ptr<UIMixerKnob> m_trimKnob;
    std::shared_ptr<UIMixerFXSummary> m_fxSummary;
    std::shared_ptr<UIMixerKnob> m_panKnob;
    std::shared_ptr<UIMixerButtonRow> m_buttons;
    std::shared_ptr<UIMixerMeter> m_meter;
    std::shared_ptr<UIMixerFader> m_fader;
    std::shared_ptr<UIMixerFooter> m_footer;

    // Cached theme colors
    NUIColor m_selectedTint;
    NUIColor m_selectedOutline;
    NUIColor m_selectedGlow;
    NUIColor m_selectedTopHighlight;
    NUIColor m_masterBackground;
    NUIColor m_mutedOverlay;

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
    bool m_cachedStripHovered{false};
    bool m_cachedShowChannelControls{true};
    bool m_cachedFaderHovered{false};
    bool m_cachedTrimHovered{false};
    bool m_cachedPanHovered{false};
    float m_cachedFaderDb{0.0f};
    float m_cachedTrimDb{0.0f};
    float m_cachedPan{0.0f};
    int m_cachedFxCount{0};

    void cacheThemeColors();
    void layoutChildren();
    void invalidateStaticCache();
    void renderStaticLayer(NUIRenderer& renderer);
};

} // namespace NomadUI
