# Aestra Diagnostics — Diagnostic Bridge Design Note

**Status:** Design note — **no code changed, not approved for implementation.** Constrains C++ that has not been written yet.
**Scope:** Runtime instrumentation for debugging Aestra: observation channels, actuation channels, authority model, realtime isolation, reproduction fixtures, and the public/private runtime boundary.
**Date:** 2026-07-27
**Direction:** Dylan Makori
**Drafted by:** Claude Opus 5
**Companion:** [`AUDIT_RT_Safety_2026Q2.md`](AUDIT_RT_Safety_2026Q2.md), [`AUDIT_Threading_Concurrency_2026Q2.md`](AUDIT_Threading_Concurrency_2026Q2.md), [`../product/Muse-AI-Spec.md`](../product/Muse-AI-Spec.md), and the public agent protocol at <https://aestra.studio/.well-known/agent-skills/index.json>

> **Aestra Diagnostics reuses MuseAgent transport and provider infrastructure — the socket, the JSONL verb protocol, the agent loop — but it is not a Muse creative capability and must never be presented as one.** Muse is the creative assistant; Aestra Diagnostics is the repair and investigation subsystem. They share plumbing, not purpose. That distinction is in the filename deliberately, so it does not need re-explaining for the next year.

> **This note does not amend `Muse-AI-Spec.md`.** That amendment should happen only after this design settles, so the spec can reference a concrete authority model instead of vaguely carving out an exception.

> **Blocking prerequisite found during drafting: Aestra has no stable UI identity today.** See §5.1. This moves work into a new Phase 0 and is the largest dependency in the plan.

---

## 1. Purpose

Aestra publishes an agent protocol (`aestra-agent-protocol/v1`) telling outside coding agents how to investigate Aestra defects. Today that protocol can only reach source code, build output, logs and project files. It cannot reach **the running application**.

That gap is where a whole class of bugs lives — cursor capture, hover, drag/drop, DPI scaling, resize, focus, native dialogs, Wayland/X11 differences, visual clipping, rendering artifacts, keyboard shortcuts, plugin editor interaction. For these, no unit test constitutes proof. The reporter says *"the fader reads 0.73 but draws at zero"* and there is currently no way for anyone but a maintainer, on the reporter's machine, to establish whether that is a state bug or a render bug.

The Muse Diagnostic Bridge closes that gap: it lets an authorized agent observe Aestra's runtime truth, drive a declared reproduction, and produce **evidence a maintainer can replay** without owning the reporter's hardware.

The goal is not smarter Muse. It is **instrumentation, authority boundaries, and reproducibility**. Once those exist, the intelligence behind them is close to interchangeable.

## 2. Non-goals

- **Not a general remote-control API.** No ambient "let an agent use the DAW" capability.
- **Not a replacement for unit and integration tests.** The bridge is for what those cannot reach.
- **Not a creative-authoring path.** Nothing here changes how Muse proposes musical ideas.
- **Not a headless-automation product.** `HeadlessMain.cpp` / `HeadlessExportMain.cpp` already cover batch rendering.
- **Not a telemetry channel.** Nothing is transmitted anywhere. All of it is local, session-scoped, and user-initiated.
- **Not a reverse-engineering surface.** See §12.

## 3. Muse vs Aestra Diagnostics

`Muse-AI-Spec.md` commits, in writing:

> **What Muse Never Becomes**
> - A tool that applies changes without user confirmation

A diagnostic session in which an agent deletes a clip through the real UI appears to violate that. It does not — but only because **the confirmation moves up a level**, not because the rule is relaxed.

| | Muse (creative) | Aestra Diagnostics |
| --- | --- | --- |
| Trigger | Ambient, always-on while working | Explicit session, off by default |
| Flow | `human intent → Muse proposes → human confirms → Aestra mutates` | `human enables session → grants bounded capabilities → agent executes a declared fixture → all actions and observations recorded → session ends, authority disappears` |
| Unit of consent | One suggestion | One bounded diagnostic operation (a named fixture) |
| Target | The user's live project | A copy, or a supplied reproduction project |
| Audit | None needed | Everything recorded |

The user authorizes **"run fixture R17 against this project copy"** — not each mouse movement inside R17. That is a stronger consent model than per-action prompting, because per-action prompting on a 40-step drag sequence degenerates into click-through fatigue and consents to nothing.

