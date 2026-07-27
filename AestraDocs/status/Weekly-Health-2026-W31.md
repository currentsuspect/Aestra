# Aestra Weekly Health — 2026-W31

**Week: July 27 – August 2, 2026**

Point-in-time. This document expires at the end of the week. The durable
engineering contract lives in `docs/technical/engineering-health.md` and must not
accumulate completed weekly objectives.

## Mission

Move Aestra materially forward across three axes:

1. **Unification** — one canonical authority/path per concept.
2. **Hardening** — encode assumptions as invariants, tests, and safe failure behaviour.
3. **Expansion** — add capability only through the unified/hardened architecture.

The week is healthy when Aestra ends with fewer competing concepts and fewer
implicit assumptions, even though the feature surface grows.

---

## Baseline — as of Monday 2026-07-27

Landed on `develop` today:

| PR | What it established |
|---|---|
| #626 | audio clip routing and mixer workflows |
| #624 | first Muse host verbs — `view.open`, `view.close`, `view.current` |
| #630 | Muse application-state ownership map, exposing real authority problems before more host verbs |
| #628 | CI phase 2 — source-tree blast-radius classification, conservative broad fallback preserved; advisory lanes gated |
| #629 | a drop that creates a lane is one transactional undo step, including stable lane identity on redo |

Open, in review:

| PR | What it establishes |
|---|---|
| #627 | project dirty state becomes an invariant of `CommandHistory` rather than something individual edit paths remember to mark |
| #631 | cancelled clip drags stop contaminating undo/redo history |
| #632 | timeline geometry — named grid inset and one x→beat conversion (partial U1; see below) |

These are committed work. Do not reopen their scope elsewhere.

Also today: **#247 closed** (its acceptance criteria were already met by
out-of-process hosting), **#633 opened** — Windows cannot start the plugin
helper, so it loads no third-party plugins at all. That is a platform capability
hole, not hardening.

---

# Backlog picks

## P0 — Must-hit this week

### U1 — Timeline geometry authority — #550

Collapse `gridStartX`, x→beat, beat→x, snapping and related timeline
calculations behind one geometry authority. There were multiple inline x→beat
implementations and 37 repetitions of the grid-start calculation.

**Target invariant:**

> Timeline position has one interpretation everywhere.

No clip operation, drag handler, zoom handler, renderer or selection path should
independently reconstruct timeline coordinates.

**Status:** #632 lands the representation half — the inset is a named constant
and the four inline x→beat copies are one primitive. **The origin is not
unified**, deliberately: the grid's origin is expressed in three bases
(component-relative, window-absolute, ruler-relative), and three ClipOps sites
use `getGlobalBounds()` where the rest use `getBounds()`, which the toolbar code
already documents as double-counting the parent. Those three are the instant
drag's grab offset and clamp region. Unifying them is a behavioural repair that
needs a human driving a drag — see engineering-health principle 9. **Remaining
work this week:** validate and land that seam.

---

### U2 — Move project serialization below the UI tier — #266

`ProjectSerializer` lives under `Source/` despite being the canonical project
schema, migration, validation and write implementation. That forces headless
consumers toward the UI tier. Move the authority into `AestraAudio` or
`AestraCore`.

**Target invariant:**

> Loading, validating, migrating and saving an Aestra project does not require
> the desktop application.

Clears the architectural path for eventual `project.*` Muse capabilities — which
#630 identified as the only domain with a real headless story today.

---

### U3 — Establish an actual settings authority

#630 found that `Preferences` looks like the settings model but seven fields have
no readers, while `GeneralSettingsPage::applyChanges()` is a bare
`m_dirty = false`. It also found `view.current` reports intent for four views and
actual visibility for two.

**A. View truth.** `view.current` must describe what the user or agent can
actually see. Where desired state and physical visibility genuinely differ —
they do, because `setViewFocus` hides panels in Audition mode while `m_viewState`
deliberately retains the restore state — expose **both** (`requestedOpen` and
`visible`) rather than collapsing them.

**B. Settings truth.** For each setting:

`persisted value → authoritative runtime owner → mutation path → observation path`

No setting may successfully persist while having no effect.

**Target invariant:**

> A setting that reports success changes the state that actually governs the
> application.

Do this **before** `settings.*` expansion. Registering verbs first would
fossilize whichever accidental state holder was picked.

---

### U4/H1 — Unify isolated bounce with the export path — #227

Master bounce already uses `AudioExporter`; isolated-track bounce still goes
through the older `AudioRenderer::renderBlock` path and therefore differs in
master-stage processing and dithering.

**Target invariant:**

> Export and bounce are configurations of one rendering system, not parallel
> render engines.

Unification and audio hardening at once.

---

## P1 — High-value hardening

### H2 — Portable DSP state — #210

Several plugins deserialize persisted state by casting byte buffers directly to
structs, embedding alignment, padding and portability assumptions into
project/plugin state. Move toward explicit field serialization.

