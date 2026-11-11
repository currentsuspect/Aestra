// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "EmberControls.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <memory>
#include <string>

namespace AestraUI {

/// Aestra Transient editor — dedicated UI for com.Aestrastudios.transient.
///
/// Attack and Sustain are bipolar (centre = neutral), so they draw as bidirectional arcs; Output is bipolar
/// too. The well sketches an envelope bent by the two amounts so the surface says "shape this envelope"
/// without an audio-thread tap. It is a sketch, not a measurement, and says so; the In and Out meters are measured.
class AestraTransientEditor : public AestraPanelWindow {
public:
    explicit AestraTransientEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);

    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }
    void setPlatformBridge(NUIPlatformBridge* bridge) override;

private:
    // Param IDs the editor binds against the plugin instance.
    static constexpr uint32_t kAttack = 0;
    static constexpr uint32_t kSustain = 1;
    static constexpr uint32_t kOutput = 2;
    static constexpr uint32_t kMix = 3;
    static constexpr uint32_t kBypass = 4;

    static constexpr float kWinW = 520.0f;
    static constexpr float kContentH = 298.0f;
    static constexpr float kWinH = kContentH + 2.0f * AestraPanelWindow::TITLE_BAR_H; // two-row header

    void layoutControls();
    void drawEnvelopeSketch(NUIRenderer& renderer, bool bypassed);
    void drawMeters(NUIRenderer& renderer, bool bypassed);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    EditorKit::EmberKnobBoard m_board;

    NUIRect m_sketchRect;
    NUIRect m_meterRects[2]; // In, Out
    float m_inputDisplay = 0.0f;
    float m_outputDisplay = 0.0f;
    double m_meterTimer = 0.0;
};

} // namespace AestraUI
