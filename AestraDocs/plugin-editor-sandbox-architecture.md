# Sandbox-first plugin editor sessions

Status: **design, pre-implementation.** Owner-directed 2026-08-02. No code in this
document has been written yet beyond vendoring the CLAP SDK.

This is the architecture note for making VST3 and CLAP hosting reliable, giving
third-party plugins their *real* editor UI instead of Aestra's generic parameter
list, and containing plugin failures so they stop being Aestra's failures.

---

## 1. The invariant

> **A third-party plugin instance and its native editor always live together in
> the same host process as each other — never split across the boundary. Under
> every sandboxed topology that process is a child process, and Aestra owns only
> the editor session, the placeholder surface, and the lifecycle.**

Everything else here follows from that sentence.

The load-bearing half is "**together**", not "in a child". What is rejected is
splitting an instance from its own editor across a process boundary — DSP in the
child, view in Aestra — because that is the arrangement that forces Aestra to run
plugin GUI code on its own threads while pretending to be sandboxed.

Two things are outside this invariant, and saying so is not a weakening:

- **Aestra's own plugins** (`com.Aestrastudios.eq`, `.comp`, `.verb`, `.delay`,
  `.sampler`) run in-process and their editors are Aestra's own code, hosted
  directly by `PluginUIController`. They are first-party, they are not untrusted
  input, and there is nothing to sandbox them from. Their `openEditor()` overrides
  return `false` precisely because they never take the native-editor path.
- **The in-process compatibility escape hatch** (§6) puts a third-party instance
  and its editor in the Aestra process. That still satisfies "together"; it does
  not satisfy "sandboxed", which is exactly why it is user-selected, warned about,
  and never a silent fallback.

The tempting alternative — get editors visible quickly by attaching the plugin's
view directly inside the Aestra process — is explicitly rejected as an
architectural direction. Plugin GUI code crashes, blocks the UI thread, spawns
modal dialogs, leaks, and corrupts memory at least as readily as plugin DSP code.
Sandboxing the audio side while hosting the GUI in-process would leave the larger
hole open and would entrench the exact ownership model we would then have to
remove.

In-process hosting survives only as an **explicit compatibility escape hatch**
(§6). It is never a silent fallback.

## 2. Why this is being written before any editor code

Cross-process native editors are not hard at `IPlugView::attached()` or
`clap_plugin_gui::set_parent()`. Those calls are a day's work. They are hard at
focus routing, DPI changes, resize loops, popup menus that escape the embedded
rectangle, plugin-triggered close, and child hangs.

So the acceptance matrix in §7 is the actual specification. Anything that reaches
`attached()` and stops there is a demo, not a feature.

## 3. Verified starting state (2026-08-02)

Measured against `develop` at `128fbf48`, not recalled:

| Area | State |
| --- | --- |
| VST3 in-process | Loads. SDK present as a submodule. |
| VST3 editor | `VST3PluginInstance::openEditor()` attach block is inside `#ifdef _WIN32`. On Linux/macOS it always returns `false`. No `X11EmbedWindowID`, no `Steinberg::Linux::IRunLoop`, no `NSView`. |
| CLAP in-process | Was never compiled — `External/clap/` held only `.gitkeep`, so `AESTRA_HAS_CLAP` was OFF and `CLAPHost.cpp` was excluded from every build. Fixed by vendoring the SDK; the file compiles clean. |
| CLAP out-of-process | Works. `AestraPluginHostMain.cpp` hand-mirrors the CLAP ABI (descriptor, factory, entry, params, note-ports, state) with no SDK dependency. |
| Editor wiring | Unreached, but not absent — see the note below. `AestraUI/Widgets/PluginUIController.cpp` contains a complete `PluginEditorWindow` that calls `openEditor()`. Nothing constructs that class, so the call site is dead. |
| OOP editor | `OutOfProcessPluginInstance::openEditor()` → `return false;`. The child host contains no window, view, or display code whatsoever. |
| Crash containment | Inverted by platform. `PluginHostProcess::start()` is `#ifdef _WIN32 return false;`, so **Windows runs plugins in-process and a plugin crash takes Aestra with it.** Linux/macOS have working `fork()`-based isolation. |
| CI coverage | **Zero.** No `actions/checkout` in `ci.yml` fetches submodules — eight set `submodules: false` explicitly, and the two that omit the key (`format-check`, `static-analysis`) get the same result from the action's default. So the VST3 SDK is absent in CI, `AESTRA_HAS_VST3` is OFF, and `VST3Host.cpp` is never compiled by any lane. |

