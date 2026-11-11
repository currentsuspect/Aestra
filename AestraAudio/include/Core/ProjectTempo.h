// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

namespace Aestra {
namespace Audio {

class AudioEngine;
class TrackManager;

/**
 * @brief Change the project tempo everywhere it is kept, holding the playhead on its beat.
 *
 * The tempo lives in the engine (metronome), the playlist, the scheduler's clock and the
 * transport field, and every playhead position is held in samples or seconds. A tempo edit
 * that only swapped the number left those positions where they were in time, so the
 * playhead jumped to another bar. This rescales the engine playhead (in the same audio block
 * as the switch while playing) and the TrackManager's playhead and play-start position.
 * Control thread only. Plugin tempo and the transport field stay with the caller.
 */
void changeProjectTempo(AudioEngine& engine, TrackManager* trackManager, float bpm);

} // namespace Audio
} // namespace Aestra
