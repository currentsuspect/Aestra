# Rumble Flagship Architecture

Status: implementation contract
Date: 2026-07-15
Scope: `com.Aestrastudios.rumble`

## Product thesis

Rumble is a playable 808 instrument, not a general-purpose synth and not a sample browser. Its job is to produce a
finished low end quickly: a convincing resonant attack, a tunable and stable tail, audible harmonics on small systems,
musical mono glide, and controlled aggression without sacrificing the fundamental.

The default sound must be useful before processing. Extreme controls may be destructive; default and central control
ranges must stay smooth, pitch-true, mono-compatible, and gain-safe.

## Research findings

The physically informed TR-808 analysis by Werner, Abel, and Smith identifies the important architecture as a shaped
1 ms trigger exciting a ringing bridged-T network, a short attack-frequency shift, a later retrigger pulse, and a longer
pitch “sigh” caused by leakage. It also shows that preserved resonator state makes repeated hits vary through constructive
or destructive interference without arbitrary randomness. The authors conclude that the interactions between these
subcircuits matter more than subtle device nonlinearities.

- Paper: <https://dafx.de/paper-archive/2014/dafx14_kurt_james_werner_a_physically_informed%2C_ci.pdf>

Modern dedicated 808 instruments add workflow beyond emulation. SubLab XL emphasizes exact harmonic control, a
psychoacoustic sub engine, transient/sample layering, parallel distortion, and macro editing. Initial Audio 808 Studio 2
similarly combines a clean sub, layered sources, envelopes, keytracked filtering, several distortion types, visualization,
presets, and built-in kick clearance.

- SubLab XL: <https://futureaudioworkshop.com/sublab-xl/>
- 808 Studio 2: <https://initialaudio.com/808-studio-2-bass-synthesizer/>

For nonlinear processing, antiderivative antialiasing research confirms that naive memoryless waveshaping aliases and
that ADAA trades alias suppression for low-pass behavior and fractional delay. Mild oversampling can compensate for the
low-pass effect. Rumble will initially use measured oversampling/parallel clean-sub preservation, with ADAA reserved for
a later implementation only if the spectral probe proves it is materially better.

- ADAA paper: <https://dafx2020.mdw.ac.at/proceedings/papers/DAFx2020_paper_35.pdf>

## Baseline audit

The pre-rebuild engine is an MVP:

- one monophonic sine oscillator with a transient-only second harmonic;
- a one-stage pitch envelope rather than separate attack jump and pitch sigh;
- low-passed deterministic-looking noise for click, but seeded from wall-clock session time;
- nonlinear processing even at zero Drive and a second always-on `tanh` output stage;
- 23 parameters, while the state regression expected 22 and shifted IDs after Glide Time;
- a `std::vector` mutated by note-on and note-off inside `process()`;
- expensive parameter mapping and transcendental coefficient work in every sample;
- a four-control editor and no real preset bank.

The repaired baseline contract now proves exact repeated renders, block-size invariance, finite output, stereo equality,
safe peak level, state migration, authorization silence, and C2 tuning within 2 cents at 44.1, 48, and 96 kHz.

## Engine architecture

```text
MIDI + velocity
    -> fixed-capacity mono note stack
    -> deterministic trigger/accent pulse
    -> stateful quadrature resonator
         - short attack-frequency jump
         - longer pitch sigh
         - preserved state on active retrigger
    -> fundamental + controlled 2nd/3rd harmonics
    -> amplitude contour + transient layer
    -> parallel nonlinear path
         - true dry behavior at Drive = 0
         - clean-sub preservation
    -> time-varying low-pass tone stage
    -> deterministic band-limited click
    -> output trim
    -> DC blocker
    -> transparent safety knee
    -> identical L/R output
```

### Compatibility contract

- Plugin ID remains `com.Aestrastudios.rumble`.
- Existing parameter IDs 0 through 22 never move or change identity.
- New parameters append after ID 22.
- Legacy RMBL V1 and V2 blobs remain loadable.
- Missing JSON parameters retain constructor defaults, so version-1 states migrate additively.
- No latency is reported in the first flagship engine revision.
- The low band remains exactly mono; stereo enhancement is out of scope until a crossover/mono probe exists.

### Initial appended controls

| ID | Key | Purpose | Default |
|---:|---|---|---:|
| 23 | `Harmonics` | Adds stable 2nd/3rd harmonic translation independently of attack | 0.22 |
| 24 | `SubClean` | Preserves the clean fundamental against the parallel drive path | 0.70 |

## Quality gates

The rebuild is not “best in the game” because it has more knobs. It must pass observable gates:

1. Same state and MIDI produce the same samples across fresh instances.
2. Output is invariant to host block size for the same absolute event timing.
3. Settled pitch is within 2 cents at 44.1, 48, and 96 kHz.
4. Output is finite, DC-controlled, below the documented safety ceiling, and identical left/right.
5. Drive at zero has a true clean path; increasing Drive adds harmonics without erasing the fundamental.
6. Active retriggers preserve resonator state and do not collapse into identical copied attacks.
7. New and legacy state round-trip without moving IDs 0 through 22.
8. The audio callback performs no heap allocation, lock, logging, file I/O, or unbounded work.
9. A spectral alias probe compares nonlinear modes at multiple sample rates before any “analog” quality claim.
10. A focused performance probe reports cost per sample and guards against regressions.

## Implemented workflow

The editor presents an Impact-first sound-shaping workflow. Punch, Sweep, and Decay are the primary controls beside a
parameter-driven transient shape, while Drive, Harmonics, Clean Sub, Tone, and Output form a compact Weight + Color
section. The full preset browser groups 16 sounds across Essentials, Modern, Character, and Utility. A shared,
immutable factory bank is consumed by both the editor and headless tests so UI choices cannot drift away from
validation. Editing any parameter truthfully changes the selector state to `CUSTOM`; stepping backward or forward
applies all 25 parameters.

## Current measured evidence

Evidence below is from the 2026-07-15 Linux `RelWithDebInfo` validation build. Timing is a local comparison metric, not
a cross-machine benchmark claim.

- all 16 factory presets rendered finite, audible, exactly mono-compatible, mutually distinct, and below the 0.98
  safety ceiling;
- the hostile C7/max-drive spectral probe measured maximum fitted foldback between -36.9 and -52.1 dBc across 44.1,
  48, and 96 kHz for both nonlinear modes;
- maximum Punch raises the first 50 ms energy by at least 25% while leaving the settled tail within 15% of the
  zero-Punch render and remaining below the 0.98 safety ceiling;
- eliminating unchanged per-sample parameter remapping improved the same worst-path probe from 958 ns/sample
  (21.7x real-time) to 541 ns/sample (38.5x real-time), with the spectral measurements unchanged;
- the full premium/UI application built successfully and all 10 Rumble-focused CTests passed.

## Delivery slices

1. Foundation: truthful tests, fixed note stack, deterministic trigger seed.
2. Core tone: stateful resonator, split pitch trajectory, harmonic translation, clean/parallel drive, safety knee.
3. Workflow: Impact-first sound page, transient visualization, automation refresh, compact displays, categorized
   factory preset browser.
4. Proof: alias/DC/retrigger/performance probes, broader state and host integration tests, listening renders.

Slices 1 through 3 and the automated portion of slice 4 are implemented on `feature/rumble-flagship-rebuild`. Human
listening review remains intentionally separate from the deterministic evidence above.
