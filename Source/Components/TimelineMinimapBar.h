// ¶¸ 2025 Aestra Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "TimelineMinimapModel.h"
#include "TimelineMinimapRenderer.h"

#include <functional>
#include <string>

namespace AestraUI {

enum class TimelineMinimapResizeEdge
{
    Left,
    Right,
};

enum class TimelineMinimapCursorHint
{
    Default,
    ResizeHorizontal,
};

class TimelineMinimapBar final : public NUIComponent
{
public:
    /** @brief Create the shared timeline minimap widget. */
    TimelineMinimapBar();

    void onRender(NUIRenderer& renderer) override;
    void onThemeChanged(const NUIThemeProperties& theme) override { cacheThemeColors_(); NUIComponent::onThemeChanged(theme); }
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onMouseLeave() override;

    /** @brief Replace the model consumed by the minimap. */
    void setModel(const TimelineMinimapModel& model);
    /** @brief Get the current minimap model. */
    const TimelineMinimapModel& getModel() const { return model_; }
    /** @brief Get the current cursor hint for the host UI. */
    TimelineMinimapCursorHint getCursorHint() const { return cursorHint_; }
    /** @brief Enable or disable the top-left mode toggles. */
    void setShowModeToggles(bool show) { showModeToggles_ = show; repaint(); }
    /** @brief Set an additional leading inset applied before the minimap content. */
    void setLeadingInset(float insetPx) { leadingInsetPx_ = insetPx; repaint(); }

    /** @brief Callback requesting that the host center the view around a beat. */
    std::function<void(double centerBeat)> onRequestCenterView;
    /** @brief Callback requesting a new view-start beat. */
    std::function<void(double viewStartBeat, bool isFinal)> onRequestSetViewStart;
    /** @brief Callback requesting viewport-edge resizing. */
    std::function<void(TimelineMinimapResizeEdge edge, double anchorBeat, double edgeBeat, bool isFinal)> onRequestResizeViewEdge;
    /** @brief Callback requesting zoom around an anchor beat. */
    std::function<void(double anchorBeat, float zoomMultiplier)> onRequestZoomAround;
    /** @brief Callback fired when the minimap mode toggle changes. */
    std::function<void(TimelineMinimapMode mode)> onModeChanged;

private:
    enum class DragKind
    {
        None,
        Viewport,
        Pan,
        ResizeLeft,
        ResizeRight,
    };

    void cacheThemeColors_();
    TimelineMinimapLayout computeLayout_() const;

    bool hitToggle_(const NUIPoint& p, TimelineMinimapMode& outMode) const;
    NUIRect toggleRect_(int index) const;

    void renderToggles_(NUIRenderer& renderer, const TimelineMinimapLayout& layout);
    std::string formatHoverText_(double hoverBeat) const;
    void endDrag_();

    TimelineMinimapModel model_;
    /// Version of model_.summary at the last setModel(), stored by value
    /// (the snapshot pointer's contents mutate in place).
    uint64_t lastSeenSummaryVersion_ = 0;
    TimelineMinimapRenderer renderer_;
    TimelineMinimapRenderColors colors_{};

    TimelineMinimapCursorHint cursorHint_{TimelineMinimapCursorHint::Default};
    TimelineMinimapResizeEdge hoverResizeEdge_{TimelineMinimapResizeEdge::Left};
    bool hoverOnResizeEdge_ = false;

    DragKind dragKind_ = DragKind::None;
    NUIPoint dragStartPos_{};
    double dragStartViewStartBeat_ = 0.0;
    double dragGrabOffsetBeat_ = 0.0;
    double dragAnchorBeat_ = 0.0;
    bool dragMoved_ = false;
    bool dragCtrlFast_ = false;

    bool hoverInMap_ = false;
    double hoverBeat_ = 0.0;
    NUIPoint hoverPos_{};
    int hoverToggleIndex_ = -1;
    bool showModeToggles_ = true;
    float leadingInsetPx_ = -1.0f;

    // Cached toggle bounds (absolute).
    NUIRect toggleBounds_[3]{};
};

} // namespace AestraUI
