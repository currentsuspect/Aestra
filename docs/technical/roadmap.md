---
**INTERNAL MEMO**

**To:** Nomad team (core contributors)

**From:** Engineering

**Date:** January 3, 2026

**Subject:** Nomad v1 Beta by Dec 2026 — Reality Check, Scope, and Execution Plan

---

### Why you’re reading this

This memo replaces the prior roadmap assumptions and sets a single goal: ship a **credible, stable Nomad v1 Beta by December 2026**.

This is not a marketing doc and not a feature wish list. It’s a practical engineering plan that optimizes for:

- shipping a complete workflow (not a demo),
- avoiding project loss/corruption,
- avoiding audio dropouts and “feels janky” UX,
- keeping the scope small enough that we actually finish.

All claims are based on repo state as of January 2026.

### Executive summary (what changes today)

- We stop aiming for broad DAW parity. Nomad wins by being **intentional and stable**, not by being comprehensive.
- The biggest Beta blockers are not “missing features”; they are **trust features**: save/load determinism, autosave/recovery, undo/redo consistency, export, and dropout resilience.
- Plugin support is a deliberate decision gate (not an open-ended roadmap). If plugins threaten stability, we cut them for Beta.
- We freeze aggressively in Q4 2026 and ship.

For the near-exhaustive execution backlog, see: `docs/technical/v1_beta_task_list.md`.

### Non-negotiable product philosophy

Nomad will not try to be Ableton, FL Studio, or Reaper.
Nomad will do one workflow exceptionally well: **pattern-based Hip-Hop music production with rock-solid stability**.

Principles:

- Scope ruthlessly: one complete workflow beats ten half-finished features.
- Stability first: users must trust the app won’t corrupt projects or drop audio.
- Performance discipline: measure, don’t guess.
- Honesty: if something can’t ship reliably, it’s out.

### What changed since the previous roadmap

The old plan implied near-term Beta and broad feature growth.
The current reality is:

- The core engine is solid, but the product is not shippable yet.
- We have progress in recording, serialization, and plugin scaffolding.
- Architectural debt remains: **Main.cpp is still 2414 lines**.
- Shipping reality is Windows-first. Linux/macOS are post-Beta unless someone owns them end-to-end.

This memo resets expectations and defines what “v1 Beta” means.

## Current State Assessment (based on repo structure)

This is not a feature list. It’s an engineering maturity assessment.

### Production-Grade (or close)

These are subsystems that appear architecturally disciplined and already have the right “shape” for shipping:

- **NomadAudio core engine**: real-time oriented architecture, explicit RT constraints, telemetry/meters, internal clocking, and extensive dedicated code.
- **Multi-tier driver concept on Windows**: explicit WASAPI shared/exclusive drivers, fallback logic, and a safety dummy driver path.
- **Pattern/playlist data model direction**: `TrackManager`, `PatternManager`, `PlaylistModel`, and snapshot mechanisms suggest a coherent core.
- **Custom UI stack**: the app is not blocked on third-party UI frameworks and appears to have stable primitives.

### Experimental / Incomplete

These exist but are not yet “product-complete,” i.e. they will break user trust if shipped without hardening:

- **Plugin hosting**: VST3 + CLAP host/scanner code exists, but product integration is unclear (routing, automation, PDC, UI embedding, crash containment).
- **Undo/Redo**: there is a `CommandHistory`, but it’s not consistently wired across the entire app UX.
- **Project management**: `ProjectSerializer` exists and saves substantial state, but gaps remain (e.g., MIDI content, “dirty state” semantics, migration/versioning strategy).
- **Recording**: recording pipeline exists (capture, file write, lane insertion), but needs validation for real devices and “don’t lose takes” reliability.
- **Cross-platform**: Linux code exists in audio, but end-to-end DAW shipping on Linux multiplies surface area (windowing, packaging, device behavior).

### Technically impressive but product-invisible

These are valuable, but if you over-invest here you risk shipping nothing:

- **Profiler/perf HUDs, telemetry, adaptive FPS, SIMD papers/experiments**.
- **Over-engineered abstractions before the workflow is frozen** (especially around plugins/AI).

### Missing but critical (for a respected Beta)

These are the Beta blockers that determine whether users can trust the app:

- **A stable project loop**: create → edit → save → reopen → identical state.
- **Crash resilience**: autosave + recovery + “don’t corrupt projects.”
- **Deterministic undo/redo across the core workflow**, not just isolated widgets.
- **Export**: offline render/bounce that matches playback.
- **Audio drop-out strategy**: underrun handling, device disconnects, graceful fallback.
- **A small but complete workflow** (defined below) that feels intentional.

## Define “Nomad v1 Beta” (December 2026)

Nomad v1 Beta is not “everything a DAW should do.” It is:

1) a complete core workflow that real musicians can finish tracks in,
2) stable enough that projects don’t get lost,
3) performant enough that it feels professional on commodity hardware.

### Platform assumptions

- **Primary / supported**: Windows 10/11 x64.
- **Linux/macOS**: explicitly **not required for v1 Beta**. Treat as post-Beta unless a contributor owns it end-to-end.

### Supported workflows (in)

**Nomad’s v1 Beta is a pattern-first production DAW**, optimized for electronic music / sample-based production:

- Create a project, set tempo, and work in a playlist/pattern workflow.
- Import audio samples and arrange them into clips/patterns.
- Basic editing: move, duplicate, split, trim (no “pro-grade audio editor,” just reliable basics).
- Record audio into the project (single-take reliability beats fancy comping).
- Mix with per-lane volume/pan/mute/solo and a master limiter/safety stage.
- Render/export a stereo mixdown.

