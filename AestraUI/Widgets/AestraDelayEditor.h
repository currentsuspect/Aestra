// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "EmberControls.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <memory>
#include <string>

namespace AestraUI {

class AestraDelayEditor : public AestraPanelWindow {
public:
    explicit AestraDelayEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    void drawOverlay(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    void onUpdate(double deltaTime) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    void layoutControls();
    void drawTaps(NUIRenderer& renderer, bool bypassed);
    float effectiveMs() const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    EditorKit::EmberKnobBoard m_board;
    EditorKit::EmberSegments m_sync;
    EditorKit::EmberSegments m_stereoMode;
    EditorKit::EmberDropdown m_division;
    EditorKit::EmberSliderControl m_mix;

    NUIRect m_well;
    double m_repaintTimer = 0.0;

    static constexpr float kWinW = 680.0f;
    static constexpr float kContentH = 338.0f;
    static constexpr float kWinH = kContentH + 2.0f * AestraPanelWindow::TITLE_BAR_H; // two-row header
};

} // namespace AestraUI
