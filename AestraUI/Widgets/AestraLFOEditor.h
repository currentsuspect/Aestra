// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "EmberControls.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <memory>
#include <string>

namespace AestraUI {

class AestraLFOEditor : public AestraPanelWindow {
public:
    explicit AestraLFOEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    void drawOverlay(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    void layoutControls();
    void drawShape(NUIRenderer& renderer, bool bypassed);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    EditorKit::EmberKnobBoard m_board;
    EditorKit::EmberSegments m_target;
    EditorKit::EmberSegments m_sync;
    EditorKit::EmberSegments m_wave;
    EditorKit::EmberDropdown m_division;

    NUIRect m_well;
    float m_level = 0.0f; // 0..1, the LFO's current position
    double m_meterTimer = 0.0;

    static constexpr float kWinW = 560.0f;
    static constexpr float kContentH = 298.0f;
    static constexpr float kWinH = kContentH + 2.0f * AestraPanelWindow::TITLE_BAR_H; // two-row header
};

} // namespace AestraUI
