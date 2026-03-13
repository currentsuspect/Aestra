# Aestra Rumble — MVP Plan

Date: 2026-03-09
Status: In progress — core MVP path compiling and headless render validation passing
Owner: Resonance + Dylan

## Implementation snapshot (2026-03-13)

Current state of the MVP slice:

- `PluginFactory` instantiates `com.Aestrastudios.rumble`
- `RumbleInstance` compiles and renders audio successfully in headless mode
- Current parameter set is:
  - Decay
  - Drive
  - Tone
  - Output Gain
- State serialization now includes a magic/version header for safer restore behavior
- MVP note handling is intentionally mono + retrigger with decay-led 808 behavior
- `RumbleRenderTest` now checks:
  - non-silent output
  - no NaN/Inf values
  - sane peak ceiling
  - tail decay behavior
  - preset differentiation between clean and driven renders
- `RumbleStateTest` covers:
  - state save/load round-trip
  - corrupt-size rejection
  - invalid magic rejection
  - unsupported version rejection
- `RumblePluginFactoryTest` covers:
  - in-process factory instantiation for `com.Aestrastudios.rumble`
  - plugin metadata sanity
  - basic initialization and parameter exposure
- `RumbleUsagePathTest` covers:
  - plugin-manager lifecycle
  - factory-backed instance creation through normal manager flow
  - MIDI note input
  - real rendered audio buffer sanity
- `RumbleDiscoveryTest` covers:
  - normal manager lookup via `findPlugin(...)`
  - instrument-list presence
  - `createInstanceById(...)` for the internal Rumble id
- `InternalPluginProjectRoundTripTest` covers:
  - unit creation + plugin attachment
  - project save/load persistence for internal instrument units
  - restored plugin state after project reload

This means Rumble has crossed from concept into a real, testable internal instrument slice.

## Why this exists

Rumble should not start life as a vague "cool 808 plugin" idea. It should start as a **small, testable internal instrument** that proves:

1. Aestra can host and process a real internal instrument reliably
2. The plugin path is useful from the terminal / headless workflows
3. We can validate sound generation without depending on unfinished UI

This plan is intentionally aligned with the current beta task list:

- **EPIC J** — export / offline render that matches playback
- **EPIC K** — audio resilience and RT-safe behavior
- **EPIC M** — plugin decision gate / minimal plugin posture
- **EPIC O/K-style testability goals** — trust through build, tests, and deterministic rendering

Rumble MVP is therefore both:
- a musical feature
- and a systems-validation tool for Aestra’s internal plugin architecture

---

## Product framing

**Rumble MVP is not a finished flagship synth.**

It is a **minimal internal 808 bass instrument** focused on:
- sub generation
- controllable decay
- basic character / drive
- deterministic offline rendering
- terminal-driven validation

The goal is to make something that is musically recognizable as an 808-style bass and easy to verify from code.

---

## Scope

### In scope for MVP

- Internal instrument plugin implementing `IPluginInstance`
- Mono voice model (single active note policy or simple retrigger)
- MIDI note input
- Sine-based core oscillator
- Fast attack + variable decay envelope
- Optional soft drive / saturation
- Simple tone shaping filter
- Stereo output (duplicated mono is acceptable for MVP)
- State save/load for basic parameters
- Headless render harness or equivalent terminal-driven test path
- WAV-based verification workflow

### Explicitly out of scope for MVP

- Full graphical editor
- Preset browser
- Polyphonic performance design
- Advanced modulation matrix
- Multiple oscillator types beyond what is needed
- Glide / portamento unless the core path already works
- Complex FX chain
- Full DAW UI integration polish
- Fancy branding / artwork

---

## Success criteria

Rumble MVP is successful if all of the following are true:

1. `PluginFactory` can instantiate `com.Aestrastudios.rumble`
2. The plugin initializes and processes audio without crashes
3. A note-on event produces audible low-frequency output
4. Parameter changes materially affect output
5. Offline render to WAV works reproducibly
6. Output passes basic sanity checks:
   - non-silent
   - no NaNs / infinities
   - no catastrophic clipping by default
   - envelope decays as expected
7. At least one automated or semi-automated render-validation path exists

---

## Architecture fit

Aestra already has the key integration point:

- `PluginFactory.cpp` expects internal ID `com.Aestrastudios.rumble`
- factory target class: `Aestra::Plugins::RumbleInstance`

That means the MVP should fit into the existing internal plugin model rather than inventing a special case.

### Expected plugin identity

- **ID:** `com.Aestrastudios.rumble`
- **Name:** `Aestra Rumble`
- **Vendor:** `Aestra Studios`
- **Format:** `Internal`
- **Type:** `Instrument`
- **Outputs:** 2
- **MIDI Input:** true
- **Editor:** false for MVP

---

## MVP signal path

Recommended v0 signal path:

`MIDI note -> note frequency -> sine oscillator -> amp envelope -> drive -> tone filter -> output gain -> stereo out`

This is enough to generate:
- clean sub bass
- dirty 808 bass
- short punchy bass
- long tail bass

### Why this path

It is:
- simple
- cheap to compute
- easy to reason about
- easy to test offline
- enough to judge whether the plugin is worth expanding

---

## Voice model

### MVP recommendation

Use **mono / retriggering** behavior first.

