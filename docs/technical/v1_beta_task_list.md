# Aestra v1 Beta Task List (Near-Exhaustive)

Date: January 3, 2026

This is the execution backlog for shipping **Aestra v1 Beta by December 2026**. It is intentionally biased toward **trust + finishability** over feature breadth.

## Current repo status (quick reality check)

This section is here to prevent us from re-building things that already exist.

- **Project save/load exists**: `ProjectSerializer` is implemented in `Source/ProjectSerializer.{h,cpp}`.
	- Known gaps (still P0): non-destructive load, validation, and migration policy.
- **Autosave exists (currently app-level)**: there is autosave plumbing inside `Source/Main.cpp` (timer + `autosave.aes` path).
	- Known gaps (still P0): non-destructive writes, rotation, recovery UX, and tying autosave to actual “dirty” edits.
- **Undo/redo exists (partially wired)**: `CommandHistory` is present and used for some clip operations in `Source/TrackManagerUI.cpp`.
	- Known gaps (still P0): consistent integration across core edits + atomic transactions.
- **Recording is present (at least partially)**: UI wiring exists (`TransportBar` → `AestraContent` → `TrackManager::record()`), and recording waveform snapshot rendering exists in `Source/TrackUIComponent.cpp`.
	- Known gaps (still P0): end-to-end reliability/stress tests, file management rules, device edge cases.
- **Export/offline render**: not confirmed in code yet (still treated as missing until we find the implementation).

New since the early January baseline:

- **Project roundtrip smoke test added**: `Tests/ProjectRoundTripTest.cpp` + CMake target `ProjectRoundTripTest`.
- **Internal plugin persistence path added**: units can now serialize plugin ID/state and restore internal instruments on project load.
- **Internal plugin discovery path added**: built-in plugins now participate in normal scanner/manager lookup.
- **Headless audible Arsenal proof added**: `RumbleArsenalAudibleTest` proves an internal Arsenal instrument can be driven by pattern playback and render audible output.

## How to use this list

- Treat this as the source of truth for Beta scope.
- Each task should be implemented with a short test plan (manual + automated where feasible) and a rollback plan.
- Keep tasks small enough to ship continuously; merge work behind feature flags if needed.
- If a task jeopardizes stability or the schedule, demote/cut it rather than letting it block Beta.

## Priority definitions

- **P0 — Beta blocker**: must be done for a respected Beta.
- **P1 — Strongly recommended**: expected by users, but can slip if needed.
- **P2 — Nice to have**: only if time remains after P0/P1 are solid.

## Phase map (for sequencing)

- **Phase 1 (Jan–Mar 2026):** Foundation lock (refactor risk reduction, smoke tests, logging)
- **Phase 2 (Apr–Jun 2026):** Project + undo/redo + autosave/recovery
- **Phase 3 (Jul–Sep 2026):** Recording + export + stress tests
- **Phase 4 (Sep 2026):** Plugin decision gate (cut or minimal MVP)
- **Phase 5 (Oct–Nov 2026):** Hardening + freeze
- **Phase 6 (Dec 2026):** Beta release (installer, diagnostics, feedback loop)

---

## EPIC A — Program management + “Definition of Beta” (P0)

**Done means:** everyone can answer what’s in/out, and we have a stable weekly cadence that converges on shipping.

- [P0][A-001] Define a single Beta workflow statement (pattern-first electronic production) and keep it consistent across docs.
- [P0][A-002] Define Beta exit criteria (see EPIC K for test gates). Publish in a single place.
- [P0][A-003] Weekly triage ritual: severity definitions, owner assignment, and burndown.
- [P0][A-004] Q4 2026 feature freeze rule: no new subsystems, only fixes.
- [P0][A-005] “Cut list” policy: pre-approved features to cut if schedule slips (plugins UI embedding, AU support, deep audio editing, AI).

---

## EPIC B — Architectural risk reduction (Main.cpp blast radius) (P0)

**Goal:** reduce the probability that every change breaks everything.

**Done means:** major subsystems can be modified/tested in isolation; startup/audio/UI wiring is separated and readable.

