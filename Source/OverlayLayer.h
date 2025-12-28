// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file OverlayLayer.h
 * @brief Layer for overlays with pass-through hit-testing
 */

#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIMouseEvent.h"

/**
 * @brief Layer for overlays (Mixer, Piano Roll, etc.) with pass-through hit-testing
 */
class OverlayLayer : public NomadUI::NUIComponent {
public:
    OverlayLayer() {
        setId("OverlayLayer");
    }

    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override {
        // Iterate children in reverse z-order for hit-testing
        auto children = getChildren();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            auto& child = *it;
            if (child && child->isVisible() && child->isHitTestVisible()) {
                if (child->onMouseEvent(event)) {
                    return true; 
                }
            }
        }
        return false;
    }
};
