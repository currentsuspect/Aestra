# Performance harness (SPEC-028 R0)

Measure what the owner feels: heat and waiting. Two tools, both for any desktop app so Aestra is compared with the
others on the same footing.

| Tool | Question | Output |
|---|---|---|
| `idle_bench.py` | What does the app cost while nothing is happening? | JSON: time to first window, idle CPU, RSS, threads, compositor CPU, box load |
| `AESTRA_FRAME_STATS=2` (in the app) | Why does it redraw, and how much of the window actually changed? | `[FrameWhy]`, `[DirtyWho]`, `[DirtyAt]`, `[RenderSelf]`, `[Phase]`, `[DirtyArea]` once a second |

## How to run it fairly

1. Quiet box: `cut -d' ' -f1 /proc/loadavg` under about 3. The script reports load at both ends.
2. Same display for every app you compare. Real session (`--display :1` through XWayland) for GPU numbers; a private
   `Xvfb :NN -screen 0 1366x768x24` for software GL, where drawing cost shows up as `llvmpipe` threads.
3. Wiped state (the script does this) and no network (the script does this).
4. Two runs each, and compare both. One number is an anecdote.

## Reading the in-app report

`AESTRA_FRAME_STATS=2 build-linux/bin/Aestra` logs, once a second:

- `[FrameWhy]`: why each frame was presented (`dirty`, `input`, `heartbeat`, ...). At rest it should be `heartbeat` only.
- `[DirtyWho]` / `[DirtyAt]`: the component types and positions that START invalidations, per second. The top entry is
  what keeps a still window redrawing (it found the mixer navigator and the DSP number).
- `[DirtyArea]` (SPEC-028 R0): the average and worst share of the window those invalidations covered, and how many
  rectangles. The renderer still draws 100% of the window per presented frame; this is what a retained frame would
  need to draw.

## Known baseline (15 Nov 2026, X240 / HD 4000)

See `SPEC-028 Retained Frame` in the vault for the table.
