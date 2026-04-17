// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "UIMixerMeter.h"
#include "UIMixerStrip.h"
#include "UIMixerInspector.h"
#include <memory>
#include <vector>
#include <functional>

// Forward declarations
namespace Aestra {
    class MixerViewModel;
    namespace Audio {
        class ContinuousParamBuffer;
        class MeterSnapshotBuffer;
        class TrackManager;
    }
}

namespace AestraUI {

/**
 * @brief Main mixer panel container with channel meters.
 *
 * This is a barebones implementation for Checkpoint 1 ("Meters Move").
 * Creates one UIMixerMeter per channel and lays them out horizontally.
 * Future checkpoints will add faders, knobs, and full channel strips.
 *
 * Requirements: 3.1 - Channel strips with fixed width (96-112px)
 */
class UIMixerPanel : public NUIComponent {
public:
    /**
     * @brief Construct a mixer panel.
     *
     * @param viewModel Shared view model for channel state
     * @param meterSnapshots Lock-free meter snapshot buffer for reading peaks
     */
    UIMixerPanel(std::shared_ptr<Aestra::MixerViewModel> viewModel,
                 std::shared_ptr<Aestra::Audio::TrackManager> trackManager);

    ~UIMixerPanel() override = default;

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    /**
     * @brief Refresh channel list from view model.
     *
     * Call when tracks are added/removed to rebuild meter widgets.
     */
    void refreshChannels();

    /**
     * @brief Get the view model.
     */
    Aestra::MixerViewModel* getViewModel() { return m_viewModel.get(); }

    /**
     * @brief Get the inspector.
     */
    UIMixerInspector* getInspector() { return m_inspector.get(); }

private:
    bool channelLayoutMatchesViewModel() const;
    NUIRect getMinimapRect() const;
    float getChannelViewportWidth() const;
    float getChannelContentWidth() const;
    float getChannelMaxScroll() const;
    void updateScrollFromMinimapX(float x);

    std::shared_ptr<Aestra::MixerViewModel> m_viewModel;
    std::shared_ptr<Aestra::Audio::TrackManager> m_trackManager;

    // Callback for fader undo/redo: channelId, newDb
    std::function<void(uint32_t, float)> m_onFaderChanged;

    /// Channel strips (header + meter + fader)
    std::vector<std::shared_ptr<UIMixerStrip>> m_strips;

    /// Master strip (pinned on the right)
    std::shared_ptr<UIMixerStrip> m_masterStrip;

    /// Inspector panel (pinned on the right, before master)
    std::shared_ptr<UIMixerInspector> m_inspector;

    // Horizontal scroll offset for channel strips (pixels).
    float m_scrollX{0.0f};
    float m_targetScrollX{0.0f};
    bool m_isDraggingMinimap{false};
    float m_minimapDragOffsetX{0.0f};

    // Layout constants (from design spec)
    static constexpr float STRIP_WIDTH = 110.0f;
    static constexpr float STRIP_SPACING = 6.0f;
    static constexpr float HEADER_HEIGHT = 28.0f;
    static constexpr float PADDING = 8.0f;
    static constexpr float MASTER_STRIP_WIDTH = 146.0f;
    static constexpr float INSPECTOR_WIDTH = 236.0f;
    static constexpr float MINIMAP_HEIGHT = 22.0f;
    static constexpr float MINIMAP_GAP = 6.0f;

    // Cached theme colors
    NUIColor m_backgroundColor;
    NUIColor m_separatorColor;

    /**
     * @brief Layout all meter widgets horizontally.
     */
    void layoutMeters();

    /**
     * @brief Render separator lines between strips.
     */
    void renderSeparators(NUIRenderer& renderer);

    /**
     * @brief Cache theme colors.
     */
    void cacheThemeColors();
};

} // namespace AestraUI
