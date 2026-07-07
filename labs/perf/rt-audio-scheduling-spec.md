# RT Audio Scheduling (#255) + Callback Overrun Breadcrumbs — Spec (owner-authored, 2026-07-04)

Sequence: elision (#402) → #255 RT scheduling → callback spike attribution → TrackMgrUI dynamic-pass audit.
Crackles outrank heat: audio dropouts break the trust contract of a DAW.

## #255 — surgical constraints

- Configure audio thread priority at stream/thread start ONLY
- No callback logic changes; no allocations/locks added to the callback
- Failure degrades gracefully with a clear warning — never prevents audio
- Linux: try the proper RT route first (SCHED_FIFO/rtkit), fall back cleanly
  when the user lacks permissions (rtprio limits)

Question it must answer: "When the UI/compositor gets hot, can the audio
thread still make its deadline?"

## Pass/fail (brutal)

- Loop playback under UI stress: no crackles, RT priority active when available
- WCET stops casually exceeding the buffer budget (10.67 ms @ 512/48k) under
  the same repro conditions
- If permissions prevent RT: app still runs; HUD/log makes the degraded state
  obvious (readable warning)

## Callback overrun breadcrumbs (don't overbuild)

Per overrun event, fixed-size record captured FROM the audio thread:
- max callback time, overrun count, timestamp/frame index
- buffer size / sample rate, transport state, active plugin count
- whether a graph swap / plugin activation / file preview / audition was nearby
- backend xrun/underrun signal if available

RT-safety requirement: fixed-size event, atomic/SPSC publication, no
formatting or string allocation on the audio thread. UI/log side formats.

## Caveat

RT scheduling fixes preemption-caused crackles, not intrinsically slow
callback work. If WCET spikes persist after #255, breadcrumbs become the
source of truth and the suspect shifts to "audio thread doing something
non-realtime or unexpectedly expensive."

## Result (owner-verified, 2026-07-04)

Two-act experiment on the 4 GB-target box (ulimit -r 0, no realtime group →
then `realtime-privileges` + re-login):

| | Degraded | RT active |
|---|---|---|
| HUD RT line | red, "DEGRADED (normal priority, err 1)" | green, "SCHED_FIFO active  mlock: yes" |
| WCET under UI stress | 6.49 ms (16.06 ms in the original baseline capture) | **2.55 ms** |
| Buffer budget | 10.67 ms (512 @ 48 kHz) | 10.67 ms |
| XRuns / Underruns | 0 in sample | 0 |

Pass criteria met: WCET stopped casually approaching the budget; degraded
state is loudly visible in HUD + log with the remedy named.
