// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Opt-in frame attribution (SPEC 3 §4): the probes that found why the idle app redrew
// every frame and where an active piano-roll frame's time went, kept as a permanent tool.
//
// AESTRA_FRAME_STATS=1  frame work summary once a second (AestraApp's run loop)
// AESTRA_FRAME_STATS=2  also, once a second:
//   [FrameWhy]   why each frame was presented (dirty / input / transport / ... / heartbeat)
//   [DirtyWho]   which component types START invalidations, as Type<ParentType>=count
//   [RenderSelf] render self-time per component type, ms per presented frame (children excluded)
//   [Phase]      widget tree / renderer submit / swap, ms per presented frame
//
// Main (UI) thread only. When off, every hook is one cached boolean check.

#include <cstddef>
#include <cstdint>
#include <string>

namespace AestraUI {

class NUIComponent;

namespace PerfProbe {

/// AESTRA_FRAME_STATS as a number: 0 (off) when unset, "0" or not a number. Read once.
int level();
/// True when AESTRA_FRAME_STATS >= 2.
bool enabled();

/// The frame-work report (level >= 1). frameWorkBegin() marks where a frame's work starts
/// (after the previous frame's pacing sleep); frameWorkEnd() marks where it ends (after the
/// swap, before the sleep) and, once a second, logs the [FrameStats] line plus, at level 2,
/// the detail lines. UnifiedProfiler's totalTimeMs includes the sleep, so it cannot show a
/// dropped frame; this can. Both are a cached check and nothing else when off.
void frameWorkBegin();
void frameWorkEnd(bool presented);

/// A component started an invalidation chain (not a propagation to its parent).
void recordDirtyOrigin(const NUIComponent& component, const NUIComponent* parent);
/// Render self-time of one component (its children's time already excluded).
void recordRenderSelf(const NUIComponent& component, double ms);

enum class Phase : std::uint8_t { Tree, Submit, Swap };
void recordPhase(Phase phase, double ms);

/// Why the run loop presented this frame.
void recordPresentReason(const char* reason);
/// recordPresentReason() when enabled, then true: `return presentBecause("dirty");` in a present gate.
bool presentBecause(const char* reason);

/// The four detail lines for the last window of @p presentedFrames frames; clears the counters.
std::string takeReport(std::size_t presentedFrames);

} // namespace PerfProbe
} // namespace AestraUI
