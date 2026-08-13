# Aestra UI Design Constitution

This document defines Aestra's visual language and implementation authority.

## Authority Rule (Non-Negotiable)

**AI may implement Aestra's visual language. AI does not define Aestra's visual language.**

If a required visual decision is not defined here, agents must escalate for a design decision instead of inventing local style.

## Core Intent

Aestra should feel:

- calm
- precise
- dense
- intentional
- musical

Aestra should read as a musical instrument/workspace, not a decorative control panel.

Primary principle:

**Visual information should be understandable before it is readable.**

## Visual Hierarchy

Every visible element maps to one level:

- **Level 0 - Canvas:** background space, near-invisible
- **Level 1 - Primary objects:** tracks, clips, channels, notes, waveform, playhead, selected object
- **Level 2 - Primary controls:** transport, fader, pan, mute/solo, tools, insert/send controls
- **Level 3 - Supporting information:** names, values, destinations, technical readouts
- **Level 4 - Infrastructure:** dividers, boundaries, secondary/meta labels, debug-ish scaffolding

Hard rule:

**Level 4 must never visually compete with Level 1.**

## Text Policy

Use text when shape, position, motion, or established iconography cannot communicate intent clearly.

Default preference order:

1. spatial/shape signal
2. icon
3. icon + tooltip
4. persistent text

Avoid permanently visible linguistic redundancy.

## Typography System

No ad-hoc text sizing.

Use a constrained semantic scale:

- Display
- Section
- Control
- Label
- Metadata
- Micro

Hierarchy is communicated by size, weight, contrast, spacing, capitalization, and placement - not size alone.

Text emphasis tiers:

- Primary text: strongest contrast
- Secondary text: moderate contrast
- Metadata: reduced contrast
- Inactive: subdued contrast

## Color Semantics

Aestra uses a small semantic vocabulary:

1. **Neutral** - primary surfaces and structure
2. **Aestra Accent (purple)** - selection, focus, active interaction intent
3. **Audio color (single hue)** - meters, waveforms, signal activity
4. **Warning (amber)** - mute, caution states
5. **Error/clip (red)** - record/armed, destructive intent, clipping
6. **Success (green)** - confirmed positive/engaged state (solo, ready, complete)
7. **Track identity colors** - user-assigned identity only

Hard rule:

**No component may introduce a new decorative color.**

Color intensity also carries hierarchy:

- faint: contextual
- normal: active/selected
- bright: immediate focus/interaction

## Borders, Dividers, and Surfaces

Borders are rare and meaningful.

Preferred separation order:

1. spacing
2. tonal contrast
3. alignment
4. subtle divider
5. border

Dividers must explain structure, not decorate.

Prefer layered surfaces over universal boxed containers:

- canvas
- elevated surface
- interactive layer

## Shape Language

Geometry is semantic and consistent:

- standard controls use shared corner radii
- pills are reserved for specific semantics (segmented controls, tags, compact state chips)
- avoid mixed arbitrary geometry

## Density and Progressive Disclosure

Aestra should be information-dense and visually calm.

- keep high-value controls visible
- reveal advanced or technical complexity progressively
- avoid always-on complexity that does not help immediate action

## Control Clarity and Interaction States

Controls should read as what they are (fader, knob, meter, clip, waveform).

All interactive controls use consistent states:

- default
- hover
- active
- focused
- disabled

No one-off visual state machines without explicit design approval.

## Icon Rules

Use icon when action is universal, repeated, and space-constrained.
Use text when concept is ambiguous or domain-specific, or when conveying destination/name/value.
Use icon + tooltip when icon alone is not universally clear.

Hard rule:

**Do not replace text with icons merely to "clean up" layout.**

## Motion Rules

Animation must communicate state or signal behavior.

Allowed examples:

- meter movement
- playhead movement
- meaningful state transitions

Disallowed:

- decorative glow, pulse, bounce, or sliding without semantic purpose

## One Voice Rule

Timeline, Mixer, Piano Roll, Sampler, Browser, Plugin UI, and Settings must feel like one product voice via shared:

- hierarchy
- typography
- color semantics
- spacing
- geometry
- interaction states
- surface strategy
- icon behavior
- border/divider philosophy

## Agent Contract

Agents may:

- implement approved layouts/components
- apply defined tokens/rules
- refactor existing UI to comply with this constitution

Agents may not autonomously define:

- new color meanings
- new hierarchy systems
- new spacing/geometry language
- icon-vs-text policy exceptions
- novel component visual grammar

If a required decision is missing, stop and request design direction.

## Required UI Workflow

1. Define musician task/UX outcome.
2. Define information hierarchy.
3. Define visual representation (shape/icon/text/color/space).
4. Define component behavior rules.
5. Implement.
6. Review rendered UI against visual intent.

## Implementation Order (Mandatory)

1. philosophy
2. hierarchy
3. visual language rules
4. design tokens
5. component specs
6. implementation

Do not reverse this order.

## Current Rollout Focus

