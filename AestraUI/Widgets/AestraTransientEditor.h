// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <memory>
#include <string>

namespace AestraUI {

/// Aestra Transient editor — dedicated UI for com.Aestrastudios.transient.
///
/// The plugin is bipolar at its heart: Attack and Sustain amounts sit at the
/// centre detent (neutral) and pull in either direction. The generic editor's
/// uniform 0..1 sweep loses that meaning visually, so this editor renders
/// the two amounts as bidirectional arcs around 12 o'clock with a hard
/// centre marker. Output stays a conventional unipolar knob (its meaning is
/// "trim" not "amount"); Mix is a horizontal slider (matches Sat/Comp).
/// The center panel sketches a derived envelope (attack + decay halves scaled
/// by the current amounts) so the surface communicates "shape this envelope"
/// without requiring an audio-thread tap.
class AestraTransientEditor : public AestraPanelWindow {
public:
    explicit AestraTransientEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);

    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
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

    static constexpr float kWinW = 560.0f;
    static constexpr float kWinH = 320.0f;

    void layoutControls();
    void drawBipolarKnob(NUIRenderer& renderer, const NUIRect& rect, uint32_t paramId, const char* label,
                         const char* rangeText);
    void drawUnipolarKnob(NUIRenderer& renderer, const NUIRect& rect, uint32_t paramId, const char* label,
                          const char* rangeText);
    void drawMixSlider(NUIRenderer& renderer);
    void drawBypassPill(NUIRenderer& renderer);
    void drawEnvelopeSketch(NUIRenderer& renderer, const NUIRect& rect);

    // Hit-test helpers
    int knobAtPoint(const NUIPoint& p) const;
    bool mixContains(const NUIPoint& p) const { return m_mixRect.contains(p); }
    bool bypassContains(const NUIPoint& p) const { return m_bypassRect.contains(p); }

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;

    NUIRect m_attackRect;
    NUIRect m_sustainRect;
    NUIRect m_outputRect;
    NUIRect m_sketchRect;
    NUIRect m_mixRect;
    NUIRect m_bypassRect;

    // Drag state: -1 inactive; 0/1/2/3 = attack/sustain/output/mix drag in progress.
    int m_dragging = -1;
    bool m_bypassHovered = false;
};

} // namespace AestraUI