**These are two subsystems, not one subsystem with a flag.** They share transport and provider infrastructure underneath:

```text
Muse                    → creative / product assistant
Aestra Diagnostics      → repair / investigation subsystem

Shared underneath       → socket, JSONL verbs, agent loop and providers,
                          semantic runtime bridge
```

They must not share a code path for mutation, and diagnostic capabilities must be unreachable from the creative surface.

### 3.1 Why this is not called "Muse diagnostic mode"

`Muse-AI-Spec.md` is explicit that producers are "deeply anti-generative-AI right now because it threatens their identity as creators," and that Muse's entire public framing depends on being predictive rather than generative.

> "Muse can control your DAW"

is exactly the wrong sentence for that positioning. Whereas:

> "Aestra Diagnostics can execute an explicitly authorized reproduction fixture"

is a completely different claim — bounded, consented, and obviously a repair tool.

So there is no public-facing "Diagnostic Muse Mode." This is an **Aestra system capability that happens to reuse Muse plumbing**, named and surfaced separately in the UI, in logs, and in any public copy. The separation is cleaner technically and culturally, and baking it into the name now costs nothing.

## 4. Consent and capability model

A diagnostic session is a bounded grant with an explicit capability set, surfaced to the user before it starts:

```text
Diagnostic session
──────────────────
Project:  recovery-copy.aestra
Fixture:  R17
Duration: this session only

Allowed
  ✓ read runtime state
  ✓ capture screenshots
  ✓ transport control
  ✓ UI input / replay
  ✓ reversible project mutations

Not allowed
  ✕ filesystem access outside fixture scope
  ✕ modify source
  ✕ enable diagnostics itself
  ✕ inspect private implementation details
```

Rules:

1. **The agent cannot enable diagnostics.** Only a human, in the Aestra UI. A skill may *tell the user* to turn it on; it must never be able to turn it on. This is the single most important rule in the document, because the party connecting is by design a stranger's coding agent.
2. **Capabilities are granted per session and revoked on session end.** No persistent grant, no "remember this choice."
3. **Aestra displays an unmistakable indicator** for the entire duration. A DAW under external control must never look like a DAW that is not.
4. **Never operate on the user's only copy.** This mirrors the public `recover-project` rule and applies to the bridge itself: the session refuses to start against a project that has no preserved original.
5. **Reversible mutations only.** Anything the fixture does must be undoable or confined to the working copy. Destructive operations are outside the capability set entirely — not gated behind a prompt.
6. **Session end is authority end.** Crash, disconnect, timeout, window close: authority disappears. Fail closed.

## 5. Observation architecture

Muse's current verb surface — from `@/home/currentsuspect/Dev/Aestra/Source/App/MuseReplMain.cpp` — is:

```text
add_track      set_bpm      list_tracks
```

Two mutators and one reader. **Muse today is an actuator with almost no instrumentation.** That priority must invert before anything else here is built:

```text
Muse as it stands     mutation  >>>  observation
Diagnostic foundation observation >>>  mutation
```

The read surface needs, by category (names indicative, not settled):

| Category | Reads |
| --- | --- |
| Session | `get_session_state`, `get_project_load_report` |
| Transport | `get_transport_state` — playing, position, tempo, loop |
| Selection | `get_selection`, `get_focus` |
| Model | `get_track`, `get_clip`, `get_routes` |
| UI | `get_visible_panels`, `get_widget_state`, `get_pointer_capture` |
| Health | `get_recent_errors`, `get_rt_violations` |

Two of these already have real backing and should come first because they cost almost nothing:

- **`get_project_load_report`** maps directly onto the existing `ProjectLoadReport` / `LoadIssue` structure in `@/home/currentsuspect/Dev/Aestra/Source/Core/ProjectSerializer.h:19-40` — severity, category, message, objectId, referenceId, context, with categories `integrity`, `clip`, `unit` plus `missingAssets`. The public `recover-project` skill already instructs agents to consume exactly this shape. Exposing it over the bridge closes the loop between the published protocol and the runtime at nearly zero design cost.
- **`get_rt_violations`** depends on P0.1 of the RT-safety audit landing first (see §6.1).

### 5.1 Stable diagnostic identity — a prerequisite that does not exist today

