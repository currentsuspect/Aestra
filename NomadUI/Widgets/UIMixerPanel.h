// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUITypes.h"
#include "UIMixerMeter.h"
#include <memory>
#include <vector>
#include <functional>

// Forward declarations
namespace Nomad {
    class MixerViewModel;
    namespace Audio {
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
                 std::shared_ptr<Nomad::Audio::MeterSnapshotBuffer> meterSnapshots);

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

    /// Meter widgets for each channel
    std::vector<std::shared_ptr<UIMixerMeter>> m_meters;

    /// Master meter widget
    std::shared_ptr<UIMixerMeter> m_masterMeter;

    // Layout constants (from design spec)
    static constexpr float STRIP_WIDTH = 104.0f;
    static constexpr float STRIP_SPACING = 2.0f;
    static constexpr float METER_WIDTH = 26.0f;  // 12px per bar + 2px gap
    static constexpr float HEADER_HEIGHT = 28.0f;
    static constexpr float FOOTER_HEIGHT = 20.0f;
    static constexpr float PADDING = 8.0f;
    static constexpr float MASTER_STRIP_WIDTH = 140.0f;

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
     * @brief Render channel labels above meters.
     */
    void renderLabels(NUIRenderer& renderer);

    /**
     * @brief Cache theme colors.
     */
    void cacheThemeColors();

    /**
     * @brief Update meter widgets with smoothed values from view model.
     */
    void updateMeterWidgets();
};

} // namespace NomadUI
