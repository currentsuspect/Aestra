# Aestra EQ Dynamic Band Plan

## Purpose

Advanced EQ workflows expose many more bands than Aestra EQ currently does. Aestra now has a polished six-band V1 surface, but the implementation is still fixed around those six public bands. Moving toward a 24-band advanced EQ must be treated as an architecture migration, not a constant change.

This note records the current six-band assumptions and the safe path to a 24-slot model without breaking existing plugin state, automation, or real-time audio constraints.

## Advanced EQ Reference Signals

Modern advanced EQ depth is not only "more bands." The workflow combines:

- A fixed 24-band capacity target, with explicit compatibility handling when saved states move between band capacities.
- Six filter structures: minimum phase, state variable, parallel, matched phase, mixed phase, and zero phase.
- Eight filter types: peak, low shelf, low pass, high shelf, high pass, notch, band pass, and tilt shelf.
- Five stereo placements: stereo, left, right, mid, and side.
- Seven slopes: 6, 12, 24, 36, 48, 72, and 96 dB/oct.
- Graph-first band creation by double-clicking the spectrum.
- A selected-band floating window attached to the graph node.
- Right-click actions for invert gain, split L/R, split M/S, copy, and paste.
- Multi-band selection where edits can apply across selected bands.
- Optional analyzer collision areas.
- EQ match with source, target, and difference curves, plus a fitting step and adjustable band count.
- User-adjustable control behavior for wheel sensitivity, drag sensitivity, and rotary slider style.

The near-term Aestra takeaway: adding 18 more bands is necessary but not sufficient. The competitive gap is the combination of slot capacity, graph-native creation, multi-select editing, split stereo workflows, dynamic/EQ-match allocation, and precise control feel.

## Current Fixed-Band Constraints

| Area | Current Constraint | Risk If Changed Casually |
| --- | --- | --- |
| Public DSP band count | `AestraEQ::kV1BandCount = 6` | Existing loops, mappings, and tests assume six active bands. |
| UI card count | `AestraEQEditor::kNumBands = 6` | Layout, hit testing, graph labels, and card rendering are sized for six cards. |
| Parameter IDs | Fixed HP/LS/B1/B2/HS/LP parameter IDs plus six stereo placement IDs | Changing IDs breaks automation and saved plugin state. |
| State blob | `EQStateBlobV6` stores `params[33]` | Expanding the blob needs an explicit V7 loader and V6 migration path. |
| Filter storage | `2 * kV1BandCount * kMaxFilterStages` filters | A 24-band model needs preallocated storage sized for worst-case RT processing. |
| Filter state mapping | `bandV1*Id()` helpers hardcode six mappings | Dynamic slots need slot-to-parameter mapping helpers. |
| Response drawing | UI response and node rendering iterate six bands | Advanced mode needs virtualized or compact rendering for 24 nodes/cards. |
| Tests | EQ tests loop over `kV1BandCount` | New tests must verify both V6 compatibility and 24-slot behavior. |

## Non-Negotiable Constraints

- Preserve the existing plugin ID: `com.Aestrastudios.eq`.
- Preserve V6 state loading exactly: old six-band projects must sound the same after migration.
- Do not change existing parameter IDs in place.
- Do not allocate, lock, log, sleep, or perform file I/O in the audio processing path.
- Keep all extra filter memory preallocated.
- Keep disabled extra slots neutral and no-op.
- Make automation behavior explicit before exposing extra bands to hosts.

## Target Model

Introduce a slot-based model:

```cpp
static constexpr uint32_t kLegacyBandCount = 6;
static constexpr uint32_t kMaxDynamicBands = 24;

struct EQBandSlot {
    uint32_t slotIndex;
    bool enabled;
    FilterType type;
    float frequencyNorm;
    float gainNorm;
    float qOrSlopeNorm;
    StereoMode stereoMode;
};
```

The first six slots map from the current V6 bands:

| Slot | Legacy Band |
| --- | --- |
| 0 | HP |
| 1 | LS |
| 2 | B1 |
| 3 | B2 |
| 4 | HS |
| 5 | LP |

Slots 6-23 default to disabled neutral bell bands until the user adds or enables them. EQ match and future assistive tools should allocate from disabled slots rather than mutating existing user bands without intent.

## Automation Strategy

The safest host-facing strategy is a fixed 24-slot parameter block:

- Every slot has stable enable, type, frequency, gain, Q/slope, and stereo placement parameters.
- Legacy parameter IDs remain unchanged for slots 0-5.
- New V7 parameter IDs append slots 6-23 after the current V6 parameter range.
- Hidden/disabled slots are still valid automation targets, but default disabled.

Avoid a variable-length vector-only state model for anything host-automatable. It would make DAW automation lanes unstable when bands are inserted or removed.

## State Migration

Add `EQStateBlobV7` with:

