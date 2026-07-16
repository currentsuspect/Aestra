// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUITypes.h"
#include "PluginHost.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace AestraUI {

class RumblePluginEditor : public AestraPanelWindow {
public:
    explicit RumblePluginEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    ~RumblePluginEditor() override = default;

    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;

private:
    enum class Zone { Impact, Body };

    struct MacroControl {
        uint32_t parameterId = 0;
        std::string title;
        std::string subtitle;
        float defaultValue = 0.0f;
        float normalizedValue = 0.0f;
        Zone zone = Zone::Body;
        NUIRect bounds;
        NUIRect knobBounds;
    };

    void buildControls();
    void layoutControls();
    void syncControlValues();
    void drawHeader(NUIRenderer& renderer);
    void drawImpactPanel(NUIRenderer& renderer);
    void drawBodyPanel(NUIRenderer& renderer);
    void drawImpactShape(NUIRenderer& renderer);
    void drawKnob(NUIRenderer& renderer, const MacroControl& control, int controlIndex, bool primary);
    void drawPresetBrowser(NUIRenderer& renderer);
    int hitTestControl(NUIPoint point) const;
    void setControlValue(int controlIndex, float normalizedValue);
    void applyFactoryPreset(size_t presetIndex);
    int matchingFactoryPreset() const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<MacroControl> m_controls;

    NUIRect m_headerBounds;
    NUIRect m_presetButtonBounds;
    NUIRect m_previousPresetBounds;
    NUIRect m_nextPresetBounds;
    NUIRect m_impactPanelBounds;
    NUIRect m_bodyPanelBounds;
    NUIRect m_impactShapeBounds;
    NUIRect m_presetMenuBounds;
    std::array<NUIRect, 16> m_presetItemBounds{};

    int m_hoveredControl = -1;
    int m_draggingControl = -1;
    int m_hoveredPreset = -1;
    size_t m_activePreset = 1;
    float m_dragStartY = 0.0f;
    float m_dragStartValue = 0.0f;
    bool m_presetMenuOpen = false;
    bool m_presetButtonHovered = false;
    bool m_previousPresetHovered = false;
    bool m_nextPresetHovered = false;

    // Manual double-click detection (the platform never sets
    // NUIMouseEvent::doubleClick) for knob reset-to-default.
    long long m_lastClickTimeMs = 0;
    NUIPoint m_lastClickPos;

    static constexpr float kWindowWidth = 840.0f;
    static constexpr float kWindowHeight = 520.0f;
    static constexpr float kPadding = 14.0f;
    static constexpr float kHeaderHeight = 76.0f;
    static constexpr float kDragRangePixels = 180.0f;
};

} // namespace AestraUI
