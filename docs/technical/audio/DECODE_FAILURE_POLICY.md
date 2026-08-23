# Audio Decode Failure Policy

Status: current behavior (timeline imports, project load, previews)

## Summary

Full-length timeline decodes are **strict**: a file whose duration cannot be
determined up front is rejected. Preview decodes are **tolerant**: they fall
back to a capped read of at most `maxSeconds`. This asymmetry is intentional.

## The asymmetry

| Path | Function | Unknown-length stream behavior |
| --- | --- | --- |
| Timeline import / project load | `decodeAudioFile` → `loadWithMiniAudio` (`AestraAudio/src/IO/MiniAudioDecoder.cpp`) | Hard fail: requires `ma_decoder_get_length_in_pcm_frames` to succeed with a non-zero length before any samples are read |
| Browser/preview playback | `decodeAudioPreview` (`AestraAudio/src/IO/MiniAudioDecoder.cpp`) | Tolerant fallback: estimates `totalFrames` from `maxSeconds * sampleRate` when the length query fails |

## Rationale

Timeline clips carry real durations, loop regions, and export/bounce parity
obligations. A clip whose buffer was decoded "until EOF" from a stream with no
declared length can disagree with its metadata: clip extents drift against
beats, exports truncate or pad unpredictably, and re-decodes on another machine
can produce different lengths. For timeline content we choose full-length
fidelity over import convenience — if a container cannot state its length, we
do not guess it.

Previews have none of those obligations. They only need bounded audio for
auditioning, so the capped tolerant read is correct there and costs nothing
downstream.

## Failure handling downstream

Because strict decodes can legitimately fail (unknown-length MP3s, corrupt
headers), failure must not poison project state:

* Project load (`Source/Core/ProjectSerializer.cpp`, source-loading loop) does
  **not** install any buffer when a decode fails. The `ClipSource` stays
  genuinely unready (`isReady() == false`, no raw buffer), which renders as
  silence via the draw path's `!isReady()` early-out and remains eligible for a
  retry on a later load or re-import.
* Any successful attachment that flips a source to ready bumps
  `SourceManager`'s revision (see `ClipSource::setBuffer` /
  `SourceManager::attachBuffer`), so the timeline's waveform-cache sweep picks
  the newly decoded audio up without needing an unrelated event.

Guarded by `Tests/AestraAudio/SourceReadinessInvariantTest.cpp`.
