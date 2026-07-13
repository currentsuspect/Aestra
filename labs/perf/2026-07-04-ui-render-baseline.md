# UI Render Baseline — 2026-07-04

Machine: owner dev box (4 GB RAM target spec). Build: Release, develop @ af354768 + HUD fix.
State captured: **idle timeline view** (no playback, no input), browser open, 11 tracks, HUD visible.
Source: UnifiedHUD (View → Performance Stats / F12), owner screenshot.

## Numbers

| Metric | Value |
|---|---|
| FPS / target | 29.1 / 30 (Mode: Auto, Active: NO — idle throttle engaged) |
| Frame time | 9.78 ms |
| Audio engine load | 7% (CB 0.769 ms / 10.67 ms budget, WCET 5.24 ms, XRuns 0, 512 @ 48 kHz) |

### Zone hotspots (per frame, idle)

| Zone | ms | share |
|---|---|---|
| Render_Prep (whole window render) | 8.77 | 56% |
| FileBrowser_Render | 3.07 | 19% |
| TrackMgrUI_Render | 3.04 | 19% |
| Transport_Render | 0.60 | 4% |
| TitleBar_Render | 0.17 | 1% |
| TrackUI_Grid | 0.09 | 1% |
| UI_Update | 0.00 | 0% |

### Broken stats observed
- HUD "RENDERING: Draws: 0 Widgets: 0 Tris: 0" — draw-call/tri counters not wired (relates to #269's vestigial batch-stats plumbing).

## Reading

- The app burns ~9.8 ms of CPU+GPU work per frame at 30 fps **while completely idle** — nothing on screen changes, yet the full window redraws ~30×/s (~29% of one core on this box, worse on the 4 GB target's weaker iGPUs).
- `TrackUI_Grid` at 0.09 ms confirms the playlist FBO cache works — the grid costs nothing.
- But `TrackMgrUI_Render` still spends 3.04 ms/frame outside the cache (dynamic pass: control strips, meters, playhead, toolbar) even with no playback.
- `FileBrowser_Render` at 3.07 ms/frame has **no caching at all** — it repaints the whole browser tree every frame.

## Optimization roster (ordered)

1. **Idle frame elision** — when no input, no playback, no animations, and no dirty state: skip rendering entirely (present nothing / reuse last frame) with a low heartbeat (e.g. 2–5 fps) for safety. Expected: idle CPU/GPU → near zero. Infra exists (AdaptiveFPS idle detection, dirty-region manager).
2. **FileBrowser FBO cache** — same pattern as the playlist cache; browser content changes only on scroll/hover/selection. Expected: −3 ms/frame whenever the browser is visible.
3. **TrackMgrUI dynamic-pass audit** — 3 ms/frame idle is high for "meters + strips"; profile what inside the dynamic pass costs when meters are static; consider caching the control strip and only live-drawing meters/playhead.
4. **Fix HUD draw-call/tri counters** so future work has render-side numbers (ties into #269 rescope).

Still to capture (owner): playback-with-meters state, browser-scroll state, process RSS.

## Addendum — active state (same session, after FPS governor fix)

Mouse-active + HUD open: **53.7 FPS against the 60 target, 14.12 ms frame** (Active: YES — governor wiring works; boost verified on mouse move and sustained through playback).

| Zone | ms (active) | vs idle |
|---|---|---|
| Render_Prep | 17.78 | 8.77 |
| FileBrowser_Render | **11.46** | 3.07 |
| TrackMgrUI_Render | 3.79 | 3.04 |

Key insight: the uncached FileBrowser costs **11.5 ms/frame under mouse activity** (hover-state repaints) — alone it nearly consumes the 16.6 ms budget at 60 FPS, which is why the target isn't reached. Roster item 2 (FileBrowser FBO cache) is promoted to the top payoff for perceived smoothness; item 1 (idle elision) remains the top battery/CPU win.

## Addendum 2 — idle frame elision result (owner-verified)

With the elision gate (perf/idle-frame-elision): **idle CPU ~2%** (from ~30% of
a core), instant wake on input/dirty/transport, HUD-open forces normal
rendering as specified. Remaining active-state heat + occasional audio
crackles are the next program targets — note the active-state capture earlier
showed audio WCET 16.06 ms against a 10.67 ms buffer budget, which is where
crackles live.
