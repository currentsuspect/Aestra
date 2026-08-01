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

The policy:

> **Bake every original automatic clip-edge fade, except an edge exactly
> coincident with the corresponding outer consolidation boundary.**

Why each half matters:

- **Internal boundaries must be baked.** After consolidation there is no
  boundary there, so nothing re-applies the fade. Not baking it changes the
  sound at every original seam. This includes a clip that starts after
  `startBeat` or ends before `endBeat` even when separated by silence — that is
  still an internal boundary.
- **Outer boundaries must not be baked.** The new clip receives the engine's
  automatic edge fade at `startBeat` and `endBeat`. Baking it as well applies
  the ramp twice — the outer edge is attenuated by `r²` instead of `r`.

Delegating the outer boundary is mathematically safe. Where several clips begin
exactly at the range boundary, applying the same linear ramp `r` to each before
summing gives `r·A + r·B`, which equals `r·(A + B)` — applying it once to the
sum. So the new clip's own edge fade reproduces it exactly, and baking is not
merely redundant but wrong.

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