**Target invariant:**

> Persisted DSP state is a defined format, not the compiler's memory layout.

---

### H3 — Asset identity, not merely asset paths — #264

Projects identify sample assets primarily by path, so a replaced file at the same
path silently becomes the project's audio. Introduce content identity/hash
verification.

**Target invariant:**

> Aestra never silently substitutes different audio for the audio a project was
> saved with.

Do this **after** the serializer move (U2), to avoid touching the persistence
layer twice.

---

### H4 — Finish defining the audio boundary — #422 + #423

Treat these as one mission. #422 defines the intended separation: internal float
truth stays unclipped while hardware and integer conversion may enforce explicit
boundary protection. #423 extends the purity harness into float32/int24/int16
export and explicit dither/quantization behaviour.

**Target invariant:**

> Every nonlinear or lossy boundary in Aestra is intentional, named, measurable
> and testable.

No mystery clipping.

---

### H5 — CLAP host completeness — #270

Several CLAP host callbacks remain no-ops, including restart, parameter rescan
and parameter flush. Implement and test at minimum the subset needed for real
parameter lifecycle behaviour. Matters before deeper plugin automation work.

---

# Expansion target

## E1 — Automation, as one dependency chain

Do not spread expansion across five unrelated surfaces this week. Automation is
the single expansion thread.

First settle **#466**: whether automation overrides or composes with mixer state.
Current behaviour can make the mixer fader effectively irrelevant when volume
automation exists. This is a product-policy decision, not an implementation
detail.

Then take **#467** as the first slice: internal-plugin parameter automation using
stable parameter identity and the existing RT-safe parameter infrastructure.

Order:

> **policy → regression → engine capability → UI later**

Do **not** start automation recording (#469) yet.

---

# CI and development health

#628 landed without weakening the conservative contract: required check names
unchanged, unknown scope still broad, selective paths carry explicit liveness
tests, `develop` pushes still establish full repository truth.

**Phase 3 remains blocked.** #620 measured **45 unlabelled tests**; selective test
execution before that is resolved can silently redefine "green".

**Week target:** bring unlabelled tests to **zero**, even if test-level slicing
itself lands later.

---

## Review throughput

CodeRabbit being the critical path is acceptable as an external constraint. It
must **not** distort engineering decisions.

- One pull request defends one primary invariant.
- Do not combine unrelated changes merely to save review requests.
- Keep preparing independent branches while review capacity is exhausted, but
  avoid stacking branches that modify the same authority before the preceding
  architectural change lands.
- CI should get cheaper without pull requests getting larger.

---

# Backlog truth

The tracker itself needs a health pass. Entries exist whose descriptions have
been overtaken by merged work — #201 still says three security tests are excluded
from CI, while #608 records that the final exclusion was removed and all three
now run across the six CI lanes plus nightly.

After the remaining pull requests merge:

- close resolved findings in #551;
- update #620 to reflect completed CI phases;
- close demonstrably stale issues such as #201;
- revalidate old issues before implementing them, rather than trusting historical
  line numbers or architecture.

**Target invariant:**

> An open Aestra issue describes a problem that still exists.

---

# Explicitly deferred

Do not let these steal the week:

- macOS application implementation #267
- cloud sync #286
- theme/font polish #287 / #393
- word wrapping #275
- automation recording #469
- large activation/monetization work #254
- low-priority Arsenal limiter fixture #366

They may matter; they do not compound this week's architectural work enough.

---

# Friday health gate

## Unification

- [ ] Timeline geometry has one authority
- [ ] Project serialization no longer belongs to the UI tier
- [ ] Settings have identifiable runtime authorities
- [ ] Bounce/export no longer have competing rendering paths

## Hardening

- [ ] Completed, failed and cancelled edits have formally different history semantics
- [ ] Persisted DSP state is moving away from compiler-layout serialization
- [ ] Sample identity can be verified rather than assumed from a path
- [ ] Float, hardware and integer-output boundaries have explicit policies
- [ ] CI narrowing cannot create fake green

## Expansion

- [ ] Muse expansion waits for truthful state ownership
- [ ] Automation semantics are deliberately chosen and regression-pinned
- [ ] Internal-plugin parameter automation can proceed through stable IDs rather
      than another bespoke mechanism

## Repository / process

- [ ] The pull requests open at the start of the week land cleanly
- [ ] Unlabelled tests reach zero, or have a complete classified plan
- [ ] Stale issues are closed or rewritten
- [ ] CodeRabbit rate limits affect waiting time, not pull-request architecture

---

## North-star test

For every substantial change this week:

> **What became the single source of truth?**
>
> **What now prevents that truth from drifting again?**

If neither answer is clear, the change probably is not contributing enough to
this week's mission. See `docs/technical/engineering-health.md` for the durable
version of this and the invariants reviewers enforce.
