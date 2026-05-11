// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUITypes.h"
#include "PluginHost.h"

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
    struct MacroControl {
        uint32_t parameterId = 0;
        std::string title;
        std::string subtitle;
        float normalizedValue = 0.0f;
        NUIRect cardBounds;
        NUIRect trackBounds;
        NUIRect thumbBounds;
        bool isDragging = false;
    };

    void buildControls();
    void layoutControls();
    void drawHero(NUIRenderer& renderer, const NUIRect& bounds);
    void drawControl(NUIRenderer& renderer, const MacroControl& control, bool hovered);
    int hitTestControl(float x, float y) const;
    void updateControlValue(int controlIndex, float normalizedValue);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<MacroControl> m_controls;

    int m_hoveredControl = -1;

    static constexpr float kWindowWidth = 520.0f;
    static constexpr float kWindowHeight = 340.0f;
    static constexpr float kHeroHeight = 78.0f;
    static constexpr float kPadding = 14.0f;
};

} // namespace AestraUI
