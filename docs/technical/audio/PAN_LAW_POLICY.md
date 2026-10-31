# Pan law policy

Decided by the founder in FD-64 (2026-10-31): **−3 dB at centre for placing a mono source, the default and only law
for now.** This page records how that applies across the signal path, as the code does it today. It answers #405.

## The rule

A pan control does one of two different jobs, and each job has one law.

| Job | Law | Centre | Where |
|---|---|---|---|
| **Place a mono source** in the stereo field | Equal power (sine/cosine) | −3.01 dB per side (`PanLaw::kEqualPowerCenterGain`) | `PanLaw::equalPower` — `MixerBus::calculatePanGains`; preview, audition and input monitoring share the same centre gain so they sound as loud as a centred track |
| **Balance a signal that is already stereo** | Stereo balance (cosine on the far side) | Unity on both sides | `PanLaw::stereoBalance` — the channel strip's pan (`AudioEngine.cpp`, post-fader) and every send's pan |

A clip's own pan in the Audio Clip editor is a **balance** too (unity at centre), with a linear taper toward the
far side (`ClipRenderKernel.cpp`).

## Why the strip is a balance, not −3 dB

The strip used equal power once. That attenuated stereo content by 3.01 dB at centre, and made a channel's level
depend on whether it had an audible route: music routed through a channel played 3 dB quieter than music routed
straight to the master. The strip and sends were moved to `stereoBalance`; `GoldenAudioRegressionTest` and
`RoutingMasterDirectParityTest` pin it. Applying −3 dB everywhere would undo that fix, so FD-64's "−3 dB centre"
is the law for **placing a mono source**, which is what equal power is for.

## Export and live

Offline export renders through the same graph and the same laws as live playback; the parity tests above and the
offline-render regression tests cover it. There is no per-project or per-user pan-law setting, so nothing is
persisted and no migration is needed.

## Open, not decided

- The clip balance's linear taper differs from the strip's cosine taper (a clip panned halfway loses more on the far
  side than a strip panned halfway). Both are unity at centre, so a centred clip is unaffected. Aligning them would
  change existing projects that pan clips; it needs its own decision.
- A selectable pan law (per project or per user) is not planned.