### Correction: there is existing native-window prior art

An earlier revision of this table asserted that "nothing in the application calls
`openEditor()` at all" and that "the path is unwired end to end". The conclusion
was right; the evidence was wrong, and the difference matters for planning.

What is actually there, at `128fbf48`:

- `AestraUI/Widgets/PluginUIController.cpp:630` calls `instance->openEditor(m_impl->hwnd)`
  from `PluginEditorWindow::open()`, into a real `CreateWindowExW` window, and tears
  the window down again if the call fails. Line 645 is the non-Windows branch, which
  calls `openEditor(nullptr)`.
- `PluginEditorWindow` is complete: `WindowProc`, `close()`, `bringToFront()`,
  `setPosition()`/`getPosition()`, `setOnClose()`, `getNativeHandle()`.
- **Nothing anywhere constructs it.** Its only references are its own definitions,
  its header declaration, and the `AestraUI/CMakeLists.txt` source listing. So it
  compiles and is unreachable.
- The live in-app route is different code: `PluginUIController::openPluginEditor()`
  hosts Aestra's *own* editors (`AestraCompEditor`, `AestraEQEditor`) directly and
  never goes through `IPluginInstance::openEditor()`.

Consequences for the phases below:

1. Phase 4 (embedding) is not starting from nothing on Windows. There is a window
   class, a message loop hook, and a close callback to read first — and to judge,
   not inherit. It is unreached code, which per §16 of `AGENTS.md` may still encode
   design decisions worth understanding.
2. It is also unreached code that nobody has run, so it carries no evidence of
   working. It is a reference, not a foundation.
3. `openEditor()` returning `false` is therefore load-bearing in exactly one place
   and dead in the other. Any editor-session API must decide which of the two it
   replaces. This document's answer is: neither is kept — §8 supersedes both.

Recorded here rather than silently corrected because the whole point of §3 is that
it was measured. A section claiming to be verified has to say when it was wrong.

Two consequences worth stating plainly:

1. The platform holding the editor implementation (Windows) is the platform with
   no sandbox. The platform with the durable process model (Linux) has no editor
   implementation. **The Windows code must not become the reference architecture
   merely because it exists.**
2. We are about to harden a subsystem that CI does not compile. Fixing that is
   phase 0, not a nice-to-have — otherwise every reliability claim rests on one
   developer's local Linux build.

## 4. Shape

```text
Aestra process                          Plugin host child process
┌──────────────────────────────┐        ┌────────────────────────────────┐
│ PluginEditorManager          │        │ plugin instance                │
│  └ EditorSession             │        │ native container window        │
│     ├ surface (viewport or   │◀──────▶│  ├ VST3 IPlugView              │
│     │  placeholder)          │  IPC   │  └ CLAP clap_plugin_gui        │
│     ├ presentation policy    │        │ platform run-loop / event pump │
│     └ liveness (heartbeat)   │        └────────────────────────────────┘
└──────────────────────────────┘
        Aestra never runs plugin GUI code on its own threads, and never
        owns a plugin's view. It may hold a child-created container
        handle for embedding only — see the handle contract below.
```

**The handle contract: use is not ownership.** Embedding requires Aestra to hold a
handle it did not create — `SetParent` on the child's container `HWND`, or an X11
reparent of the child's window — so an invariant reading "Aestra never holds a
plugin-owned handle" would forbid §5's own platform policy. The real distinction is
narrower:

| | Allowed |
| --- | --- |
| Receive the **container** handle the child created, and reparent it | Yes |
| Move, resize, show, hide, restack that container | Yes |
| Hold the plugin's **own view** handle (`IPlugView`, `clap_plugin_gui` surface) | **No** — the child owns it, and only the child ever touches it |
| Destroy the container, or outlive the child holding it | **No** — the child creates it and the child destroys it |
| Call any plugin GUI entry point from an Aestra thread | **No**, in every mode |

So Aestra receives a scoped, revocable capability to position a window, not
ownership of a plugin's UI. If the child dies, the container dies with it, and
Aestra's surface must already be able to survive that — which is what row 13 of the
acceptance matrix tests.

IPC verbs for the editor session, kept deliberately small: `open`, `close`,
`resize`, `focus`, `scale`, `heartbeat`. Everything richer (popup behaviour, DnD)
is a platform concern inside the child, not a protocol concern.

## 5. Presentation is platform policy, not architecture

