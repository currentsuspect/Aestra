// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "EmberControls.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <memory>
#include <string>

namespace AestraUI {

class AestraDriftEditor : public AestraPanelWindow {
public:
    explicit AestraDriftEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    void layoutControls();
    void drawRuler(NUIRenderer& renderer, bool bypassed);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    EditorKit::EmberKnobBoard m_board;
    EditorKit::EmberSegments m_intervals; ///< shortcuts that write Pitch; not a parameter of their own
    EditorKit::EmberSliderControl m_mix;

    NUIRect m_well;

    // 12 px wider than the mock: eight knobs, three group gaps and the 4 px minimum knob gap need a 588 px row.
    static constexpr float kWinW = 612.0f;
    static constexpr float kContentH = 310.0f;
    static constexpr float kWinH = kContentH + 2.0f * AestraPanelWindow::TITLE_BAR_H; // two-row header
};

} // namespace AestraUI