That means:
- one active voice at a time
- new note-on retriggers envelope and pitch
- note-off may either release or be ignored if decay-only behavior is desired

### Why mono first

- closer to classic 808 use
- simpler RT-safe implementation
- fewer edge cases
- easier waveform analysis
- faster to validate musically

Polyphony can come later if there is a strong reason.

---

## Parameters

Keep the initial parameter set extremely small.

### Required MVP params

1. **Decay**
   - Controls tail length
   - Core 808 behavior

2. **Drive**
   - Soft saturation amount
   - Needed for character testing

3. **Tone**
   - Simple low-pass brightness/dampening control
   - Lets us test low-end vs harmonics balance

4. **Output Gain**
   - Keeps renders sane and testable

### Optional next param

5. **Pitch Fine**
   - Small detune amount if needed

### Deferred params

- Glide / portamento
- Attack shaping beyond fast attack
- Click transient / punch layer
- Distortion mode switching
- EQ section
- sub/upper harmonic blending

---

## DSP design notes

### Oscillator

Start with a sine oscillator.

Implementation notes:
- phase accumulator
- frequency derived from MIDI note
- stable at 44.1k / 48k / higher rates
- avoid denormals

### Envelope

Minimal ADS-ish shape is enough, but MVP can be simpler:
- near-instant attack
- variable decay
- optional short release

Practical MVP behavior:
- note-on resets phase optionally or retriggers envelope
- gain decays exponentially or linearly toward zero

### Drive

Soft clip is enough for v0:
- `tanh`
- or a cheap polynomial soft saturator

Important:
- keep output bounded
- avoid super loud defaults

### Tone filter

One-pole low-pass is enough.

Goal is not analog realism — goal is quick usefulness and clear effect during tests.

---

## Real-time safety expectations

Rumble must respect Aestra’s plugin contract.

### In process()

Must avoid:
- allocations
- locks
- file I/O
- exceptions
- logging spam
- unstable branching dependent on dynamic container mutation

### Safe state model

- parameter storage via atomics or fixed trivially-safe structure
- fixed voice state
- no dynamic memory churn in the render path

---

## State serialization

For MVP, state only needs to store:
- decay
- drive
- tone
- output gain

Format can be simple and internal.

Done means:
- saveState() returns enough to restore the plugin sound
- loadState() restores correctly without weird defaults drift

---

## Headless / terminal validation strategy

This is a central part of the plan, not an afterthought.

### Preferred approach

Create a small **Rumble render harness** that:
- instantiates `RumbleInstance`
- initializes plugin at sample rate + block size
- sends MIDI note events via `MidiBuffer`
- renders N seconds to a WAV file

This can be a standalone test executable or a dedicated headless utility.

### Minimum test cases

1. **C2 clean**
   - medium decay
   - low drive
2. **C2 driven**
   - same note, higher drive
3. **A#1 long decay**
   - lower pitch, longer tail

### What to inspect

- non-silent output
- peak value
- RMS value
- tail decay behavior
- obvious clipping
- deterministic repeatability
- audible/visible difference when parameters change

---

## Suggested implementation order

### Phase 1 — plugin skeleton

Deliverables:
- `RumbleInstance.h`
- `RumbleInstance.cpp`
- metadata + parameter plumbing
- factory path returns instance

Done when:
- plugin compiles
- factory creates it
- dummy process path produces silence safely

### Phase 2 — core sound generation

Deliverables:
- sine oscillator
- mono voice
- MIDI note-on handling
- decay envelope
- stereo output

Done when:
- a note generates a basic sub tone

### Phase 3 — character controls

Deliverables:
- drive
- tone filter
- gain normalization

Done when:
- at least 2–3 distinct, useful 808-style tones are renderable

### Phase 4 — terminal harness

Deliverables:
- headless render target for Rumble
- WAV output
- reproducible CLI usage

Done when:
- Rumble can be tested from terminal without DAW UI

### Phase 5 — validation / regression

Deliverables:
- sanity checks or scripted render comparisons
- parameter smoke tests
- at least one repeatable regression workflow

Done when:
- we can detect breakage without listening manually every time

---

## Verification matrix

### Functional checks

- plugin can be instantiated
- plugin activates
- MIDI note-on changes output
- note-off / decay behavior is sane
- parameters update correctly

### Audio sanity checks

- output not silent
- no NaNs / infs
- peak below dangerous threshold by default
- no hard clipping under default settings
- tail reaches near-silence after expected time

### Regression checks

- same input -> same output within tolerance
- parameter change -> measurable output difference
- different MIDI note -> different dominant pitch / cycle length

---

## Relationship to beta roadmap

Rumble MVP should **not** expand scope uncontrollably.

Its value to the broader product is:
- proving internal instrument hosting
- strengthening offline render workflows
- exercising plugin lifecycle code
- giving Aestra one real musical instrument to test with

If it remains small and test-driven, it supports the roadmap.
If it balloons into a flagship synth before the basics are proven, it becomes a distraction.

---

## Recommended next action

Proceed immediately with:
1. scaffold `RumbleInstance`
2. make it produce a sine-based bass tone
3. add a terminal render test path
4. validate output with WAV renders

That is the shortest path from idea -> proof.

---

## Final constraint

If a choice appears between:
- more features
n- or more testability / determinism

choose **testability / determinism** for MVP.

That will tell us faster whether Rumble deserves to become a serious Aestra instrument.
