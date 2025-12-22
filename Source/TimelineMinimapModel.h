// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstddef>
#include <cstdint>

namespace NomadUI {

struct TimelineSummarySnapshot;

enum class TimelineMinimapMode
{
    Clips = 0,
    Energy = 1,
    Automation = 2,
};

enum class TimelineMinimapAggregation
{
    MaxPresence = 0,
    SumDensity = 1,
};

enum class TimelineMinimapMarkKind
{
    ClipMissing = 0,
    MasterClip = 1,
    XRun = 2,
    SearchHit = 3,
    Marker = 4,
};

struct TimelineRange
{
    double start = 0.0;
    double end = 0.0;

    bool isValid() const { return end > start; }
};

struct TimelineMinimapMark
{
    double t = 0.0;
    TimelineMinimapMarkKind kind = TimelineMinimapMarkKind::Marker;
    float severity = 1.0f;
};

// A "one-shot" state bundle. The controller fills it; the bar renders it.
struct TimelineMinimapModel
{
    const TimelineSummarySnapshot* summary = nullptr;

    // Canonical unit: beats (controller converts from seconds/tempo as needed).
    TimelineRange view;
    double playheadBeat = 0.0;

    TimelineRange loop;
    TimelineRange selection;

    const TimelineMinimapMark* marks = nullptr;
    size_t markCount = 0;

    TimelineMinimapMode mode = TimelineMinimapMode::Clips;
    TimelineMinimapAggregation aggregation = TimelineMinimapAggregation::MaxPresence;

    int beatsPerBar = 4;

    bool showSelection = true;
    bool showLoop = true;
    bool showMarkers = true;
    bool showDiagnostics = true;
};

} // namespace NomadUI

