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

class AestraLimitEditor : public AestraPanelWindow {
public:
    explicit AestraLimitEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);

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
    void drawGrMeter(NUIRenderer& renderer);
    void drawKnob(NUIRenderer& renderer, const KnobControl& control);
    void drawPill(NUIRenderer& renderer, NUIRect rect, bool selected, bool hovered,
                  const char* label, NUIColor accent);
    void drawMeterBar(NUIRenderer& renderer, NUIRect rect, float norm,
                      const char* label, const char* value, NUIColor color);
    std::string valueText(uint32_t paramId) const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<KnobControl> m_controls;

    NUIRect m_grBarRect;
    NUIRect m_grLabelRect;
    NUIRect m_ceilingKnobRect;
    NUIRect m_releaseKnobRect;
    NUIRect m_autoPillRect;
    NUIRect m_manualPillRect;
    NUIRect m_bypassPillRect;
    NUIRect m_inMeterRect;
    NUIRect m_outMeterRect;
    NUIRect m_grNumRect;

    bool m_bypassHovered = false;
    bool m_autoHovered = false;
    bool m_manualHovered = false;

    float m_grDisplayDb = 0.0f;
    float m_inputDisplay = 0.0f;
    float m_outputDisplay = 0.0f;
    double m_meterTimer = 0.0;

    static constexpr float kWinW = 520.0f;
    static constexpr float kWinH = 400.0f;
    static constexpr float kPad = 16.0f;
    static constexpr float kKnobSizePrimary = 64.0f;
    static constexpr float kKnobSizeSecondary = 48.0f;
    static constexpr float kPillW = 56.0f;
    static constexpr float kPillH = 22.0f;
};

} // namespace AestraUI
