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
        NUIRect cell;       ///< knob disc, label and value
        bool bipolar = false;
    };

    void buildControls();
    void layoutControls();
    void syncControlsFromPlugin();
    void drawTransferCurve(NUIRenderer& renderer);
    void drawMeters(NUIRenderer& renderer);
    void drawRightColumn(NUIRenderer& renderer);
    void drawKnobs(NUIRenderer& renderer);
    void setParam(uint32_t paramId, float value);
    void setBypassed(bool bypassed);
    bool isBypassed() const;
    float param(uint32_t paramId) const;
    bool hovering(const NUIRect& rect) const { return m_hasMouse && rect.contains(m_mouse); }
    int segmentAt(const NUIRect& rect, int count, const NUIPoint& p) const;
    float hpfFromX(float x) const;
    std::string valueText(uint32_t paramId) const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<KnobControl> m_controls;

    // Layout, in window coordinates; recomputed from the window's position every frame.
    NUIRect m_curveRect;
    NUIRect m_meterRects[3];   // In, GR, Out
    NUIRect m_modeRect;
    NUIRect m_osRect;
    NUIRect m_sidechainRect;
    NUIRect m_hpfRect;
    NUIRect m_bypassRect;
    NUIRect m_resetRect;
    NUIPoint m_origin;         // top-left of the content area

    NUIPoint m_mouse;
    bool m_hasMouse = false;
    bool m_hpfDragging = false;

    float m_grDisplayDb = 0.0f;   // positive = reduction
    float m_inputDisplay = 0.0f;  // linear
    float m_outputDisplay = 0.0f; // linear
    double m_meterTimer = 0.0;

    static constexpr float kWinW = 640.0f;
    static constexpr float kContentH = 332.0f;
    static constexpr float kWinH = kContentH + AestraPanelWindow::TITLE_BAR_H;
};

} // namespace AestraUI