> **Stable diagnostic target identity is a prerequisite for durable interaction fixtures. Fixtures must not address UI elements through pointers, draw order, transient hierarchy position, or coordinates alone.**

Without it, `R17` silently degrades from

```text
target = mixer.insert[4].gain
```

to

```text
click x=814 y=392
```

and the entire committable-reproduction premise collapses. A coordinate fixture is invalidated by any layout change, any DPI change, any window resize — the exact conditions several of the target bug classes involve.

**Current state, verified 2026-07-27:**

| Question | Finding |
| --- | --- |
| Is there an identity field? | Yes — `id_`, a plain `std::string`, `@/home/currentsuspect/Dev/Aestra/AestraUI/Core/NUIComponent.h:124-125,163` |
| Is it populated? | **Rarely.** 28 `setId()` call sites in the entire UI. Default is empty. |
| Is it unique per instance? | **No.** Values are type/panel labels — `MenuBar`, `AestraEQEditor`, `UIMixerPanel_Inner`, `SegmentedControl`. Two EQ instances collide. |
| Can you resolve an id to a component? | **No.** No `findById` / `getById` / `findComponent` exists anywhere in `AestraUI` or `Source`. |
| Do leaf controls have identity? | **No.** No knob, fader, insert or send is addressable. `Mixer.Knob17` has no backing. |
| Is there an accessibility layer to borrow names from? | **No.** |

So `id_` today is an occasional debug label, not an addressing scheme. **Aestra cannot express a durable UI fixture.**

**The precedent to follow already exists one layer up.** `CommandRegistry`
(`@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Commands/CommandRegistry.h:15-59`) keys commands by stable verb string in an `unordered_map<std::string, Factory>`, with real lookup and parse-safety tests in `Tests/Commands/`. Commands are stably addressable *today*; widgets are not. The UI needs the analogue of what the command layer already has.

**Consequences for this design:**

- Stable identity moves into **Phase 0** (§16). It is not Phase 2 work, and not optional.
- Identity must be **instance-unique and hierarchical** — `mixer.insert[4].gain`, not `EffectChainRack` — and stable across builds, layout changes and window size.
- It needs a **resolver**: id → component, with a defined failure mode when a target is absent (a fixture step targeting a missing widget must fail loudly, never fall back to coordinates).
- Because §8's actuation ladder says escalate no further than necessary, and the **command layer is stable while the widget layer is not**, fixtures should prefer command-level actuation wherever a bug permits it — today that is the only durable channel.
- This is the largest hidden dependency in the plan and the item most likely to be under-scoped, because it touches every widget rather than any single subsystem.

**Semantic reads are the primary source of truth.** A bridge that reports `fader.value = 0.73` and `fader.bounds = {x,y,w,h}` lets an agent distinguish a state bug from a render bug in one step — something no amount of pixel inspection can do reliably.

## 6. Realtime-thread isolation

**Non-negotiable, and the highest-risk item in this document:**

> **Muse observes published audio state. Muse never synchronously queries the audio thread.**

The audio thread writes snapshots and events outward. Diagnostic readers consume them. **No bridge call may cause the realtime thread to wait** — not on a mutex, not on an allocation, not on a condition variable, not on I/O.

Without this stated up front and enforced in review, someone will eventually write:

```cpp
std::lock_guard lock(engineMutex);
return engine.getState();
```

and Heisenberg's debugger is now shipping inside the DAW: an instrument that perturbs exactly the timing-sensitive behavior it exists to measure, and that manufactures dropouts on the machines of users who enabled it to diagnose dropouts.

### 6.1 The pattern already exists — reuse it, don't invent

Aestra has already solved this correctly at least three times. The diagnostic bridge should look like existing code, not like new infrastructure:

| Existing mechanism | Reference |
| --- | --- |
| Atomically flipped immutable snapshot | `std::shared_ptr<const EffectChainSnapshot>` — `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Plugin/EffectChain.h:282,290,299`, consumed at `@/home/currentsuspect/Dev/Aestra/AestraAudio/include/Core/AudioGraph.h:72`. The RT audit calls this out as the "correct lock-free pattern." |
| Lock-free atomics for scalar transport state | `m_transportPlaying`, `m_fadeState` |
| Lock-free command queue into the audio thread | `applyPendingCommands` |
| SPSC queue draining to a non-RT consumer | the `recording/` path |
| Canonical RT-thread detection | `isRealtimeAudioThread()` / `ScopedRealtimeAudioThread`, `@/home/currentsuspect/Dev/Aestra/Source/AudioThreadConstraints.h:76` |