- [P0][B-001] Split application bootstrap into modules (init, windowing, audio init, UI init, services).
- [P0][B-002] Introduce explicit app “lifecycle state” (startup, running, shutting down) to avoid partial-init bugs.
- [P0][B-003] Move global singletons behind a stable service locator / context object (minimal, not over-abstracted).
- [P0][B-004] Standardize threading model documentation (audio thread, UI thread, worker threads).
- [P0][B-005] Add compile-time + runtime checks for audio-thread constraints (no allocations/locks in callback where required).
- [P1][B-006] Add feature flags for risky subsystems (plugins, experimental renderers, etc.).

---

## EPIC C — Project loop reliability (create → edit → save → reopen → identical) (P0)

**Done means:** projects round-trip correctly, failures are non-destructive, and we have migration/versioning rules.

- [P0][C-001] Define project schema versioning policy (semantic version field + migration strategy).
- [P0][C-002] Implement non-destructive load: load into temporary structure; only commit if validation passes.
- [P0][C-003] Validate project files on load (required fields, type checks, ID uniqueness, bounds checks).
- [P0][C-004] Ensure referenced audio assets are handled safely (missing files, relink behavior, clear error messages).
- [P0][C-005] Dirty-state semantics: define what marks project dirty; ensure UI reflects unsaved changes.
- [P0][C-006] “Save As…” + template location rules (project folder conventions).
- [P0][C-007] Project path portability rules (relative paths within project folder; absolute allowed but flagged).
- [P0][C-008] Add a minimal project smoke test runner (headless if possible): create project, add clip, save, reload, compare.
- [P1][C-009] Project backup rotation on manual save (keep last N).
- [P1][C-010] Project import/export compatibility note (document what is stable for Beta).

---

## EPIC D — Autosave + crash recovery (P0)

**Done means:** a crash does not destroy work; recovery flow is predictable and safe.

- [P0][D-001] Implement autosave manager with “dirty” marking on edits (thread-safe, minimal overhead).
- [P0][D-002] Autosave file format/location spec (per-project autosave file(s), timestamped rotation).
- [P0][D-003] Recovery UX: on startup/open, detect autosave and offer recover/discard.
- [P0][D-004] Autosave is non-destructive (never overwrites the canonical project unless user confirms).
- [P0][D-005] Crash-safe writes: write to temp file then atomic rename/replace.
- [P1][D-006] Recovery log record (what was recovered, from when).

---

## EPIC E — Undo/redo that users can trust (P0)

**Done means:** core edits are undoable, atomic, deterministic, and survive common sequences.

- [P0][E-001] Define the canonical command model for core edits (clip ops, lane ops, pattern ops, mixer ops).
- [P0][E-002] Ensure command history is integrated across the main UX paths (not “some widgets”).
- [P0][E-003] Transactions/atomic grouping: multi-step edits become a single undo step.
- [P0][E-004] Undo/redo invalidation rules (e.g., when project is reloaded).
- [P0][E-005] Test plan: scripted sequences of edits + undo/redo replay producing identical state.
- [P1][E-006] Merge/squash trivial operations (drag moves) to avoid 500-step histories.

---

## EPIC F — Core editing workflow (clips, lanes, patterns) (P0)

**Done means:** users can actually arrange a track with reliable basic editing.

- [P0][F-001] Clip hit testing correctness (selection is reliable at all zoom levels).
- [P0][F-002] Move/drag clips with snapping (grid snap on/off, appropriate rounding).
- [P0][F-003] Duplicate workflow (copy/paste and/or alt-drag).
- [P0][F-004] Split clips reliably at playhead/cursor; undoable.
- [P0][F-005] Trim clip edges; undoable.
- [P0][F-006] Multi-select (range select and ctrl-select); bulk operations.
- [P0][F-007] Delete behavior (safe; can’t delete “invisibly”; undoable).
- [P0][F-008] Pattern/playlist loop mechanics: predictable looping, clip repetition, and timeline alignment.
- [P1][F-009] Basic fades (optional, if already supported in engine) or at minimum click-free trimming.
- [P1][F-010] Basic clip gain control (per-clip gain) and normalization option (non-destructive).

