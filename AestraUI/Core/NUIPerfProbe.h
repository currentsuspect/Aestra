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

#include <cstdint>
#include <string>

namespace AestraUI {

class NUIComponent;

namespace PerfProbe {

/// True when AESTRA_FRAME_STATS >= 2. Read once.
bool enabled();

/// A component started an invalidation chain (not a propagation to its parent).
void recordDirtyOrigin(const NUIComponent& component, const NUIComponent* parent);
/// Render self-time of one component (its children's time already excluded).
void recordRenderSelf(const NUIComponent& component, double ms);

enum class Phase : std::uint8_t { Tree, Submit, Swap };
void recordPhase(Phase phase, double ms);

/// Why the run loop presented this frame.
void recordPresentReason(const char* reason);

/// The four detail lines for the last window of @p presentedFrames frames; clears the counters.
std::string takeReport(size_t presentedFrames);

} // namespace PerfProbe
} // namespace AestraUI