The diagnostic publication path is the `EffectChainSnapshot` pattern applied to observability: build the snapshot off-thread, publish by atomic pointer flip, let readers take a `shared_ptr` copy and read it at leisure.

**Shipping prerequisite.** `AUDIT_RT_Safety_2026Q2.md` P0.1 records that **no RT-violation handler is installed in production**. Until that lands, an RT violation introduced by the bridge is undetectable in the field — we would ship the instrument without the alarm that tells us the instrument broke something.

This gates **shipping**, not design or implementation: Phase 1 can be built and tested internally against a debug build with violation detection enabled. It must not reach users until P0.1 closes. Tracked in Phase 0 (§16) for that reason.

## 7. Diagnostic state publication

- **The audio thread publishes; it is never interrogated.** Publication is a pointer flip of an immutable, pre-built structure.
- **Snapshots are versioned and timestamped**, so a trace can align observations to sample position and wall clock.
- **Publication rate is bounded and independent of reader count.** A hundred reads per second must not become a hundred snapshot builds per second; readers see the most recent published snapshot, and a reader that falls behind loses intermediate frames rather than applying backpressure.
- **UI/widget state is published from the UI thread** by the same discipline, so widget reads never block rendering.
- **The bridge is inert when no session is active.** Zero publication cost, zero allocation, zero branch in the hot path beyond a single relaxed atomic load. A user who never opens a diagnostic session must not pay for its existence.

## 8. Action channels

Two axes that the naive "determinism ladder" conflates. Observation channels and actuation channels are independent, and the right combination is **the least invasive observation with the most deterministic actuator that can still reproduce the bug.**

**Observation channels** — ordered by interpretive reliability:

| Channel | Perturbs state | Ambiguous to interpret |
| --- | --- | --- |
| Semantic introspection | no | no |
| Logs / events | no | low |
| Audio measurement (probes, capture) | no | low |
| Screenshots / video | no | **high** |

**Actuation channels** — ordered by determinism:

| Channel | Determinism | Reaches |
| --- | --- | --- |
| Verbs (`set_bpm`, …) | highest | model state only |
| Synthetic internal event injection | high | widget logic, most UI |
| Recorded trace replay | high | full declared sequence |
| Real OS input (uinput / portal) | lowest | pointer capture, focus, native dialogs, compositor behavior |

Because screenshots are **non-invasive**, they can run continuously at every step without being the primary source of truth. That is the correction to treating "visual" as simply the bottom rung: it is the least reliable to *interpret* and among the safest to *collect*.

Escalate only as far as the bug requires. A bug reproducible by verbs must be reported at the verb level, because a real-input reproduction of a model-level bug is a worse artifact — slower, flakier, and machine-dependent for no benefit.

## 9. Session trace and the R17 fixture model

**A reproduction must be an artifact, not an instruction.**

"Muse, reproduce the sequence like before" is an agent improvising the same thing twice, which is precisely the non-determinism the whole design is trying to escape. A fixture is a file: declared steps, declared observation points, replayable by anyone.

Illustrative only — **the serialization format is deliberately not settled here:**

```yaml
id: R17
version: 1

environment:
  platform: linux
  display_protocol: wayland

setup:
  project: fixtures/cursor-capture.aestra

steps:
  - action: pointer_move
    target: Mixer.Knob17
  - action: pointer_down
    button: left
  - action: pointer_move
    delta: [0, -180]
  - observe:
      cursorCapture.owner: Mixer.Knob17
  - action: pointer_up
  - observe:
      cursor.visible: true
      cursorCapture.active: false
```

Consequences worth being explicit about:

- **Fixtures are committable.** They belong in the repository next to the regression tests they support.
- **A PR can now carry real evidence:** bug report + project fixture + interaction fixture + regression test + source fix. That is a categorically stronger claim than *"unit test green, probably fixed."*
- **Replay is the verification step.** After the patch, rerun R17 and compare observations. The public `prepare-pr` skill already insists that a passing build is not proof the bug is fixed; a replayable fixture is what makes that demand satisfiable for UI bugs.
- **Observation points are assertions.** A fixture that records only actions is a macro; one that records expected observations is a test.
- **This section is blocked on §5.1.** Every `target:` in the example above presumes an addressing scheme Aestra does not yet have. Until Phase 0 lands, fixtures can only address the command layer.

