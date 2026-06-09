// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUISlider.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <memory>
#include <string>
#include <vector>

namespace AestraUI {

class AestraCompEditor : public AestraPanelWindow {
public:
    explicit AestraCompEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);

    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void onResize(int width, int height) override;
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    struct KnobControl {
        std::shared_ptr<NUISlider> slider;
        std::string label;
        uint32_t paramId = 0;
        NUIRect bounds;
        bool isPrimary = false;
    };

    void buildControls();
    void layoutControls();
    void syncControlsFromPlugin();
    void drawTransferCurve(NUIRenderer& renderer, NUIColor accent);
    void drawMeters(NUIRenderer& renderer, NUIColor accent);
    void drawControl(NUIRenderer& renderer, const KnobControl& control, NUIColor accent);
    void drawUtilityButtons(NUIRenderer& renderer, NUIColor accent);
    void drawSectionLabel(NUIRenderer& renderer, const char* label, float y, float leftX, float rightX);
    void setBypassed(bool bypassed);
    bool isBypassed() const;
    std::string valueText(uint32_t paramId) const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<KnobControl> m_controls;

    NUIRect m_transferCurveRect;
    NUIRect m_metersRect;
    NUIRect m_bypassRect;
    NUIRect m_autoRect;
    NUIRect m_linkRect;
    NUIRect m_mixLockRect;
    NUIRect m_resetRect;
    NUIRect m_modePills[3];

    bool m_bypassHovered = false;
    bool m_autoHovered = false;
    bool m_linkHovered = false;
    bool m_mixLockHovered = false;
    bool m_resetHovered = false;
    bool m_autoEnabled = false;
    bool m_linkEnabled = false;
    bool m_mixLocked = false;

    float m_grDisplayDb = 0.0f;
    float m_inputDisplay = 0.0f;
    float m_outputDisplay = 0.0f;
    double m_meterTimer = 0.0;

    // Spectrum analyzer display data (smoothed from FFT)
    static constexpr uint32_t kDisplayBins = 1025;
    float m_inputSpectrum[kDisplayBins]{};
    float m_outputSpectrum[kDisplayBins]{};

    // Pre-allocated drawing buffers (avoid per-frame heap alloc)
    static constexpr int kMaxCurvePixels = 600;
    NUIPoint m_spectrumInputPts[kMaxCurvePixels + 2]{};
    NUIPoint m_spectrumOutputPts[kMaxCurvePixels + 2]{};
    NUIPoint m_spectrumBaseline[kMaxCurvePixels + 2]{};
    NUIPoint m_curvePts[130]{};
    int m_numSpectrumPts = 0;

    // Log-frequency LUT for spectrum display
    static constexpr int kLogFreqLutSize = 600;
    float m_logFreqLut[kLogFreqLutSize]{};

    static constexpr float kWinW = 680.0f;
    static constexpr float kWinH = 555.0f;
    static constexpr float kPad = 14.0f;
    static constexpr float kKnobSizePrimary = 58.0f;
    static constexpr float kKnobSizeSecondary = 40.0f;

    static constexpr float kStatsW = 100.0f;
    static constexpr float kModesW = 80.0f;
    static constexpr float kDividerW = 1.0f;
    static constexpr float kRightMargin = 12.0f;
    static constexpr float kRightZoneW = kStatsW + kModesW + kDividerW * 2.0f + kRightMargin; // 194.0f
    static constexpr float kPillW = 52.0f;
    static constexpr float kPillH = 20.0f;
    static constexpr float kPillGap = 4.0f;
};

} // namespace AestraUI
