// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "TimelineMinimapModel.h"
#include "TimelineMinimapRenderer.h"

#include <functional>
#include <string>

namespace NomadUI {

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
    TimelineMinimapBar();

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onMouseLeave() override;

    void setModel(const TimelineMinimapModel& model);
    const TimelineMinimapModel& getModel() const { return model_; }
    TimelineMinimapCursorHint getCursorHint() const { return cursorHint_; }

    std::function<void(double centerBeat)> onRequestCenterView;
    std::function<void(double viewStartBeat, bool isFinal)> onRequestSetViewStart;
    std::function<void(TimelineMinimapResizeEdge edge, double anchorBeat, double edgeBeat, bool isFinal)> onRequestResizeViewEdge;
    std::function<void(double anchorBeat, float zoomMultiplier)> onRequestZoomAround;
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
    void renderTooltip_(NUIRenderer& renderer, const TimelineMinimapLayout& layout) const;
    std::string formatHoverText_(double hoverBeat) const;
    void endDrag_();

    TimelineMinimapModel model_;
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

    // Cached toggle bounds (absolute).
    NUIRect toggleBounds_[3]{};
};

} // namespace NomadUI
