// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIArrangementWidgets.h"

#include "../Graphics/NUIRenderer.h"
#include <algorithm>
#include <string>

namespace NomadUI {

TimelineRuler::TimelineRuler()
    : zoom_(1.0)
{
}

void TimelineRuler::onRender(NUIRenderer& renderer)
{
    NUIRect bounds = getBounds();
    
    // Background
    renderer.fillRect(bounds, NUIColor::fromHex(0x2A2A2AFF));
    
    // Bottom border
    renderer.drawLine(NUIPoint(bounds.x, bounds.bottom()), 
                      NUIPoint(bounds.right(), bounds.bottom()), 
                      1.0f, NUIColor::fromHex(0x3A3A3AFF));

    // Ticks and Labels
    // Assume 120 BPM, 4/4 signature for now
    // zoom_ = pixels per beat? Let's say zoom 1.0 = 50 pixels per beat.
    float pixelsPerBeat = 50.0f * static_cast<float>(zoom_);
    float pixelsPerBar = pixelsPerBeat * 4.0f;
    
    int startBar = 0;
    int endBar = static_cast<int>(bounds.width / pixelsPerBar) + 1;
    
    for (int bar = startBar; bar <= endBar; ++bar) {
        float x = bounds.x + bar * pixelsPerBar;
        
        // Major tick (Bar)
        renderer.drawLine(NUIPoint(x, bounds.bottom()), 
                          NUIPoint(x, bounds.bottom() - 15.0f), 
                          1.0f, NUIColor::fromHex(0xAAAAAAFF));
                          
        // Label
        std::string label = std::to_string(bar + 1);
        renderer.drawText(label, NUIPoint(x + 5.0f, bounds.y + 5.0f), 10.0f, NUIColor::fromHex(0xAAAAAAFF));
        
        // Minor ticks (Beats)
        for (int beat = 1; beat < 4; ++beat) {
            float bx = x + beat * pixelsPerBeat;
            if (bx > bounds.right()) break;
            renderer.drawLine(NUIPoint(bx, bounds.bottom()), 
                              NUIPoint(bx, bounds.bottom() - 8.0f), 
                              1.0f, NUIColor::fromHex(0x666666FF));
        }
    }
}

void TimelineRuler::setZoom(double zoom)
{
    zoom_ = std::clamp(zoom, 0.1, 16.0);
    repaint();
}

TrackHeader::TrackHeader()
    : title_("Track")
{
}

void TrackHeader::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void TrackHeader::setTitle(const std::string& title)
{
    title_ = title;
    repaint();
}

ClipRegion::ClipRegion()
    : color_(NUIColor::fromHex(0xff3333ff)), looped_(false)
{
}

void ClipRegion::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void ClipRegion::setColor(const NUIColor& color)
{
    color_ = color;
    repaint();
}

void ClipRegion::setLooped(bool looped)
{
    looped_ = looped;
    repaint();
}

AutomationCurve::AutomationCurve() = default;

void AutomationCurve::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void AutomationCurve::setPoints(const std::vector<Point>& points)
{
    points_ = points;
    repaint();
}

GridLines::GridLines()
    : spacing_(1.0f)
{
}

void GridLines::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void GridLines::setSpacing(float spacing)
{
    spacing_ = std::max(0.1f, spacing);
    repaint();
}

Playhead::Playhead()
    : position_(0.0)
{
}

void Playhead::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void Playhead::setPosition(double position)
{
    position_ = std::max(0.0, position);
    repaint();
}

SelectionBox::SelectionBox()
{
    selectionRect_ = {0.0f, 0.0f, 0.0f, 0.0f};
}

void SelectionBox::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void SelectionBox::setSelectionRect(const NUIRect& rect)
{
    selectionRect_ = rect;
    repaint();
}

ZoomControls::ZoomControls()
    : zoom_(1.0)
{
}

void ZoomControls::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

bool ZoomControls::onMouseEvent(const NUIMouseEvent& event)
{
    // Map legacy scroll event to current wheelDelta
    if (event.wheelDelta != 0.0f)
    {
        setZoom(zoom_ + event.wheelDelta * 0.1);
        return true;
    }
    return false;
}

void ZoomControls::setZoom(double zoom)
{
    zoom = std::clamp(zoom, 0.1, 16.0);
    if (zoom_ == zoom)
        return;
    zoom_ = zoom;
    repaint();
    if (onZoomChanged_)
        onZoomChanged_(zoom_);
}

void ZoomControls::setOnZoomChanged(std::function<void(double)> callback)
{
    onZoomChanged_ = std::move(callback);
}

ArrangementCanvas::ArrangementCanvas() = default;

void ArrangementCanvas::onRender(NUIRenderer& renderer)
{
    (void)renderer;
}

void ArrangementCanvas::addTrackHeader(std::shared_ptr<TrackHeader> header)
{
    if (!header)
        return;
    trackHeaders_.push_back(header);
    addChild(header);
}

void ArrangementCanvas::addClip(std::shared_ptr<ClipRegion> clip)
{
    if (!clip)
        return;
    clips_.push_back(clip);
    addChild(clip);
}

void ArrangementCanvas::setTimeline(std::shared_ptr<TimelineRuler> ruler)
{
    timeline_ = ruler;
    if (timeline_)
        addChild(timeline_);
}

} // namespace NomadUI
