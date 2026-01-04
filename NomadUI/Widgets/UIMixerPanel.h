// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUITypes.h"
#include "UIMixerMeter.h"
#include "UIMixerStrip.h"
#include "UIMixerInspector.h"
#include <memory>
#include <vector>
#include <functional>

// Forward declarations
namespace Nomad {
    class MixerViewModel;
    namespace Audio {
        class ContinuousParamBuffer;
        class MeterSnapshotBuffer;
    }
}

namespace NomadUI {

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
    UIMixerPanel(std::shared_ptr<Nomad::MixerViewModel> viewModel,
                 std::shared_ptr<Nomad::Audio::MeterSnapshotBuffer> meterSnapshots,
                 std::shared_ptr<Nomad::Audio::ContinuousParamBuffer> continuousParams);

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
    Nomad::MixerViewModel* getViewModel() { return m_viewModel.get(); }

private:
    std::shared_ptr<Nomad::MixerViewModel> m_viewModel;
    std::shared_ptr<Nomad::Audio::MeterSnapshotBuffer> m_meterSnapshots;
    std::shared_ptr<Nomad::Audio::ContinuousParamBuffer> m_continuousParams;

    /// Channel strips (header + meter + fader)
    std::vector<std::shared_ptr<UIMixerStrip>> m_strips;

    /// Master strip (pinned on the right)
    std::shared_ptr<UIMixerStrip> m_masterStrip;

    /// Inspector panel (pinned on the right, before master)
    std::shared_ptr<UIMixerInspector> m_inspector;

    // Horizontal scroll offset for channel strips (pixels).
    float m_scrollX{0.0f};

    // Layout constants (from design spec)
    static constexpr float STRIP_WIDTH = 104.0f;
    static constexpr float STRIP_SPACING = 2.0f;
    static constexpr float HEADER_HEIGHT = 28.0f;
    static constexpr float PADDING = 8.0f;
    static constexpr float MASTER_STRIP_WIDTH = 140.0f;
    static constexpr float INSPECTOR_WIDTH = 220.0f;

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

} // namespace NomadUI
