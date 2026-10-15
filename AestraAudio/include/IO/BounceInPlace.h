// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/ClipInstance.h"

#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

class AudioEngine;
class TrackManager;

/** @brief What Bounce in Place did, or why it refused. A refusal changes nothing. */
struct BounceInPlaceResult {
    bool ok = false;
    std::string error;   ///< one sentence the user can act on; empty on success
    ClipInstanceID clip; ///< the clip that replaced the selection
};

/**
 * @brief Bounce the selected audio clips in place (V8-W5).
 *
 * Clips on ONE track: renders that track's output over their span through the
 * live engine (inserts, insert-parameter automation, delay compensation; before
 * the fader, no master stage), then replaces the clips with the result on that
 * track as one undo step, bypassing the inserts and muting their automation so
 * nothing is applied twice.
 *
 * Clips whose sounds play on SEVERAL tracks, or straight to Master (a drum
 * pattern usually does): renders each track after its fader and pan, plus what
 * goes to Master directly, sums them, and places one clip on Master. It sounds
 * the same and passes through none of those tracks, so nothing is bypassed.
 *
 * Refuses, with a reason, rather than guess: clips that are neither audio nor
 * patterns, or that share their span with unselected clips playing through the
 * same tracks (those would be baked into the result and then play a second time).
 *
 * Blocking: call from a control/worker thread, never the audio callback.
 */
BounceInPlaceResult bounceClipsInPlace(AudioEngine& engine, TrackManager& trackManager,
                                       const std::vector<ClipInstanceID>& clipIds);

} // namespace Audio
} // namespace Aestra