## 10. Visual and audio evidence

- **Screenshots are corroboration, never diagnosis.** The semantic read says what Aestra believes; the screenshot says what it drew. A bug is proven by the *disagreement between them*, which requires both.
- **Frame capture is scoped to Aestra's own surface**, never the whole desktop.
- **Audio evidence is measurement, not recording** where possible — peak/RMS, silence detection, null-difference against an expected render. "No audio after undo" should be provable numerically, not by a maintainer listening to an attachment.
- **Privacy is a first-class constraint.** Screenshots and project fixtures contain the user's unreleased music, project names, and sample paths. The bridge must warn before any capture leaves the machine, and any skill that instructs an agent to attach evidence to a **public** GitHub issue must repeat that warning. The public `collect-diagnostics` skill already flags that project files can identify the user; visual evidence is strictly worse.

## 11. Environment provenance

For input and rendering bugs the environment is not incidental — **it is part of the experiment.** A cursor-capture bug under Hyprland with `ydotool` injection is not the same experiment as GNOME with XTest, and a fixture that omits this is not reproducible.

Every diagnostic session records:

```text
display protocol        wayland | x11
compositor + version    e.g. Hyprland 0.56.0
portal implementation   e.g. hyprland.portal, wlr.portal
input injection method  e.g. uinput/ydotoold, XTest, internal synthetic
scale factor / DPI
window size + display configuration
GPU / driver
```

Note what this implies: on Wayland, Aestra **cannot** warp the pointer or capture other surfaces on its own. Real-input actuation necessarily runs through `uinput` or a desktop portal, which means **the compositor and portal stack are inside the test fixture.** A reproduction that works on one compositor may legitimately fail on another, and that is a finding rather than a flaw in the fixture.

This section should be mirrored into the public `collect-diagnostics` skill when the bridge ships — it is the field most likely to be omitted, precisely because it does not feel like part of Aestra.

## 12. Public/private runtime boundary

The frozen `aestra-agent-protocol/v1` halt condition stops an agent when it reaches *code unavailable in the checked-out repository*. A runtime bridge changes what is reachable: an agent can observe the **behavior** of a private component without ever seeing its source.

That is mostly desirable — it is exactly the evidence a boundary report wants ("the failure occurs when the public host calls this entry point with this payload"). But it needs a line, or the bridge becomes a reverse-engineering oracle that quietly defeats source availability:

> **Publicly observable behavior may be exposed across a private component boundary. Private implementation state, algorithm intermediates, memory layout, proprietary assets, and internal diagnostics remain inaccessible.**

Concretely: a private effect's parameter values, latency, and output signal are observable. Its internal coefficient tables, model weights, DSP intermediates and heap layout are not.

**No protocol v2 is created by this document.** The v1 halt condition is not contradicted by anything that exists today — Muse exposes no private internals, so there is no operational gap to repair. Versioning a public protocol around an API that has not been built would be exactly the kind of speculative breaking change the compatibility policy exists to prevent. When the bridge actually ships and the public skills need to instruct agents in its use, *then* evaluate whether the added prohibition requires v2 under the rules in the website repo's `docs/agent-protocol-versioning.md`.

Until then: **v1 stands, and no recovery skill changes.**

## 13. Failure with instrumentation attached

Instrumentation changes timing. Several of the target bug classes — pointer capture, focus, drag/drop — are timing-sensitive by nature. Therefore:

- **A bug that reproduces only with the bridge attached is a finding, not a failed reproduction.** It means the bridge perturbs something real, and that is a defect worth its own investigation.
- **A bug that stops reproducing with the bridge attached is equally a finding**, and the more dangerous one, because it looks like success.
- Every fixture run records **hit rate with and without instrumentation.** The public `reproduce-crash` skill already asks for hit rate over repeated runs; this extends the same discipline to the observer.
- The bridge records its own overhead per session, so a report can distinguish "slow because of the bug" from "slow because of the measurement."

## 14. Audit log and reproducibility

