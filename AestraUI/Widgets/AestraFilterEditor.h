// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "EmberControls.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <memory>
#include <string>

namespace AestraUI {

class AestraFilterEditor : public AestraPanelWindow {
public:
    explicit AestraFilterEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    void layoutControls();
    void drawResponse(NUIRenderer& renderer, bool bypassed);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    EditorKit::EmberKnobBoard m_board;
    EditorKit::EmberSegments m_type;

    NUIRect m_well;
    float m_envLevel = 0.0f; // 0..1, the envelope follower
    double m_meterTimer = 0.0;

    static constexpr float kWinW = 600.0f;
    static constexpr float kContentH = 334.0f;
    static constexpr float kWinH = kContentH + 2.0f * AestraPanelWindow::TITLE_BAR_H; // two-row header
};

} // namespace AestraUI
