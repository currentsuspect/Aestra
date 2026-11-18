// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once
#include "../Graphics/NUIDepth.h"
#include "NUIComponent.h"
#include "NUISlidingIndicator.h"
#include "NUIRenderer.h"
#include "NUITypes.h"
#include "NUIThemeSystem.h"
#include <functional>
#include <string>
#include <vector>

namespace AestraUI {

/**
 * NUISegmentedControl - A modern segmented toggle control with sliding indicator
 * 
 * Creates a pill-shaped container with multiple segments. Click to switch between them.
 * Features a smooth sliding indicator that moves to the selected segment.
 */
class NUISegmentedControl : public NUIComponent {
public:
    enum class VisualStyle {
        Pill,
        UnderlineTabs
    };

    NUISegmentedControl(const std::vector<std::string>& segments)
        : segments_(segments)
        , selectedIndex_(0)
        , cornerRadius_(-1.0f)
        , accentColor_(NUIThemeManager::getInstance().getColor("accentPrimary"))
        , hoveredIndex_(-1)
        , visualStyle_(VisualStyle::Pill)
    {
        setId("SegmentedControl");
    }
    
    void setSelectedIndex(size_t index, bool animate = true) {
        if (index >= segments_.size()) return;
        if (index == selectedIndex_) {
            if (!animate) {
                indicator_.snapTo(static_cast<float>(index));
                setDirty(true);
            }
            return;
        }
        selectedIndex_ = index;
        if (!animate) {
            indicator_.snapTo(static_cast<float>(index));
        }
        if (onSelectionChanged_) {
            onSelectionChanged_(selectedIndex_);
        }
        setDirty(true);
    }
    
    size_t getSelectedIndex() const { return selectedIndex_; }
    
    void setOnSelectionChanged(std::function<void(size_t)> callback) {
        onSelectionChanged_ = callback;
    }
    
    void setCornerRadius(float radius) { cornerRadius_ = radius; }
    void setAccentColor(const NUIColor& color) { accentColor_ = color; setDirty(true); }
    void setVisualStyle(VisualStyle style) { visualStyle_ = style; setDirty(true); }
    
    void onRender(NUIRenderer& renderer) override {
        // Design Spec, Segment switch (locked 12 Nov 2026): one part for every "pick one of a few", tabs included. The
        // track is sunk; the chosen option sits on a lit thumb that slides to a new choice; unchosen labels are
        // textMuted and the chosen or hovered one textPrimary. No outline, no underline tabs, quiet hover.
        auto bounds = getBounds();
        auto& theme = NUIThemeManager::getInstance();
        const auto& props = theme.getCurrentTheme();
        const float fontSize = props.fontSizeS;
        const bool enabled = isEnabled();
        const float radius = bounds.height * 0.5f;

        Depth::bed(renderer, bounds, radius, Depth::Cut::Shallow);
        if (segments_.empty()) {
            NUIComponent::onRender(renderer);
            return;
        }

        constexpr float kPad = 3.0f;
        const float segmentWidth = (bounds.width - kPad * 2.0f) / static_cast<float>(segments_.size());
        const NUIRect thumb(bounds.x + kPad + indicator_.position() * segmentWidth, bounds.y + kPad, segmentWidth,
                            bounds.height - kPad * 2.0f);
        Depth::ObjectLook look;
        look.base = enabled ? props.surfaceRaised : props.surfaceTertiary;
        look.lightTop = 0.12f;
        look.shadeBottom = 0.14f;
        Depth::object(renderer, thumb, thumb.height * 0.5f, look);

        for (size_t i = 0; i < segments_.size(); ++i) {
            const NUIRect segmentBounds(bounds.x + kPad + static_cast<float>(i) * segmentWidth, bounds.y, segmentWidth,
                                        bounds.height);
            const bool isSelected = i == selectedIndex_;
            const bool hovered = enabled && static_cast<int>(i) == hoveredIndex_;
            const NUIColor textColor = !enabled ? props.textDisabled
                                       : (isSelected || hovered) ? props.textPrimary
                                                                 : props.textMuted;
            renderer.drawTextCentered(segments_[i], segmentBounds, fontSize, textColor);
        }
        NUIComponent::onRender(renderer);
    }
    
    void onUpdate(double deltaTime) override {
        // selectedIndex_ remains the single authority; the indicator chases it.
        // The first sync snaps (nothing to animate from), later ones animate,
        // and an unchanged target does nothing — so layout, resize or segment
        // changes never replay a selection animation.
        constexpr float kIndicatorSpeed = 12.0f;
        if (indicator_.sync(static_cast<float>(selectedIndex_), deltaTime, kIndicatorSpeed)) {
            setDirty(true);
        }

        NUIComponent::onUpdate(deltaTime);
    }
    
    bool onMouseEvent(const NUIMouseEvent& event) override {
        if (!isVisible() || !isEnabled()) return false;
        if (segments_.empty()) return false;

        auto bounds = getBounds();

        if (event.button == NUIMouseButton::None) {
            int newHover = -1;
            if (bounds.contains(event.position)) {
                float relativeX = event.position.x - bounds.x;
                float segmentWidth = bounds.width / static_cast<float>(segments_.size());
                size_t hoverIndex = static_cast<size_t>(relativeX / segmentWidth);
                if (hoverIndex < segments_.size()) {
                    newHover = static_cast<int>(hoverIndex);
                }
            }
            if (newHover != hoveredIndex_ && !event.cursorCaptured) {
                hoveredIndex_ = newHover;
                setDirty(true);
            }
        }
        
        if (event.pressed && event.button == NUIMouseButton::Left) {
            if (bounds.contains(event.position)) {
                setFocused(true);
                // Determine which segment was clicked
                float relativeX = event.position.x - bounds.x;
                float segmentWidth = bounds.width / static_cast<float>(segments_.size());
                size_t clickedIndex = static_cast<size_t>(relativeX / segmentWidth);
                
                if (clickedIndex < segments_.size() && clickedIndex != selectedIndex_) {
                    setSelectedIndex(clickedIndex, true);
                }
                return true;
            }
        }
        
        return NUIComponent::onMouseEvent(event);
    }

    void onMouseLeave() override
    {
        if (hoveredIndex_ != -1) {
            hoveredIndex_ = -1;
            setDirty(true);
        }
        NUIComponent::onMouseLeave();
    }
    
private:
    std::vector<std::string> segments_;
    size_t selectedIndex_;
    // Indicator position + whether it has ever been synchronised (#653 follow-up).
    // Initialisation is explicit state, not inferred from the position value:
    // 0.0f is a valid position, so it cannot distinguish "never initialised"
    // from "correctly on segment zero".
    NUISlidingIndicator indicator_;
    float cornerRadius_;
    NUIColor accentColor_;
    int hoveredIndex_;
    VisualStyle visualStyle_;
    std::function<void(size_t)> onSelectionChanged_;
};

} // namespace AestraUI
