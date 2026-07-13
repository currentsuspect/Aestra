# Idle Frame Elision — Design Spec (owner-authored, 2026-07-04)

Structural power/CPU optimization for the 4 GB target: stop burning render/swap
cycles when nothing changes. Distinct from local hot-path work (e.g. the browser
hover fix in #401) — this is the architectural win.

## Core model

NOT "skip when transport stopped". The gate is:

    dirty == false && realtime_visuals == false && input_recent == false

Render happens only on:
- invalidate()
- input event
- timer heartbeat
- async model update
- transport/meter activity
- window expose/resize

## Conservative idle definition (v1 — ALL must hold to skip a frame)

- transport stopped
- no mouse movement / hover changes
- no active drag
- no text caret blink due
- no tooltip animation/state change
- no meters/waveforms requiring live update
- no modal/dialog animation
- no pending invalidation
- no profiler HUD open (HUD forces normal/adaptive rendering — the profiler
  needs fresh samples or it becomes misleading)

## Heartbeat

2–5 fps while deeply idle — keeps the window alive without burning CPU.

## Known trap

"Transport stopped" != "nothing changes": hover, typing, caret blink, menu
state, tooltips, resize, browser updates, async scan completions, plugin load
status all mutate UI while stopped. Every one of these must count as activity.

## Acceptance (three numbers after the PR lands)

1. Idle, no HUD: render/swap nearly disappears except heartbeat frames.
2. Idle, HUD open: ledger balances via HUD_Render + Renderer_Flush (normal rendering forced).
3. Active hover/scroll: browser stays near ~5 ms; scroll rebuild spikes clearly
   separated from hover cost.

## Roster after this

1. TrackMgrUI dynamic-pass audit — the browser bug pattern is contagious:
   state that should be cached recomputed in per-frame paint because it was
   "cheap enough" when first written. Suspects: track lanes, clips, row
   backgrounds, grid subdivisions, text labels, selection geometry, shadows.
2. Scroll-friendly browser cache (texture-offset scrolling) — specialized,
   after the structural win.
