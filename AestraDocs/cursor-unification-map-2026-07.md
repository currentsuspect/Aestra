# Cursor / Pointer Unification — Full Map (2026-07-19)

Symptom report (owner): cursor hiding during slider/knob drags is inconsistent —
some surfaces hide (desired "infinite drag" magic), some don't; when it does
hide, the cursor sometimes reappears at the wrong position.

## The four competing cursor authorities

| # | Authority | Where | Mechanism |
|---|---|---|---|
| 1 | **OS cursor** | `AestraPlatform.h` → `PlatformWindowWin32/Linux` | `setCursorVisible(bool)` (Win32: per-thread `ShowCursor` count loops; Linux: `SDL_ShowCursor`), `setCursorPosition` (**Win32: `SetCursorPos` = SCREEN coords; Linux: `SDL_WarpMouseInWindow` = window coords**). SDL relative-mouse mode deliberately removed ("focus-dependent, unreliable"). |
| 2 | **WindowManager SVG cursor** | `AestraWindowManager` (`m_useCustomCursor{true}` by default) | OS cursor force-hidden while focused (`:438`, `:851`); SVG icon rendered every frame (`renderCustomCursor`, `:758`); skipped when bridge style == `Hidden` (`:858`); external `setCursorVisible` calls **suppressed** while custom cursor on (`:539`). |
| 3 | **TrackManagerUI custom cursor** | timeline | `isCustomCursorActive()` — overrides the WindowManager SVG (`:761-768`). A third visual cursor system. |
| 4 | **Widget hide+warp ("the magic")** | 3 independent hand-rolled copies | `NUISlider` (rotary), `UIMixerKnob`, `UIMixerFader`: `setCursorStyle(NUICursorStyle::Hidden)` on drag start → frame-delta value updates → on release warp (`setCursorPosition`) to knob-center / thumb-pos, then `setCursorStyle(Arrow)`. |

Event plumbing: `NUIPlatformBridge` stamps `event.cursorCaptured =
(m_currentCursorStyle == Hidden)` on mouse events (3 sites, `:127/171/200`);
widgets branch on it.

## Divergences → the observed symptoms

### A. "Sometimes it doesn't hide"
Only 3 widget classes implement the magic. Everything else drags with the
cursor visible: `AestraEQEditor` band/knob drags, plugin editors (Verb, Sat,
Filter, OTT, LFO editor knobs), `NUIUtilityWidgets` / `NUIMixerWidgets`
sliders, `ExportDialog` sliders, piano-roll and timeline drag gestures.
(Full per-widget census = first task of the implementation arc.)

### B. "Reappears at unwanted positions"
1. **Win32 coordinate bug**: widgets pass UI/window-space coords
   (`bounds.x/y`) into `setCursorPosition` → bridge forwards verbatim →
   `SetCursorPos()` interprets as **screen** coords. No `ClientToScreen`
   anywhere in the chain. Every warp-back on Windows lands at
   window-offset-wrong screen position. Linux is correct (window-relative).
2. **No DPI / UI-scale conversion** in the warp path on either platform.
3. **Inconsistent warp targets by design**: rotary → knob center; linear →
   computed thumb position (separate H/V formulas, `sliderRadius_` insets);
   three implementations drift independently.
4. **Restore ordering**: warp and `setCursorStyle(Arrow)` order differs across
   the three copies; if style is restored before the warp completes, the SVG
   cursor renders ≥1 frame at the drifted OS-cursor position → visible jump.
5. **Focus-loss mid-drag**: WindowManager's focus handler (`:438-440`)
   flips OS-cursor visibility on focus change regardless of an active
   Hidden-drag; widget never gets to warp back → cursor reappears wherever
   the invisible OS cursor drifted.

### C. Authority fights ("sometimes the magic dies")
- Two hiding channels: style-based (`Hidden`) vs raw `setCursorVisible`.
  Raw calls are silently ignored while the SVG cursor is active (`:539`) —
  callers using the raw channel *think* they hid something.
- Win32 `ShowCursor` is a per-thread **counter**; the impl loops to force
  state and warns about cross-thread desync (`PlatformWindowWin32.cpp:1236`).
- WindowManager decides SVG suppression by polling `getCursorStyle()` each
  frame — races widget restore by up to a frame.
- TrackManagerUI's cursor takes precedence over the SVG cursor via a
  different flag, unrelated to both channels.

## Recommended move: one owner, one API — `NUICursorService`

Single state machine owned at the bridge/window-manager seam:

```
states: Normal(style) | CapturedDrag(token, restorePolicy)
API:    beginDragCapture(DragCaptureSpec) -> CaptureToken
        updateDrag(token)                  // delta accounting (already event-driven)
        endDragCapture(token)              // warp-back + unhide, correct order
        setStyle(style) / pushStyleOverride(provider)  // SVG + timeline cursors become providers
```

- **Hide** only via the style channel; raw `setCursorVisible` becomes private
  to the service. The SVG cursor and TrackManagerUI cursor register as style
  providers instead of parallel systems — one arbitration point.
