# Consolidate Audio Range — Contract

**Status:** Locked before implementation — build to this, not to the nearest code seam
**Owner:** Dylan
**Last Updated:** 2026-08-01
**Scope:** Internal architecture doc. Not public-facing.

> Written before the renderer exists, deliberately. Consolidation carries enough
> hidden decisions that an implementation grown from whichever seam is nearest
> would quietly become the specification, and would then be permanently
> inconsistent with playback.

Two statements govern everything below.

> **The reference implementation for audible behaviour is the runtime clip
> renderer, not a parallel interpretation of clip edits.**

> **Consolidation equivalence is measured on the engine's rendered output before
> and after the operation, including the samples immediately around every
> original clip boundary.**

---

## 1. Public command and range semantics

```
consolidate_audio --lane <canonical-lane-id> --start <beat> --end <beat>
```

Stateless and explicit. There is deliberately **no** Muse selection object and
no `set_selection` verb: invisible state drifts between calls, and a GUI
selection is trivially translated into these three arguments at the call site.

The lane is addressed by its **canonical UUID**, not a positional index.
`list_clips` already emits it losslessly (`MuseService.cpp`,
`laneJson.set("id", laneId.toString())`). Positional indexes shift when lanes
are deleted; this is the same identity class already repaired for clips.

*Note: no first-class timeline selection model exists in the tree today. The
only thing resembling one is `TrackManagerUI::m_selectionBoxStart/End`, which is
pixel-space rubber-band state, not a model range. A selection verb would have
had to fabricate the state it then hid.*

**Range is half-open: `[startBeat, endBeat)`.** The resulting clip occupies that
exact range, including any silence between contributing clips.

## 2. Accepted and refused inputs

Accepted:
- One or more audible audio clips on the lane intersecting the range.
- Gaps between them (rendered as silence).
- Overlaps between them (summed).

Refused, precisely — never silently discarded or omitted:
- **Any audio clip crossing `startBeat` or `endBeat`** (see §3.3).
- A MIDI clip, or any unsupported clip type, intersecting the range.
- An audible clip edit the renderer does not implement.
- An invalid or unknown lane id, a non-finite or inverted range, an empty range.
- A range containing no audio clip at all.

The refusal rule is the load-bearing one: **an audible property that cannot be
reproduced must stop the operation.** Silent omission produces a consolidated
clip that sounds wrong and looks committed.

## 3. Audible contributions and edge-fade policy

### 3.1 What must be reproduced

Every clip contributes exactly what playback would give it: source offsets,
slip (`ClipEdits::sourceStart`, in project-rate samples — rescale it), playback
rate and the resulting resampling through the canonical resampler, mute, gain,
pan, and fades. Overlaps sum exactly as playback sums them. Gaps stay silent.

Per the governing statement above: mirror `AudioEngine`'s clip render path.
Do not re-derive an interpretation of `ClipEdits` in parallel — that is how the
fade shape in the first render slice ended up disagreeing with playback.

### 3.2 Edge fades — the subtle part

The engine applies an automatic edge fade (`CLIP_EDGE_FADE_SAMPLES`, 128) at
every clip boundary, as an anti-click measure. Consolidation **removes the
boundaries between the original clips**, so the engine will no longer apply
those fades. It will, however, apply them at the two boundaries of the new
consolidated clip.

The policy — **revised**, see the correction below:

> **Bake every contributor's fades exactly as the runtime renders them. Mark the
> resulting consolidated clip so its own automatic edge fades are not applied
> again.**

Internal boundaries must be baked: after consolidation there is no boundary
there, so nothing re-applies the fade, and not baking it changes the sound at
every original seam. This includes a clip that starts after `startBeat` or ends
before `endBeat` even when separated by silence — that is still an internal
boundary.

Outer boundaries must also be baked, and the *result* clip must be told not to
fade them again. That is durable state on the clip, not a render-time argument.

#### Why the earlier strategy was wrong

An earlier revision said to bake every fade *except* one coincident with the
outer boundary, and to let the new clip's own automatic fade reproduce it —
justified by linearity: applying ramp `r` to each contributor before summing
gives `r·A + r·B = r·(A + B)`.

