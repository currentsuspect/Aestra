// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "NUICoreWidgets.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace NomadUI {

class TimelineRuler : public NUIComponent {
public:
    TimelineRuler();

    void onRender(NUIRenderer& renderer) override;

    void setZoom(double zoom);
    double getZoom() const { return zoom_; }

private:
    double zoom_;
};

class TrackHeader : public NUIComponent {
public:
    TrackHeader();

    void onRender(NUIRenderer& renderer) override;

    void setTitle(const std::string& title);
    const std::string& getTitle() const { return title_; }

private:
    std::string title_;
};

class ClipRegion : public NUIComponent {
public:
    ClipRegion();

    void onRender(NUIRenderer& renderer) override;

    void setColor(const NUIColor& color);
    NUIColor getColor() const { return color_; }

    void setLooped(bool looped);
    bool isLooped() const { return looped_; }

private:
    NUIColor color_;
    bool looped_;
};

class AutomationCurve : public NUIComponent {
public:
    struct Point {
        float time;
        float value;
    };

    AutomationCurve();

    void onRender(NUIRenderer& renderer) override;

    void setPoints(const std::vector<Point>& points);
    const std::vector<Point>& getPoints() const { return points_; }

private:
    std::vector<Point> points_;
};

class GridLines : public NUIComponent {
public:
    GridLines();

    void onRender(NUIRenderer& renderer) override;

    void setSpacing(float spacing);
    float getSpacing() const { return spacing_; }

private:
    float spacing_;
};

class Playhead : public NUIComponent {
public:
    Playhead();

    void onRender(NUIRenderer& renderer) override;

    void setPosition(double position);
    double getPosition() const { return position_; }

private:
    double position_;
};

class SelectionBox : public NUIComponent {
public:
    SelectionBox();

    void onRender(NUIRenderer& renderer) override;

    void setSelectionRect(const NUIRect& rect);
    NUIRect getSelectionRect() const { return selectionRect_; }

private:
    NUIRect selectionRect_;
};

class ZoomControls : public NUIComponent {
public:
    ZoomControls();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setZoom(double zoom);
    double getZoom() const { return zoom_; }

    void setOnZoomChanged(std::function<void(double)> callback);

private:
    double zoom_;
    std::function<void(double)> onZoomChanged_;
};

class ArrangementCanvas : public NUIComponent {
public:
    ArrangementCanvas();

    void onRender(NUIRenderer& renderer) override;

    void addTrackHeader(std::shared_ptr<TrackHeader> header);
    void addClip(std::shared_ptr<ClipRegion> clip);
    void setTimeline(std::shared_ptr<TimelineRuler> ruler);

    const std::vector<std::shared_ptr<TrackHeader>>& getTrackHeaders() const { return trackHeaders_; }
    const std::vector<std::shared_ptr<ClipRegion>>& getClips() const { return clips_; }
    std::shared_ptr<TimelineRuler> getTimeline() const { return timeline_; }

private:
    std::vector<std::shared_ptr<TrackHeader>> trackHeaders_;
    std::vector<std::shared_ptr<ClipRegion>> clips_;
    std::shared_ptr<TimelineRuler> timeline_;
};

} // namespace NomadUI