Every diagnostic session produces a complete, self-contained record:

- the capability grant that was authorized, and by whom
- the fixture executed, by id and content hash
- every action dispatched, with timestamps
- every observation returned
- environment provenance (§11)
- instrumentation overhead and hit rates (§13)
- Aestra version and commit; project format version (currently `PROJECT_VERSION_CURRENT = 3`, min supported `1`)

This mirrors the provenance discipline already shipped in the public protocol, where reports cite an artifact URL and digest so a reader can verify the exact instructions an agent followed. **The same principle applies one layer down: a session record should let a maintainer verify the exact experiment that was run**, not merely read an agent's summary of it.

## 15. Security boundaries

The bridge is a local socket that can read application state and synthesize input. It must be treated as an attack surface, not a convenience.

- **Off by default.** Not merely disabled — not listening.
- **Loopback only.** Never a routable interface. The existing Muse surface already defaults to `127.0.0.1` (`@/home/currentsuspect/Dev/Aestra/Source/MuseAgent/MuseCliRequest.h:17`); the diagnostic bridge must make that a hard constraint rather than a default.
- **Per-session token**, issued when the user enables the session, never persisted.
- **Capability-scoped**, not all-or-nothing. A session granted read-only observation cannot synthesize input.
- **Fail closed** on any ambiguity — expired token, unknown verb, capability not granted, malformed fixture.
- **Bounded input.** The fixture parser is parsing hostile input by assumption. Apply the same discipline the project loader already uses: bounded strings, bounded sizes, structural validation before use.
- **No filesystem access outside fixture scope.** The fixture declares its project and asset paths; nothing else is reachable.
- **The session cannot escalate itself** — it cannot enable capabilities, extend its own lifetime, or start another session.

## 16. Incremental implementation plan

Deliberately boring, and strictly ordered. Each phase is independently useful and independently shippable; **no phase begins before the one above it is real.**

| Phase | Deliverable | Gate |
| --- | --- | --- |
| **0** | **Stable diagnostic identity** (§5.1): instance-unique hierarchical ids, a resolver, loud failure on missing targets. **Production RT-violation visibility** — `AUDIT_RT_Safety_2026Q2.md` P0.1. | Nothing below starts without identity. RT visibility gates *shipping*, not design work. |
| **1** | Read-only semantic state. Snapshot publication per §6–§7. Starts with `get_project_load_report` and transport/selection reads. | Phase 0 identity landed. No mutation added in this phase. |
| **2** | Trace recording. Capture real user interaction into the fixture format. | Phase 1 stable; format reviewed but still revisable. |
| **3** | Deterministic internal replay. Synthetic event injection, no OS input. | Recorded traces replay reliably in-process. |
| **4** | Screenshots and visual evidence. Scoped capture, privacy warnings. | Phase 3 replay is deterministic enough to correlate frames. |
| **5** | External OS input, only where §8 shows it is required. | Environment provenance (§11) captured and complete. |
| **6** | Agent-driven end-to-end diagnostic sessions. Consent UI, capability grants, audit log. | Everything above, plus §4 and §15 fully implemented. |

Only after Phase 6 does anything reach the public agent protocol. **The skills must not instruct agents to query a surface that does not exist** — a skill describing imaginary verbs is worse than no skill, because it produces confident, unfalsifiable reports.

---

## Open questions

1. **Fixture format.** YAML is convenient for humans and diffs; JSONL matches the existing Muse socket. Deferred until Phase 2 has real recorded traces to shape it.
2. **Identity scheme shape.** §5.1 settles *that* stable identity is needed and that none exists. It does not settle the scheme: hierarchical path (`mixer.insert[4].gain`), registered opaque id, or a hybrid. Nor does it cost the migration across every existing widget.
3. **Snapshot granularity.** One session-wide snapshot, or per-subsystem snapshots with independent publication rates?
4. **Relationship to the existing REPL.** Is `MuseReplMain.cpp` the foundation, or does the bridge want its own entry point with different lifetime and security properties? Leaning toward its own — §15's security properties differ sharply from a developer REPL's.

**Settled during drafting:**

- *Does this belong in Muse?* No. It reuses MuseAgent infrastructure and is named separately — see §3, §3.1.
- *Does stable widget identity exist?* No. See §5.1; promoted to Phase 0.
