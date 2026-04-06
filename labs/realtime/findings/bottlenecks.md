# Bottlenecks

Known scheduling characteristics before any optimization work.

## Current State

### What's Broken

1. **`RtAudioDriver::openStream()`**: Sets `RTAUDIO_MINIMIZE_LATENCY` only.
   Does NOT set `RTAUDIO_SCHEDULE_REALTIME`. Priority = 0.

2. **`RtAudioDriver::startStream()`**: Calls `pthread_setschedparam(pthread_self(), SCHED_FIFO, ...)`
   from the UI thread. This is a **bug** — it sets priority on the calling thread,
   not RtAudio's internal callback thread. The audio thread runs at SCHED_OTHER.

3. **`mlockall(MCL_CURRENT | MCL_FUTURE)`**: ✅ Works (process-wide, prevents page faults).

### RtAudio Interns (ALSA backend)

- `RTAUDIO_SCHEDULE_REALTIME` is **not honored** by the ALSA callback path.
- Only PulseAudio and JACK backends set SCHED_RR when this flag is present.
- The ALSA thread is created with default `SCHED_OTHER` policy.

### What Works

| Mechanism | Status |
|-----------|--------|
| `mlockall` | ✅ Process-wide, prevents page faults |
| `RTAUDIO_MINIMIZE_LATENCY` | ✅ Reduces buffer count |
| `RTAUDIO_SCHEDULE_REALTIME` (ALSA) | ❌ Not honored by RtAudio |
| `pthread_setschedparam` in `startStream()` | ❌ Sets wrong thread (UI, not audio) |

### Potential Fix Paths

1. **Set `RTAUDIO_SCHEDULE_REALTIME` + priority in `openStream()`**:
   Works for PulseAudio/JACK, NOT for ALSA. On most Linux desktops, ALSA
   is the default backend.

2. **Post-thread-creation priority setter**: After `openStream()`, find the
   RtAudio callback thread and set its priority. Difficult — we don't control
   thread creation and can't easily identify the callback thread.

3. **Wrapper callback with thread-local priority**: Set priority in the first
   invocation of the callback using `pthread_once` or thread-local storage.
   This would set priority on the actual RtAudio callback thread.

4. **Patch RtAudio ALSA backend**: Add `SCHED_RR` support to the ALSA
   callback thread. This requires modifying external code — forbidden by lab rules.
