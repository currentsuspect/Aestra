// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUITypes.h"

namespace NomadUI {

class NUIRenderer;
struct TimelineMinimapModel;

struct TimelineMinimapLayout
{
    NUIRect bounds;
    NUIRect cornerRect;
    NUIRect mapRect;
};

struct TimelineMinimapRenderColors
{
    NUIColor glassFill;
    NUIColor glassBorder;
    NUIColor cornerSeparator;

    NUIColor audioTint;
    NUIColor midiTint;
    NUIColor automationTint;
    NUIColor baseline;

    NUIColor viewFill;
    NUIColor viewOutline;
    NUIColor selectionFill;
    NUIColor loopFill;

    NUIColor playheadDark;
    NUIColor playheadBright;

    NUIColor text;
};

class TimelineMinimapRenderer final
{
public:
    void render(NUIRenderer& renderer, const TimelineMinimapLayout& layout, const TimelineMinimapModel& model,
                const TimelineMinimapRenderColors& colors) const;

    static float timeToX(double beat, const NUIRect& mapRect, double domainStartBeat, double domainEndBeat);
    static double xToTime(float x, const NUIRect& mapRect, double domainStartBeat, double domainEndBeat);
};

} // namespace NomadUI