- Magic/version for V7.
- A fixed `kMaxDynamicBands` slot array or fixed parameter array.
- Optional visible/active slot count for UI presentation only.

Load behavior:

1. V7 loads all 24 slots.
2. V6 loads current parameters into slots 0-5.
3. V6 initializes slots 6-23 as disabled neutral slots.
4. Older state versions continue through the existing compatibility path, then map into the six legacy slots.

Save behavior:

1. Save V7 once 24-slot support ships.
2. Do not attempt to down-save to V6.
3. Document the version bump in internal release notes before shipping a build that writes V7.

## UI Direction

The graph can show 24 nodes, but the bottom card row must not scale by squeezing 24 miniature editors into the current space. That model only works while every band is permanently visible and there are only six bands. With 24 bands, labels become unreadable and every future feature multiplies layout complexity.

The target UI model is:

```text
[ Graph overview ]

B1 B2 B3 B4 B5 B6 B7 B8 ... B24

Selected: B7
Type: Bell
Freq: 2.4 kHz
Gain: +3.2 dB
Q: 1.40

Advanced
- Stereo
- Dynamic
- Sidechain
- Solo Band
```

The graph is the overview. The selected-band inspector is the editor.

Recommended UI:

- Keep the curve graph as the primary editing surface.
- Replace the fixed card row with a compact band strip for selection and status.
- Show one selected-band inspector with full controls instead of 24 miniature editors.
- Make the inspector collapsible so users can trade editing controls for more graph/analyzer height.
- Add `+ Band` and `Duplicate` actions that enable the next disabled slot.
- Add graph creation using the same disabled-slot allocator: once the full 24-band model is live, clicking an empty point on the EQ graph should add/select a new band at that frequency and gain. This should not require reaching for the bottom strip.
- Keep node clicks reserved for selecting existing bands; graph creation only applies when the pointer is not over an existing node, label, tooltip, menu, or inspector control.
- If single-click creation proves too easy to trigger accidentally in practice, gate it behind double-click or a modifier, but the target workflow is "click the graph where you want the band."
- Add multi-select before EQ match so grouped edits and later fit results have a natural selection model.
- Add split L/R and split M/S actions once the slot allocator exists: duplicate the selected slot, set the original to Left or Mid, and set the duplicate to Right or Side.
- Keep hover minimal: a tiny readout for band ID, frequency, gain, and Q is useful, but editing should be click-centric.
- Click on a graph node or band strip item selects the band and updates the inspector.
- Double-click can open rename/color or exact-edit affordances later, but hover should not be required for editing.
- Keep default view focused on the six legacy bands for simple projects.

## Implementation Order

1. Add neutral slot metadata helpers while preserving current six-band behavior.
2. Rename internal concepts from "V1 band" to "legacy slot" where it improves clarity, without changing public behavior.
3. Add preallocated 24-slot filter storage and runtime arrays.
4. Add V7 state loading/saving with V6 migration.
5. Append host parameters for slots 6-23.
6. Update DSP loops to iterate `kMaxDynamicBands`, skipping disabled slots.
7. Update graph rendering and hit testing for 24 slots.
8. Replace the six-card fixed row with a compact band strip plus selected-band inspector.
9. Add add/delete/duplicate slot actions.
10. Add graph click creation for empty graph points using the disabled-slot allocator.
11. Add multi-select editing.
12. Add split L/R and split M/S actions.
13. Let EQ match allocate into disabled slots.

## Future Work

These are feature backlog items, not blockers for the current EQ PR:

- EQ match using source, target, and difference curves, with fit results allocated into disabled dynamic slots.
- Deeper dynamic EQ and sidechain controls, including clearer detector targeting, range, threshold, timing, and linked/unlinked behavior.
- Multiple filter structures and phase modes, including explicit minimum-phase, matched/mixed-phase, and zero-phase design decisions with latency reporting.
- Gain-compensation and output utility controls, including clearer output staging, polarity, and audition-safe comparison behavior.

## Required Tests

- V6 state roundtrip still passes through the V6 loader path and produces identical parameter values for slots 0-5.
- V6-to-V7 migration leaves slots 6-23 disabled and neutral.
- All 24 slots enabled with bell filters processes silence without NaN/Inf output.
- All 24 slots enabled at extreme gain remains finite and bounded.
- Disabled slots do not alter output.
- Existing automation IDs for the first six bands remain unchanged.
- V7 save/load preserves all 24 slot states.
- UI hit testing can select each of the 24 slots.
- Copy/duplicate/reset behavior works for slots above index 5.

## First Safe Code Slice

The first implementation slice should not expose 24 bands in the UI. It should only introduce internal naming and mapping helpers that make the six legacy slots explicit. That gives the next slice a stable place to add the remaining 18 slots without mixing parameter migration, DSP storage expansion, and UI changes in one patch.