---

## EPIC G — Mixing essentials (P0)

**Done means:** basic per-track/per-lane mix controls exist and export matches playback.

- [P0][G-001] Per-lane or per-track volume control (clear mapping to engine).
- [P0][G-002] Pan control.
- [P0][G-003] Mute/solo with correct precedence (solo overrides mute rules).
- [P0][G-004] Master fader and a safety limiter/clip guard stage for Beta.
- [P0][G-005] Metering: stable RMS/peak meters with smoothing; no UI stutter.
- [P1][G-006] Track naming + color persistence.
- [P1][G-007] Basic routing policy documented (even if it’s “no sends for Beta”).

---

## EPIC H — Transport + timing (tempo/metronome/latency semantics) (P0)

**Done means:** the DAW’s timing is credible; tempo is not hardcoded; recording/playback aligns.

- [P0][H-001] Tempo control UI + internal tempo source is authoritative.
- [P0][H-002] Time signature representation (even if Beta is limited to 4/4, be explicit).
- [P0][H-003] Metronome (playback + recording), with gain control.
- [P0][H-004] Pre-roll count-in option for recording.
- [P0][H-005] Loop region UX (set loop in/out, toggle loop).
- [P1][H-006] Tap tempo (optional).

---

## EPIC I — Recording reliability (P0)

**Done means:** users can record takes without losing audio, and recorded items land correctly.

- [P0][I-001] Input device selection UX (per project/global policy).
- [P0][I-002] Track/lane arming + clear armed state UI.
- [P0][I-003] Monitoring policy (software monitoring on/off; avoid feedback).
- [P0][I-004] Record start/stop correctness (file handles, buffer flushing, no truncated files).
- [P0][I-005] File naming + storage rules (inside project folder, stable relink).
- [P0][I-006] Recorded clip placement alignment (latency semantics documented; consistent result).
- [P0][I-007] Recording stress test: many takes, long takes, disk under load.
- [P1][I-008] Basic punch-in/out (optional; only if stable).

---

## EPIC J — Export / offline render that matches playback (P0)

**Done means:** users can finish a track and trust the output.

- [P0][J-001] Define render scope: full song vs loop region, tail handling.
- [P0][J-002] Implement offline render/bounce pipeline.
- [P0][J-003] Ensure export uses the same signal path as realtime playback (no “sounds different” bugs).
- [P0][J-004] Render to WAV (minimum). Sample rate/bit depth selection policy.
- [P0][J-005] Dither policy (if 16-bit is supported) or explicitly omit for Beta.
- [P0][J-006] Render verification tests: compare realtime capture vs offline render (within tolerance).
- [P1][J-007] Render progress UI + cancel.

---

## EPIC K — Audio device resilience + dropout strategy (P0)

**Done means:** device changes and underruns don’t hard-crash the app; failures degrade gracefully.

- [P0][K-001] Underrun detection + reporting (counters + logs + user-visible indicator).
- [P0][K-002] Device disconnect handling (USB interface unplug, default device change).
- [P0][K-003] Graceful fallback policy (exclusive → shared → fallback backend → dummy).
- [P0][K-004] Audio thread safety audits (no locks, no allocations in RT path).
- [P0][K-005] Soak tests: 30–60 minute playback under load, repeated start/stop.
- [P1][K-006] Latency reporting (input/output reported to UI).

---

## EPIC L — UI responsiveness + workflow ergonomics (P0)

**Done means:** UI does not stutter under audio load; common actions are fast.

- [P0][L-001] Performance budgets (audio callback time, UI frame time) and metrics tracking.
- [P0][L-002] Input latency: fix “click stutter” and high-frequency UI events.
- [P0][L-003] Keyboard shortcuts for the supported workflow (transport, split, delete, duplicate, undo/redo).
- [P0][L-004] Context menus for core objects (clips/tracks) with minimal, reliable actions.
- [P0][L-005] Selection model consistency (one mental model across timeline/pattern UI).
- [P1][L-006] Onboarding flow (open project, import sample, place clip, export) surfaced in-app or in docs.

