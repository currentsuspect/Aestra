// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "EmberControls.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <memory>
#include <string>

namespace AestraUI {

class AestraSatEditor : public AestraPanelWindow {
public:
    explicit AestraSatEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    void layoutControls();
    void drawTransferCurve(NUIRenderer& renderer, bool bypassed);
    void drawMeters(NUIRenderer& renderer, bool bypassed);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    EditorKit::EmberKnobBoard m_board;
    EditorKit::EmberSegments m_mode;
    EditorKit::EmberSliderControl m_mix;

    NUIRect m_curveRect;
    NUIRect m_meterRects[2]; // In, Out
    NUIPoint m_origin;

    float m_inputDisplay = 0.0f;  // linear
    float m_outputDisplay = 0.0f; // linear
    double m_meterTimer = 0.0;

    static constexpr float kWinW = 520.0f;
    static constexpr float kContentH = 256.0f;
    static constexpr float kWinH = kContentH + 2.0f * AestraPanelWindow::TITLE_BAR_H; // two-row header
};

} // namespace AestraUI
