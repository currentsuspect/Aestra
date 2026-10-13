// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUISlider.h"
#include "NUIRenderer.h"

#include <algorithm>

namespace AestraUI {

/**
 * @brief The slider of the sharp-cornered Aestra look: a flat track, a flat fill and a square thumb, no glow.
 *
 * Mechanics are NUISlider's (drag, Shift for fine, double-click back to the default). A bipolar slider fills from the
 * zero point, so a pan or a pitch reads as a distance from centre. Used by the floating editor panels.
 */
class FlatSlider : public NUISlider {
public:
    explicit FlatSlider(bool bipolar = false) : NUISlider(), m_bipolar(bipolar) {}

protected:
    NUIRect trackRect() const {
        const auto bounds = getBounds();
        const float inset = std::min(getSliderRadius(), bounds.width * 0.5f);
        return {bounds.x + inset, bounds.y + (bounds.height - getSliderThickness()) * 0.5f,
                std::max(0.0f, bounds.width - inset * 2.0f), getSliderThickness()};
    }

    void drawSliderTrack(NUIRenderer& renderer) override {
        const NUIRect track = trackRect();
        renderer.fillRect(track, getTrackColor());
        const float valueX = track.x + track.width * static_cast<float>(valueToProportionOfLength(getValue()));
        if (m_bipolar) {
            const float centreX = track.x + track.width * static_cast<float>(valueToProportionOfLength(0.0));
            const float left = std::min(centreX, valueX), right = std::max(centreX, valueX);
            if (right > left) renderer.fillRect({left, track.y, right - left, track.height}, getFillColor());
            renderer.fillRect({centreX - 0.5f, track.y - 2.0f, 1.0f, track.height + 4.0f}, getThumbColor().withAlpha(0.35f));
        } else if (valueX > track.x) {
            renderer.fillRect({track.x, track.y, valueX - track.x, track.height}, getFillColor());
        }
    }

    void drawSliderThumb(NUIRenderer& renderer) override {
        const NUIRect track = trackRect();
        const float x = track.x + track.width * static_cast<float>(valueToProportionOfLength(getValue()));
        const auto bounds = getBounds();
        const float h = std::min(bounds.height, 16.0f);
        renderer.fillRect({std::round(x) - 3.0f, bounds.y + (bounds.height - h) * 0.5f, 6.0f, h},
                          isHovered() ? getThumbHoverColor() : getThumbColor());
    }

private:
    bool m_bipolar{false};
};

} // namespace AestraUI