That argument only holds when the delegated ramp is **the same ramp**. It is
not, for two independent reasons.

**User and automatic fades collapse into one ramp.** The runtime computes a
single effective length:

```cpp
effectiveLength = min(clipLength, max(kClipEdgeFadeSamples, userFadeLength));
```

With a 500-sample user fade the original output has one 500-sample ramp. Baking
the user ramp while delegating the automatic one yields the *product* of two
ramps, where the original had a `max()`.

**Lengths differ with clip length.** A 64-sample contributor consolidated into a
512-sample result:

```
original automatic fade = min(64, 128)  =  64
result automatic fade   = min(512, 128) = 128
```

Suppressing the original and relying on the result's fade substitutes a
different envelope entirely.

#### What this requires

An ephemeral renderer argument (`suppressLeading`/`suppressTrailing`) is not
sufficient: it would make consolidation sound correct only while being created.
After save and reload the playback engine would have no durable explanation for
why the result clip must not add another automatic fade.

The state belongs on the clip:

```cpp
struct AutomaticEdgeFadePolicy {
    bool applyLeading{true};
    bool applyTrailing{true};
};
```

and the runtime becomes:

```cpp
const uint64_t automaticLeading =
    clip.edgeFadePolicy.applyLeading ? std::min<uint64_t>(clipLength, kClipEdgeFadeSamples) : 0;
const uint64_t userLeading = std::min<uint64_t>(clipLength, clip.fadeInSamples);
const uint64_t fadeInLen = std::max(automaticLeading, userLeading);
```

Defaults keep every existing clip bit-identical. A consolidated result sets both
to `false` with zero user fades, because all original edge behaviour is already
present in the rendered source.

That state must be stored on the model clip, copied into `ClipRenderState`,
consumed by the shared renderer, serialized and restored, defaulted to `true`
for old projects, and covered by save/load and audible-equivalence tests. It is
therefore **its own PR, landing before consolidation** — a zero-default-change
edge-policy change, gated on: default policy 6/6 exact; each edge suppressible
independently; user fades still working when automatic fades are suppressed;
very short clips; serialization round-trip; and sabotage proving that a missing
suppression causes outer-edge double attenuation.

Inverse compensation — dividing the rendered mix by the result clip's future
automatic envelope — would avoid serialized state, but it needs severe
pre-emphasis for short boundary clips and can exceed the representable range of
the stored format. Rejected as operationally fragile.

### 3.3 Why boundary alignment is required in v1

A range cutting through the middle of a clip creates a clip boundary where
playback previously had none. The consolidated result then gains an edge fade
that the original never had, and the operation is no longer audibly equivalent.

**v1 therefore rejects any range that cuts through an audio clip.** This still
delivers the functionality that matters — multiple clips, overlaps, silent gaps
— without quietly changing audio.

Arbitrary boundary cuts wait for an explicit per-edge policy (whether an
automatic edge fade is enabled, or already baked). When that arrives it must be
**serialized, consumed by playback, and covered by equivalence tests in the same
PR**. It must never be another passive field: this arc began by deleting
`pitchSemitones` and `timeStretchRatio`, which were declared, compared, and read
by nothing.

## 4. Ownership through execute, failure, undo and redo

The result references **one flat audio source**, starts at `startBeat`, and
carries unity/default edits with no inherited offsets — the offsets are baked
into the rendered audio, so leaving them set would apply them twice.

- **execute** — render, write, register source and pattern, remove the original
  clips, place the consolidated clip. All project mutation belongs to the
  command; the factory validates and constructs only, never mutates.
- **failure** (render, write, or placement) — leaves **no** new clip, pattern or
  source, and no command-history entry. A source this command introduced is
  withdrawn; a source the project already had is not.
- **undo** — restores the original clips exactly, including their ids.
- **redo** — reuses the rendered file, source and pattern. It must **not**
  render again or write another file.

## 4b. Reaching the runtime kernel — characterize before extracting

§3.1 requires mirroring `AudioEngine`'s clip render path rather than re-deriving
it. That logic currently sits inside `processBlock`, interleaved with graph
concerns, so "mirror" will in practice mean extracting a shared clip-local
kernel both callers use. **Characterize it before touching it.**

