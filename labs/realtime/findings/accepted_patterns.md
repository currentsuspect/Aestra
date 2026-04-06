# Accepted Patterns

Fixes that measurably improved realtime scheduling and passed all gates.

## Callback-Based RT Scheduling via pthread_once

**Where**: `AestraAudio/src/Linux/RtAudioDriver.cpp` — `rtAudioCallback()`
**What**: Moved `sched_setscheduler(SCHED_FIFO)` and `mlockall()` from
`startStream()` (UI thread) to the audio callback via `pthread_once`.
The first callback invocation runs on RtAudio's internal thread, which is
the correct thread to set scheduling priority on.
**Why it works**: `pthread_once` guarantees the setup function runs exactly
once per process. Since the callback runs on the RtAudio thread, the
scheduling is set on the right thread. Without CAP_SYS_NICE, the call fails
silently and the thread continues at SCHED_OTHER (graceful degradation).
**Round**: 02 (session 002)
