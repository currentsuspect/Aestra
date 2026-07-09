# Aestra Beta Gameplan — Locked 2026-07-07

**Owner:** Dylan · **Status:** Active execution plan · **Finish line:** the core loop is undeniable — *not* issue-count zero.

> This is the "never steer" doc. Work that isn't in the MUST-FIX list below is
> consciously deferred until the core loop is locked. New ideas get parked in
> §6, not started. The single checkpoint is §5.

---

## 0. The finish line (read this first)

Beta readiness is **one loop, boringly reliable**, on a 4 GB-target box:

> open Aestra → play notes from a keyboard → record + arrange a beat → mix it →
> export it → close → reopen tomorrow → **everything is exactly as left and
> sounds identical.**

Nothing on the "moat stack" (Muse, Cards, borrowed-metaphor polish) is required
for this. Do not fund the moat until this loop is undeniable.

---

## 1. Meta-finding: the issue tracker has drifted from the code

Verified 2026-07-07 by reading source, not labels. **Several "critical
beta-blockers" are already done or partly done.** So triage step #1 is a
**verify-and-close sweep**, before any fixing — the real MUST-FIX list is
smaller than 60 open issues implies.

### Verified DONE or nearly-done (close / re-scope, don't "fix")

| # | Label says | Reality (verified) | Action |
|---|-----------|--------------------|--------|
| #245 | "No fsync in any write path" | **Done.** `Aestra::syncOfstream` + `fsyncParentDirectory` used across Autosave, Export, UIState, Preferences, ProjectSerializer (5 paths). | Verify covers crash_flag (#284), then **close**. |
| #241 | "Routing: no cycle detection" | **Engine-side done.** `AudioGraphBuilder` runs a Kahn topo-sort, sets `graph.hasRoutingCycle`. Tracks don't silently mute at graph build. | Confirm UI surfaces the warning; re-scope to "UI feedback" or **close**. |
| #228 | "Oversampling for nonlinear DSP" | **Done** via #387 — real 241-line `DSP/Oversampler.h` with up/downsample. | Verify wired into limiter too, **close**. |
| #251 | "atomic_load … 14+ call sites" | **3 left**, not 14+ (rest migrated in #386). | Migrate the 3, **close**. |
| #239 | "Autosave off-thread data race" | Fully mutex-synchronized now (not the naive race described). | Confirm snapshot capture thread, **close or re-scope**. |
| #252 | "5 empty model stubs" | **Mixed/stale.** PatternManager.h (166 L) + ClipSource.h (147 L) are real; AudioClip.h absent, PlaylistTrack.h still a 6-line stub. | Re-scope to just the real stubs. |

### Verified genuinely OPEN (real gaps)

| # | Reality (verified) | Bucket |
|---|--------------------|--------|
| #263 | No checksum/integrity verify on project load. | MUST-FIX (trust) |
| #268 | `SerializationCompatibilityTest` still commented out in CMake. | MUST-FIX (test) |
| #273 | Roundtrip tests check structure, not values. | MUST-FIX (trust test) |
| #270 | CLAP host callbacks still `// TODO` (restart/flush no-op). | Hosting — see D1 |
| #244 | CLAP MIDI-in absent (no note events). | Hosting / core input |
| #278 | Ping-pong loop: UI buttons exist, engine path doesn't. | NOT-NOW |

**Everything else in the 60 needs the same verify pass before it's trusted as
"open." Do the sweep first.**

---

## 2. MUST-FIX NOW — the trust-and-loop backbone

Sequenced by dependency. This is the whole near-term job.

1. **#397 — UI-compile CI lane (FIRST).** Until CI builds the app, every fix
   after it can silently break the app build (already happened once, #396). One
   workflow file, highest leverage. Do before anything else.
2. **Verify-and-close sweep (§1).** Close the stale-done issues; re-scope the
   partial ones. Shrinks the board to reality. ~half a day of reading + `gh`.
3. **Serialization trust cluster:** #263 (content integrity on load), #268
   (re-enable SerializationCompatibilityTest), #273 (roundtrip *value* fidelity),
   #249 (migration proof + fixture corpus). Do these together — they're one
   "sessions never lie" theme. **Gate before any schema change.**
4. **Core input loop (features, not issues — own cadence):**
   - Hardware MIDI-in (ALSA seq / RtMidi) + computer-keyboard note entry.
   - Audio recording (RtAudio input params already exist; no `startRecording`
     app-side yet).
   - #244 CLAP MIDI-in feeds the same input path.
   *These are multi-day builds — do NOT mix into the issue-grind cadence.*
5. **Resampling path unification:** one kernel policy consumed by every path
   (preview/sampler/audition still diverge from mainline); merge the anti-alias
   prefilter to production default. Gated null test = all paths null vs mainline.

---

## 3. IMPORTANT — not now

Real, but after the loop is locked:
- Export enforcement: true-peak ceiling + mandatory dither (#423, #227).
- PDC v2 (grade C→A) — weeks of work; flat-chain PDC covers the common case today.
- `ProjectSerializer` → `AestraAudio` tier move (#266); UI monolith carves.
- Reference-hardware nightly perf budget (64-track CPU + memory-over-session +
  cold start). *Cheap; pull forward if a spare day appears — it defends the whole thesis.*
- Token-system rollout completion + audit of un-seen panels (mixer/piano-roll/plugin editors).
- #200 (VST3Host void* shared_ptr), #203, #210/#211/#213, #207, #279, #280, #284, #274.

## 4. CONSCIOUSLY DEFER — post-beta (do not start)
- Muse (any code) — v1.1/2027.
- Cards / activation / sign-in (#254, #286) — v1.0.
- macOS (#267), mobile, cloud sync.
- Third-party VST3/CLAP hosting **as a beta headline** — see D1.
- Theme picker (#393), Audition seeking (#282), word-wrap SDF (#275).
- Resampler SINAD beyond A- — **stop optimizing it.**

## 4b. KILL / MERGE-AWAY (cleanup, not features)
- #269 NUIBatchManager dead code — remove or integrate.
- #204 empty stub headers, #206 (likely dup of #270).
- #437 AutosaveManagerTest TSan flake — fix the test.

---

## 5. The one checkpoint

After the serialization trust cluster (§2.3) lands, **actually run the loop** —
make a beat, close, reopen, listen. That's where the real remaining gap shows
(bet: MIDI-in / recording). Lock the direction; keep exactly this one reality-gate.

---

## 6. Parking lot (new ideas go here, not into the branch)

_Anything that isn't §2 gets written here and left until the loop is locked._

---

## Decisions still owner's to make

- **D1 — Third-party plugin hosting: beta or post-beta?** Recommendation:
  **post-beta.** Ship beta on native plugins + Rumble; advertise VST3/CLAP as
  coming. #270/#244/#238/#247 are the evidence it's not ready. If it's post-beta,
  those hosting issues all drop out of MUST-FIX.
- **D2 — Is Muse the revenue justification or upside?** Recommendation: **upside.**
  Make plugins+packs carry the subscription so revenue doesn't depend on the
  hardest, least-proven build.
- **D3 — Freeze audio-quality + UI-polish at "good enough" to finish the loop?**
  Recommendation: **yes** (audio is A-, shell is high-finish). Highest-value call.