Order of work, not negotiable:

1. Add focused fixtures capturing **current** runtime output for the cases that
   matter: unity-rate, resampled, panned, faded, overlapping, and short clips.
2. Sabotage against those fixtures, so they are *known* to detect altered clip
   behaviour before they are trusted to protect a refactor.
3. Extract only the pure clip-local kernel.
4. Re-run the characterization tests and require **identical** output.
5. Build `AudioConsolidationRenderer` on that kernel.

The extraction is acceptable while it moves arithmetic without changing
ownership or scheduling. **Stop** the moment it requires touching any of:

- `processBlock` orchestration
- graph traversal or topology
- track / effect / routing order
- RT buffer ownership or allocation
- transport state
- callback timing
- PDC or automation application

At that point, copying the logic is **also not acceptable** — it would
immediately create the second interpretation §3.1 exists to forbid. The correct
response is to pause and decide whether the live-path refactor deserves its own
narrowly scoped, zero-audio-change PR.

If that preparatory PR happens, it carries one extra gate beyond the normal
suite:

> The refactor must pass a **before/after rendered-byte comparison on fixed
> fixtures**, not merely the existing tests.

Ordinary tolerance-based assertions can miss a shifted interpolation phase, an
off-by-one in edge-ramp indexing, a changed pan law, a different overlap order,
or altered floating-point accumulation order. Exact bytes are not portable
across architectures, so keep the strict comparison **within one build and
environment**, and keep tolerance-based semantic tests for cross-platform CI.

## 5. Separation from graph rendering

This is **source-domain only**. It must not evaluate track plugins, sends,
automation, routing or PDC.

Those belong to the later range/graph renderer that will back Freeze, stem
printing and bus printing. The two layers may share the WAV writer and source
registration; they must not share rendering semantics. Anything needing the
audio graph does not belong here.

**Three pieces — do not stretch `ClipRenderService` into something vague:**

| Piece | Responsibility |
|---|---|
| `AudioConsolidationRenderer` | Mix multiple source-domain clips into one timeline-aligned buffer |
| `ClipRenderService` (existing) | Durable WAV writing, source and pattern registration |
| `ConsolidateAudioRangeCommand` | Clip removal, replacement, rollback, undo, redo |

## 6. Muse schema

```
{"consolidate_audio", CommandCategory::Clip, {
    {"lane",  FlagType::Id,    true},
    {"start", FlagType::Float, true},
    {"end",   FlagType::Float, true}
},
 "Replace a lane's audio across [start, end) with one equivalent clip. Refuses a range that cuts through a clip, or that contains a non-audio clip."}
```

`lane` is `FlagType::Id` — the canonical form, matching the clip verbs. `Int` is
for indexes and counts, never object identity.

## 7. Equivalence and sabotage gates

Load-bearing tests:

1. Two clips separated by silence → one clip with the same gap.
2. Overlapping clips → the same rendered sum before and after.
3. Differing source sample rates → playback preserved through the canonical
   resampler.
4. Gain, pan, fades, mute and playback rate all survive audibly.
5. Redo invokes no rendering and writes no file.
6. Placement or write failure restores every original clip and resource.
7. `render_song` before vs after is equivalent across the selected range.

**Test material must be deliberately discontinuous.** Smooth material is already
near zero at a boundary and will conceal a missing edge fade. Use signals with a
real step at every original clip boundary, and assert on the samples immediately
around those boundaries specifically — not only on aggregate energy.

**Sabotage gates** — each of these must be shown to fail the suite:
- one timeline offset (prove alignment is checked),
- one overlap addition (prove summing is checked),
- one rollback branch (prove failure cleanup is checked).

A passing test that has never been seen failing is not evidence. Note also that
sabotage proves a test detects *some* change; it does not prove the test encodes
the correct specification. The runtime renderer is the authority for that.

## 8. Naming

**Consolidate**, internally and in the UI. "Glue" may become an alias later; the
internal operation keeps the precise meaning — *replace a lane's audio across a
defined timeline range with one equivalent source-domain clip*.

"Bounce in Place" remains reserved for the graph-level operation and must not be
used here, for the same reason `CommitAudioClipEditsCommand` is not called
bounce.
