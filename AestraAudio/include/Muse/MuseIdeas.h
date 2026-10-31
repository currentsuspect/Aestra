// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V83-MU1 (FD-63): the Muse Quiet idea engine. Given the notes so far and the harmony service's answer, it writes
// a few short ideas for the next bar. Rules only: no learning, no randomness, no network. Pure data in, pure data
// out; it runs on demand on a non-real-time thread and never touches the project.
#pragma once

#include "Harmony/HarmonyAnalysis.h"

#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Muse {

struct IdeaNote {
    double startBeat = 0.0;
    double durationBeats = 0.0;
    int pitch = 60;
    float velocity = 0.8f;
};

struct Idea {
    std::string label;           // one short line a producer reads, e.g. "Answer bar 2, a step up"
    std::vector<IdeaNote> notes; // absolute beats, all inside [barStartBeat, barStartBeat + beatsPerBar)
};

/// The bar Muse writes into: the first whole bar after the last note ends. -1 when there are no notes.
double nextBarStart(const std::vector<Harmony::HarmonyNote>& notes, double beatsPerBar);

/// Up to maxIdeas ideas for the bar starting at barStartBeat, best first. Deterministic: same input, same output.
/// Empty when there is nothing to answer (no notes before barStartBeat) or no key.
std::vector<Idea> nextBarIdeas(const std::vector<Harmony::HarmonyNote>& notes, const Harmony::HarmonyTimeline& harmony,
                               double beatsPerBar, double barStartBeat, int maxIdeas = 3);

} // namespace Muse
} // namespace Audio
} // namespace Aestra