### Beta stability expectations

- **No project corruption**: failing to load must not destroy the original file.
- **Autosave + recovery**: after a crash, user recovers work with minimal loss.
- **Real-time safety**: audio thread remains allocation-free and lock-free where intended.
- **Deterministic state**: save/reload is bit-for-bit equivalent for project structure (audio content is referenced by path + metadata).

### Performance targets (concrete)

- **Audio dropouts**: zero underruns in a 30-minute soak test on a “reasonable” Windows machine at 48 kHz / 256 frames with a moderate session.
- **UI latency**: no input stutter during playback; UI should remain responsive under load.
- **Startup + project load**: “feels fast” is not enough — measure and track.

### Intentionally out (post-Beta)

If these land, great — but they are not Beta blockers:

- AI features (Muse). Kill the temptation until the DAW is real.
- Video support, notation, collaboration, cloud sync.
- Deep time-stretch/pitch-correct workflows.
- Multi-platform shipping and installers for all OSes.
- “Everything plugin” expectations (see plugin scope below).

## Scope Control Rules (non-negotiable)

1) **Every new feature must land with a test plan and a rollback plan.**
2) **No new subsystems in Q4 2026** (Beta hardening phase).
3) **If a feature can’t be made reliable, it’s out.**
4) **Prefer fewer workflows done well** over broad capability.

## Roadmap Phases (now → Dec 2026)

Dates are approximate; the key is sequencing and freeze points.

### Phase 1 — Foundation lock (Jan–Mar 2026)

Primary goal: stop the product from being “a demo held together by `Main.cpp`.”

Deliverables:
- App structure refactor: isolate initialization, event handling, audio wiring, UI wiring (reduce blast radius of changes).
- Single “project loop” smoke test: open → edit → save → reopen → verify.
- Unified logging + diagnostics (one place to look when things go wrong).

Freeze:
- Data model API shapes (`PlaylistModel`, `PatternManager`, lane/clip IDs).

### Phase 2 — Project + undo/redo become real (Apr–Jun 2026)

Primary goal: users can trust edits.

Deliverables:
- Project format v1 spec: versioning/migrations, validation, non-destructive load failures.
- Undo/redo integrated into the main UX for core actions (clip edits, lane edits, pattern edits).
- Autosave + recovery UX (minimal, reliable).

Freeze:
- Project file schema for Beta (allow forward-compatible additions only).

### Phase 3 — Recording + export (Jul–Sep 2026)

Primary goal: finishing tracks becomes possible.

Deliverables:
- Recording workflow reliability: arming, input selection, monitoring, latency compensation behavior.
- Render/export: offline bounce that matches playback.
- Session stress tests: long playback, repeated record takes, device switch, device disconnect.

Freeze:
- Recording file management rules (where files live, naming, relinking behavior).

### Phase 4 — Plugin scope decision (Sep 2026)

This is a single decision gate, not an open-ended plugin roadmap.

Option A (recommended if bandwidth is tight):
- **Ship Beta with zero third-party plugin support**, but provide a small internal “Arsenal” set of instruments/effects that is stable.

Option B (only if it can be made robust):
- **Ship a minimal plugin MVP**:
	- scan + load VST3/CLAP,
	- process audio,
	- basic parameter list,
	- watchdog/circuit-breaker behavior on hangs,
	- no promises about every plugin UI.

Hard rule:
- If plugins threaten stability, **cut them**. A stable Beta beats “supports 10k plugins” every time.

### Phase 5 — UX hardening + Beta freeze (Oct–Nov 2026)

Primary goal: stop changing what the app is, and make it reliable.

Deliverables:
- Bug triage discipline: severity definitions + weekly burn-down.
- Performance budgets enforced (audio callback time, UI frame time).
- Onboarding docs: “how to finish a track” with the supported workflow.

Freeze:
- No new features. Only fixes.

### Phase 6 — v1 Beta release (Dec 2026)

Deliverables:
- Signed Windows build, basic installer.
- Crash logs + user diagnostics collection (opt-in if required by policy).
- Beta feedback loop: a small test cohort with a clear reproduction template.

## Industry Reality Check (how to win without competing everywhere)

### Where Nomad can be competitive

- **Pattern-first workflow** (closest to FL Studio conceptually) with “fast hands” UI.
- **Low-latency + stability discipline** (Reaper-level seriousness is the bar, not Ableton’s features).
- **A coherent internal architecture** that doesn’t fight you when you debug performance.

### Where Nomad should not try to compete (for Beta)

- Ableton: deep warping workflows, mature device ecosystem, performance/live features.
- Bitwig: modulators + hybrid device graph UX.
- FL Studio: decades of UI polish and plugin ecosystem expectations.
- Reaper: breadth + stability + scripting ecosystem.

Nomad’s edge is not “more features.” It’s:
- fewer workflows,
- extremely stable,
- extremely intentional,
- and fast for pattern-based creation.

## Hard Truths (what to stop / double down on)

Stop:
- Chasing AI integration before the DAW is shippable.
- Expanding platforms before Windows is boringly stable.
- Over-investing in performance micro-optimizations while core workflows are still changing.

Double down:
- Project loop reliability (save/load/undo/autosave).
- Recording + export (finishing tracks).
- Safety paths (fallback drivers, crash recovery, non-corrupting storage).

Kill (if they threaten the schedule):
- Full plugin GUI embedding across all plugins.
- AU support.
- “Pro audio editor” features.

---

Last updated: January 2026