The architecture is sandboxed everywhere. Only the *visual result* varies, and it
degrades to a floating window rather than to in-process hosting.

| Platform | Presentation | Mechanism |
| --- | --- | --- |
| Windows | Embedded | Child creates a container `HWND` and attaches the plugin view to it; Aestra embeds that container with `SetParent`. |
| X11 / XWayland | Embedded | Child creates a container window and gives the plugin its ID via `X11EmbedWindowID`; Aestra reparents that container into the editor panel. Aestra reparents the container, never the plugin's own view — same rule as Windows, see the handle contract in §4. |
| Native Wayland | **Detached** | No general, reliable cross-process embedding exists. Child owns a real top-level window; Aestra coordinates position, stacking, and lifecycle. |
| macOS | **Detached** | Cross-process `NSView` embedding is not a foundation worth building on. Child owns the window. |

Detached is a first-class outcome, not a failure path. It must look deliberate:
correct title, correct stacking relative to the main window, closes with the
session, and never orphans if the child dies.

## 6. Process topology is configurable

Aestra targets machines with 4 GB of RAM. One OS process per plugin instance is
not an acceptable default, so sandboxing must not be bound to that shape.

| Mode | Use |
| --- | --- |
| Shared host process | **Default.** Economical; one child hosts many instances. |
| Per plugin binary / per vendor | Isolate a suspect vendor without paying per instance. |
| Dedicated process | A plugin with a crash history, or one the user flags. |
| In-process | Compatibility escape hatch only (below). |

Aestra itself is always isolated from plugin code, in every mode except the
escape hatch.

**The shared default has a blast radius, and choosing it is choosing that.** One
child hosting many instances means one plugin's crash or hang takes every instance
in that process with it. That is the price of the 4 GB target and it is acceptable,
but only if the recovery behaviour is specified rather than discovered:

- **Audio keeps running.** The engine must continue producing output. Instances in
  the dead process go to bypass — passthrough for effects, silence for
  instruments — and the audio thread must never block waiting on a dead or wedged
  child. `OutOfProcessPluginInstance` already has the right shape here: a worker
  thread owns the IPC, `process()` publishes into a single-slot buffer and returns.
- **The user is told which instances were affected, by name**, not that "a plugin
  crashed". With a shared process the answer is usually several plugins across
  several tracks, and the one that actually crashed is the one worth naming.
- **Project state survives.** Parameters and saved plugin state stay authoritative
  in Aestra, so a relaunch restores rather than resets. This is why a poisoned IPC
  channel must fail the instance loudly rather than desynchronize quietly.
- **Relaunch is per-process, and it is not automatic-forever.** A child that dies
  repeatedly must stop being restarted into the same crash — the same quarantine
  reasoning as row 14, applied to hosting rather than scanning.
- **A hang is not a crash.** A wedged child holds instances that still look alive.
  Heartbeat detection (§4) is what separates the two, and the kill path must be
  bounded — the lesson from #709, where an unbounded post-terminate wait would have
  moved the hang from the plugin to Aestra's shutdown.

Rows 12 and 13 of the acceptance matrix already cover child hang and child crash
per instance. Row 21 adds the case this topology creates and those two do not: the
multi-instance loss.

**Escape hatch rules.** Surfaced as *"Run without sandboxing"*, never as a
technical term. Carries an explicit warning that a plugin failure may terminate
Aestra. Requires deliberate user action per plugin. **Never engaged silently, and
never as automatic recovery from a sandbox failure** — a sandbox that fails open
is not a sandbox.

## 7. Acceptance matrix

The specification. Each row needs a defined expected behaviour and a way to
observe it before the corresponding code is called done. Rows are ordered roughly
by how early they tend to bite.

