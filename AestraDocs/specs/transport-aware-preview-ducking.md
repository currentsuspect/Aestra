# Transport-Aware Preview Ducking
**Status:** TODO
**Priority:** Medium
**Created:** 2026-04-14

## Problem
When the transport is playing and a file preview plays simultaneously, the combined output can be jarring — especially at unity gain preview volume.

## Solution
Preview should duck the transport when both are active:

- **Transport playing + preview starts:** Duck transport by ~6 dB, play preview at unity (0 dB). The preview is the focus — the user is auditioning a sample against the mix.
- **Preview ends:** Restore transport to full volume.
- **Transport stopped + preview plays:** Full unity preview, no ducking needed.
- **Transport plays, no preview active:** Full transport volume (normal behavior).

## Implementation
- PreviewEngine already has `m_activeVoice` tracking — check if a voice is playing
- AudioEngine has transport state (`isPlaying()`)
- The duck should be a smooth fade (~50ms), not a hard cut
- Apply duck to the main mix output (master bus), not individual channels
- Use a `std::atomic<float> m_previewDuckGain` on the master bus, set by PreviewEngine

## Files
- `AestraAudio/src/Playback/PreviewEngine.cpp` — notify engine when preview starts/ends
- `AestraAudio/src/Core/AudioEngine.cpp` — apply duck gain to master output
- `AestraAudio/include/Core/AudioEngine.h` — add `m_previewDuckGain` atomic

## Verification
1. Load a project with clips on the timeline
2. Press play (transport running)
3. Click a file in the browser to preview
4. Transport should duck (~6 dB lower), preview plays at full volume
5. Preview ends → transport restores to full volume
6. Stop transport → preview plays at full volume with no ducking
