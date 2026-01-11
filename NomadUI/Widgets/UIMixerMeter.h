// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUITypes.h"
#include <functional>

namespace NomadUI {

/**
 * @brief Stereo level meter widget for the mixer UI.
 *
 * Displays two vertical bars (L/R) with green/yellow/red zones,
 * peak hold indicators, and clip latch indicators.
 *
 * Accepts dB values (already smoothed by MixerViewModel).
 * Uses quasi-logarithmic scale for better visual feedback.
 *
 * Requirements: 9.1, 9.2, 9.3, 9.4, 9.5, 15.3
 */
class UIMixerMeter : public NUIComponent {
public:
    UIMixerMeter();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    /**
     * @brief Set meter levels (in dB, already smoothed by ViewModel).
     * @param dbL Left channel level in dB
     * @param dbR Right channel level in dB
     */
    void setLevels(float dbL, float dbR);

    /**
     * @brief Set fast peak overlay (in dB).
     *
     * Drawn as a thin marker on top of the energy body to match perceived punch.
     */
    void setPeakOverlay(float peakDbL, float peakDbR);

    /**
     * @brief Set peak hold levels (in dB).
     * @param holdL Left channel peak hold in dB
     * @param holdR Right channel peak hold in dB
     */
    void setPeakHold(float holdL, float holdR);

    /**
     * @brief Set clip latch state.
     * @param clipL Left channel clip latch
     * @param clipR Right channel clip latch
     */
    void setClipLatch(bool clipL, bool clipR);

    /// Render meters in a muted/monochrome style (levels still update).
    void setDimmed(bool dimmed);

    /// Callback when clip indicator is clicked (to clear clip latch)
    std::function<void()> onClipCleared;

private:
    // Current meter state (in dB)
    float m_peakL{-90.0f};
    float m_peakR{-90.0f};
    float m_peakOverlayL{-90.0f};
    float m_peakOverlayR{-90.0f};
    float m_peakHoldL{-90.0f};
    float m_peakHoldR{-90.0f};
    bool m_clipL{false};
    bool m_clipR{false};
    bool m_dimmed{false};

    // Cached theme colors (avoid per-frame lookups)
    NUIColor m_colorGreen;
    NUIColor m_colorYellow;
    NUIColor m_colorRed;
    NUIColor m_colorGreenDim;
    NUIColor m_colorYellowDim;
    NUIColor m_colorRedDim;
    NUIColor m_colorBackground;
    NUIColor m_colorPeakHold;
    NUIColor m_colorPeakOverlay;
    NUIColor m_colorPeakOverlayDim;
    NUIColor m_colorClipOff;

    // Layout constants
    static constexpr float METER_GAP = 2.0f;      // Gap between L and R bars
    static constexpr float CLIP_HEIGHT = 6.0f;   // Height of clip indicator
    static constexpr float PEAK_OVERLAY_HEIGHT = 2.0f; // Height of fast peak overlay marker
    static constexpr float PEAK_HOLD_HEIGHT = 2.0f; // Height of peak hold line

    // dB thresholds for color zones
    static constexpr float DB_YELLOW_THRESHOLD = -12.0f;
    static constexpr float DB_RED_THRESHOLD = -3.0f;
    static constexpr float DB_MIN = -60.0f;
    static constexpr float DB_MAX = 0.0f;

    /**
     * @brief Convert dB to normalized value (0.0 to 1.0) for rendering.
     *
     * Uses segmented linear mapping for predictable visual behavior:
     * - -60 to -20 dB maps to 0.0 to 0.5
     * - -20 to 0 dB maps to 0.5 to 1.0
     */
    float dbToNormalized(float db) const;

    /**
     * @brief Render a single meter bar.
     * @param renderer The renderer
     * @param bounds The bounds for this bar
     * @param levelDb Current level in dB
     * @param peakHoldDb Peak hold level in dB
     * @param clip Whether clip latch is active
     */
    void renderMeterBar(NUIRenderer& renderer, const NUIRect& bounds,
                        float levelDb, float peakOverlayDb, float peakHoldDb, bool clip);

    /**
     * @brief Get the color for a given dB level.
     */
    NUIColor getColorForLevel(float db) const;

    /**
     * @brief Cache theme colors from the theme manager.
     */
    void cacheThemeColors();
};

} // namespace NomadUI