| # | Scenario | Must hold |
| --- | --- | --- |
| 1 | Open / close / reopen | No leak of windows, handles, or child processes across repeated cycles. Reopen restores size and parameter state. |
| 2 | Keyboard focus routing | Typing reaches the plugin when its editor has focus, and Aestra's shortcuts when it does not. No key is delivered to both. |
| 3 | IME / dead keys | Composed input reaches plugin text fields; not silently dropped by the embedding layer. |
| 4 | Resize initiated by plugin | Plugin-driven resize adjusts the host surface without an oscillating feedback loop between host and child. |
| 5 | Resize initiated by user | Constraints and aspect ratios reported by the plugin are respected; the plugin is told the truth about its size. |
| 6 | DPI / scale change | Editor stays correct across scale changes and monitor moves, including mid-session. |
| 7 | Popup menus and combo boxes | Popups escaping the embedded rectangle are not clipped and do not appear behind the host window. |
| 8 | Secondary / child windows | Plugin-opened extra windows behave, stack correctly, and close with the session. |
| 9 | Modal dialogs inside the plugin | A plugin modal must not deadlock Aestra's UI thread or capture host input permanently. |
| 10 | Drag and drop | Into the editor (samples, presets) and out of it, across the process boundary. |
| 11 | Plugin-triggered close | A plugin closing its own editor is reported to the host and the session tears down cleanly. |
| 12 | Child hang | An unresponsive child is detected by heartbeat, surfaced to the user, and killable — audio must not wedge with it. |
| 13 | Child crash with editor open | Audio degrades predictably, the surface shows a clear failed state, project state survives, and the instance is recoverable. |
| 14 | Crash during scan | Never fatal; the offending plugin is quarantined rather than rescanned into the same crash. |
| 15 | Parameter sync while open | Automation and host-side edits are reflected in the editor and vice versa, without feedback loops. |
| 16 | State save/load with editor open | Save and reload while the editor is open produces identical state. Ties AGENTS.md §12. |
| 17 | Sample-rate / block-size change | Device change with an editor open does not wipe parameters (a known live defect: `initialize()` resets parameters and `prepare()` re-calls it) and does not desync the view. |
| 18 | Multiple editors at once | Several plugins with open editors across shared and dedicated topologies. |
| 19 | Session shutdown with editors open | Clean teardown; no orphaned child processes or windows. |
| 20 | Detached-mode parity | On Wayland/macOS every row above still holds for a floating child-owned window. |
| 21 | Shared-process multi-instance loss | One plugin taking down a shared child must not take down audio. Every instance in that process bypasses safely, the user is told **which** instances were lost and which plugin caused it, project state survives for all of them, and a repeatedly-crashing child stops being relaunched into the same crash (§6). |

## 8. Proposed API

Callers should be unable to tell, or care, whether the result is embedded,
detached, sandboxed, or in compatibility mode.

```cpp
EditorSessionHandle openEditor(PluginInstanceId instance,
                               EditorSurfaceId  surface,
                               EditorPresentationPolicy policy);
```

This replaces scattered `openEditor(void* parentWindow)` calls, which leak the
assumption that the host owns a window handle to hand over — precisely the
assumption the invariant in §1 forbids.

## 9. Sequencing

- **Phase 0 — make CI compile this at all.** A plugin-hosting lane that checks
  out submodules (or vendors what it needs) and builds the VST3 + CLAP host
  paths. Without it, nothing below is verifiable outside one laptop. *Vendoring
  the CLAP SDK, done, is the first half of this.*
- **Phase 1 — Windows containment.** Implement `PluginHostProcess::start()` on
  Win32 so no platform runs plugins in-process by default. Removes the inversion.
- **Phase 2 — editor session skeleton.** `PluginEditorManager`, session
  lifecycle, IPC verbs, heartbeat, and a placeholder surface. No plugin views
  yet; rows 1, 12, 13, 19 become testable.
- **Phase 3 — child-side native container.** Child creates and owns the window,
  attaches VST3 `IPlugView` / CLAP `clap_plugin_gui`, and runs the platform run
  loop (`IRunLoop` on Linux). Detached mode first — it is the honest baseline and
  the fallback for two platforms anyway.
- **Phase 4 — embedding.** X11 reparent, then Win32 `SetParent`. Rows 2–10.
- **Phase 5 — hardening.** Quarantine, crash history driving topology, recovery
  UX, and the compatibility escape hatch with its warning.

Phases 2 and 3 are where the acceptance matrix starts paying; phases 4 and 5 are
where it earns its keep.

## 10. Known defects to fold in

Existing issues that belong to this work rather than to a separate pass:

- `initialize()` resets parameters and `prepare()` re-calls it, so a device
  change wipes plugin parameters (row 17).
- Editors leak through a `setOnClose` retain cycle; a `weak_ptr` wiring helper
  was the identified fix.
- `SecOutOfProcessPluginHost` requires an absolute host path.
- CLAP note dialect and note-ports negotiation are still missing natively.

## 11. Non-goals

- No plugin bridging across architectures (32/64-bit, x86/ARM).
- No plugin-format translation (no CLAP↔VST3 shims).
- No change to internal Aestra plugins, which draw their own UI in-process by
  design and are not third-party code.
