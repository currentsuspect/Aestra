// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V83-HS1 (FD-63): gather MIDI notes from a TrackManager for the harmony service. Headless, on demand, never on
// the audio thread. Reads the playlist model and the pattern manager; changes nothing.
#pragma once

#include "Harmony/HarmonyAnalysis.h"
#include "Models/PatternSource.h"

#include <vector>

namespace Aestra {
namespace Audio {

class TrackManager;

namespace Harmony {

/** One pattern on its own, at its own beats (the piano roll's view). Empty for audio patterns. */
std::vector<HarmonyNote> gatherPatternNotes(const PatternSource& pattern);

/**
 * Every MIDI pattern clip on the timeline, at its arranged position, across every unit. Muted lanes and muted
 * clips are skipped. A clip longer than its pattern repeats the pattern; a shorter one cuts it. When toBeat >
 * fromBeat only notes overlapping [fromBeat, toBeat) are returned, clipped to it.
 */
std::vector<HarmonyNote> gatherArrangementNotes(TrackManager& tracks, double fromBeat = 0.0,
                                                double toBeat = 0.0);

} // namespace Harmony
} // namespace Audio
} // namespace Aestra