---

## EPIC M — Plugin decision gate (Sep 2026) (P0 decision)

**Goal:** decide a stable Beta posture: **no plugins** or **minimal plugins**.

**Done means:** a single, explicit decision with clear constraints.

Option A (recommended if stability is threatened):

- [P0][M-A01] Beta ships with no third-party plugins.
- [P0][M-A02] Internal Arsenal instrument/effect set is stable enough to finish tracks.

Option B (only if robust):

- [P0][M-B01] Scanner is hardened (timeouts/watchdog; no UI lockups).
- [P0][M-B02] Plugins can process audio in the graph.
- [P0][M-B03] Basic parameter control list is usable; automation can be postponed unless already stable.
- [P0][M-B04] Crash containment strategy documented (blacklist, safe mode, recovery on crash).
- [P1][M-B05] Plugin UI embedding only if it is stable; otherwise parameter-only UI.

Hard rule:

- If plugin support increases crash rate or breaks determinism, cut it for Beta.

---

## EPIC N — Testing + QA gates (P0)

**Done means:** we have repeatable ways to catch regressions before users do.

- [P0][N-001] Define Beta test matrix (hardware devices, sample rates, buffer sizes).
- [P0][N-002] Add automated smoke tests where feasible (project save/load roundtrip, command history replay).
- [P0][N-003] Add long-run soak tests (manual protocol is acceptable if automated is hard).
- [P0][N-004] Crash repro template + log collection instructions.
- [P0][N-005] “Golden session” test projects stored in repo or as downloadable fixtures (small, legal).

---

## EPIC O — Build, packaging, and release readiness (P0)

**Done means:** we can produce a signed Windows build, with basic installer, and users can report problems.

- [P0][O-001] One-command Windows release build instructions are correct and current.
- [P0][O-002] Create a basic installer pipeline (Inno Setup exists; ensure it’s wired and reproducible).
- [P0][O-003] App versioning policy (semantic versions, build metadata).
- [P0][O-004] Crash log collection strategy (local logs + optional opt-in upload if policy allows).
- [P0][O-005] “Safe mode” startup option (disable risky subsystems like plugins).
- [P1][O-006] Auto-update is out for Beta unless trivial; document manual update path.

---

## EPIC P — Documentation for Beta users (P0)

**Done means:** users can finish a track and provide actionable bug reports.

- [P0][P-001] “Finish a track in Aestra” guide (the supported workflow).
- [P0][P-002] Troubleshooting: audio device setup, buffer size guidance, known limitations.
- [P0][P-003] Crash/reporting guide (where logs are, what to attach).
- [P1][P-004] Shortcuts reference sheet.

---

## EPIC Q — Compliance, licensing, and third-party notices (P0)

**Done means:** the release is legally/operationally safe to distribute.

- [P0][Q-001] Confirm third-party license attributions are complete (NOTICE / LICENSING.md).
- [P0][Q-002] Confirm plugin SDK licensing constraints are respected (VST3, CLAP).
- [P0][Q-003] Ensure installer bundles the right notices.

---

## EPIC R — Things explicitly out of scope for v1 Beta (guardrails)

These are common “project killers” if we accidentally accept them into the Beta scope.

- [R-001] Collaboration/cloud sync/CRDT features.
- [R-002] Multi-backend renderer migration (Vulkan/Metal/D3D12). Keep renderer stable for Beta.
- [R-003] AI features.
- [R-004] Deep audio editing suite (spectral editor, advanced warping, full comping workflows).
- [R-005] Full multi-platform packaging (Linux/macOS) unless a contributor owns it end-to-end.

---

## Suggested next step (this week)

If we start immediately, the best first slice is:

1) EPIC B (reduce blast radius) + EPIC C (project roundtrip smoke test)
2) instrument logging/diagnostics enough that failures are actionable

That creates the foundation to implement everything else without drowning in regressions.