Migration order: Mixer → File Browser → Timeline → Arsenal.

### Mixer (first target)

Mixer refactors must prioritize:

1. reducing persistent non-essential text
2. preserving fast, dense operation
3. enforcing semantic color usage
4. reducing decorative borders/dividers
5. making signal flow legible by structure before labels

### File Browser

The browser is a tool for reaching content, not a destination. It should place the musician's hands on the right sample and then recede.

Role mapping:

- Files and folders are Level 1 objects while inside the browser.
- Navigation tree, path, sort/filter, and counts are Level 3/4 infrastructure.
- Search is a Level 2 control.

Rules:

1. **One compact header row.** Back/forward/up, path, and sort/filter share a single row. Item counts and filter-status text are permanent linguistic redundancy — never spend a second header row on them.
2. **Shared header grammar.** Navigation pane and list pane use the same header height and bottom rule so they read as one continuous structure, not two stacked panels.
3. **Rows separate by tone, not rules.** Zebra tint and row grid lines stay faint (0.05 alpha class). Hover is a faint wash, never a bordered plate. Selection is the accent wash — the only strong surface signal in the list.
4. **Search field sits on the surface.** A subtle border plus focus ring delineates the field. Do not recess it into a dark well unless the surrounding surface is genuinely recessed.
5. **Identity colors are reserved for taxonomy.** Collection/tag category colors (user-assigned) are the only chromatic colors the browser may carry — the same identity class as track colors. No decorative color additions.
6. **Empty states stay quiet.** A muted metadata line or nothing. No hero text, no illustration.
7. **Density first.** Compact rows; chrome never grows to accommodate status text.

### Timeline

The timeline is the instrument. Its infrastructure must recede so clips, playhead, and selection (Level 1) dominate.

Role mapping:

- Clips, playhead, selection, loop range: Level 1.
- Transport, tools, add-track, snap: Level 2 controls.
- Track names, counts, channel numbers, bar numbers: Level 3/4 metadata.
- Ruler, grid, track chrome, borders: Level 4 infrastructure.

Rules:

1. **Ruler is infrastructure.** Bar numbers render in secondary text; tick hierarchy by contrast (major bar > minor bar > beat), beat ticks barely visible. Ticks never read as content.
2. **Grid never competes with clips.** Bar/beat/subdivision alphas are capped (current palette ≈ 0.02 / 0.005 / 0.002) and tiers cull by zoom density.
3. **Grid advertises only what snap can hit.** A drawn tier must be reachable by the active snap resolution — a grid line the user cannot snap to is a trap.
4. **One quiet separator per row.** The controls-to-clips boundary is a single subtle line; no double separators, no boxed rows. The control slab is a faint tonal tint that lifts slightly on hover.
5. **Lane identity is the color strip.** The 5 px user-color strip is the only chromatic row element outside content; its intensity scales with selection state.
6. **Toolbar controls are real controls.** Hover/active/disabled states come from control state, not bespoke hover flags; universal actions are icon + tooltip.
7. **Transport micro-labels stay micro.** DAW-specific concepts may carry a small icon plus a ≤ 10 px label; the label carries meaning and the icon reinforces — not the reverse.
8. **Overview/minimap is a surface.** It anchors with a panel-strength boundary line and must not out-draw the ruler beneath it.
9. **Piano roll inherits the timeline grammar** — ruler hierarchy, grid-tier/snap contract, and toolbar state behavior are shared. (One Voice.)

### Arsenal (step sequencer)

The step grid is the instrument. Rows and pads carry the musical content; everything around them is scaffolding.

Role mapping:

- Step pads, active notes, the playing step, unit identity strips: Level 1.
- Bars stepper, content badge, fit/scroll toggle, add-unit, power/mute/solo/arm: Level 2 controls.
- Pattern name, selected-unit name: Level 3 metadata.
- Bar numbers, beat markers, card borders: Level 4 infrastructure.

Rules:

1. **One status line, never a counter.** The command header shows the pattern name and at most the selected unit's name. Type, unit counts, and "READY"-style status words are permanently visible redundancy — the row list already carries them.
2. **Step pads are the instrument.** Active steps use the accent; the playing step is the only element allowed to lift (lighten or glow) — motion/state on the grid is the playhead, never decoration.
3. **Row identity is the color strip + name dot.** Same restraint as timeline lanes: identity color, not decorative fills. Type and routing live in the row's own labels and chips.
4. **Waveforms in the audio hue.** Audio-type rows and any signal preview render with the audio-hue tokens (waveformLine/waveformFill), never the accent or identity colors.
5. **Borders recede like the rest of the app.** Card strokes sit at the quiet tier (~0.4 alpha); pills stay at control strength (~0.5). No concentric card-in-card outlines.
6. **Mute/solo/arm use semantic state color** (warning/success/error) with letter glyphs — DAW shorthand, not decoration.
7. **Empty rows stay quiet.** A hover-only hint line names the next action; idle empty rows show nothing.