- **Warp-back** centralized: UI-space target → per-platform conversion
  (`ClientToScreen` on Win32, window-relative on SDL), DPI-aware, and always
  ordered warp-then-unhide. Restore policy enum: `KnobCenter`, `ThumbPosition`,
  `GrabOrigin` — widgets declare intent, service computes.
- **Cancellation safety**: service subscribes to focus-loss / window-leave and
  performs the same restore path, so mid-drag interruptions can't strand a
  hidden or mispositioned cursor.
- `event.cursorCaptured` stamped from service state (single source).

### Amended capture invariant (owner, 2026-07-19)

> While captured, the physical pointer may generate deltas but no longer
> participates in normal UI hover or hit-testing. `NUICursorService` owns
> hover arbitration and exposes an anchored logical cursor position. Normal
> widget hit-testing, hover transitions, tooltips, and style-provider changes
> are suspended until capture ends or cancels.

Implemented in phase 1 (bridge level):
- **Routed dispatch**: motion/button/wheel events go ONLY to the capture
  owner while captured; the rest of the tree never sees the wandering
  pointer. (`s_cursorCaptureActive` already froze `setHovered` globally;
  routing removes every other reaction class.)
- **Style-steal guard**: external `setCursorStyle` calls are ignored during
  capture (previously any widget under the hidden pointer could overwrite
  `Hidden`, which un-clipped and broke the un-hide — the "cursor lost in
  another panel" glitch). The service bypasses the guard internally.
- **Anchored logical position**: `getCursorPosition()` and wheel-event
  positions pin to the capture anchor (grab origin on the control);
  physical position feeds deltas only.
- **Focus-loss cancel**: synthetic release to the owner at the anchor, then
  cancel (unhide in place, no warp) — the safe fallback.
- **Owner-teardown guard**: widget dtors cancel an active capture so the
  bridge never routes to a dangling owner.

Deferred (phase 2.5, polish): small-rect confinement around the anchor +
per-frame recenter warp with synthetic-motion suppression. Removes delta
saturation when the hidden pointer reaches the window edge on very long
drags (pre-existing limitation). Requires moving widgets from event-position
deltas to service-provided deltas; do after phases 1-2 are validated by ear.

### Migration phases (each behavior-neutral, separately PR'd)
1. **Service + NUISlider(rotary)** migrated as reference implementation;
   Win32 ClientToScreen fix lands here (inside the service, one place).
2. **UIMixerKnob + UIMixerFader** onto the service; delete their copies.
3. **Adopt** in non-hiding drag surfaces: EQ editor first (highest-traffic
   knobs), then plugin editors, utility/mixer widget sliders, ExportDialog.
4. **Fold in** WindowManager SVG + TrackManagerUI cursors as style providers;
   remove the raw `setCursorVisible` channel from widget reach; delete the
   per-frame `getCursorStyle()` polling.

### Open questions before phase 1
- ~~Census~~ **DONE 2026-07-19** (owner confirmed symptom: "some plugin knobs
  still use the old rotary mouse"):
  - **NUISlider-based → phase-1 magic free once merged**: AestraVerbEditor,
    AestraDelayEditor, AestraCompEditor, AestraLimitEditor, NUIMixerWidgets,
    UIMixerPanel.
  - **Hand-rolled rotary drag, NO hide (phase-3 targets)**: AestraLFOEditor,
    AestraSatEditor, RumblePluginEditor, AestraEQEditor, AestraFilterEditor,
    AestraOTTEditor, AestraDriftEditor — all 7 have their own delta-drag code
    and zero Hidden/capture usage.
  - **Phase-2 targets** (own hide+warp copies to delete): UIMixerKnob,
    UIMixerFader.
  - FileBrowser drag = spatial (scroll) → keeps normal cursor per rule.
- Should linear sliders hide at all, or only rotary? (Current: NUISlider
  hides only in rotary mode; fader hides. Decide the product rule.)
- ~~Wayland: verify warp works on Hyprland~~ **RESOLVED 2026-07-19, empirical
  (SDL 2.32.70, Hyprland, probe in scratchpad):**
  - x11/XWayland: warp + hide fully work.
  - Native Wayland: warp **works** once the window is mapped (needs a
    committed buffer — bufferless windows never map) **and the pointer has
    focus**; no `SDL_VIDEO_WAYLAND_EMULATE_MOUSE_WARP` hint needed on this
    stack. Hide works.
  - Warp is a **silent no-op without pointer focus**. Design consequence:
    during a hidden infinite-drag the invisible pointer can drift OUT of the
    window → focus lost → release-warp no-ops → cursor reappears outside the
    window. This is the most plausible mechanism for the observed
    "reappears at unwanted positions" on Linux. **The service must confine
    the pointer during capture** (`SDL_SetWindowGrab` / pointer-constraints,
    or re-warp-to-center each frame while focused) so release-warp is always
    valid. Verify confinement in the real app during phase 1.
  - Aestra runs the `wayland` driver by default on this machine (SDL 2.32
    default preference; repo forces nothing).
