# AestraVerb Gain Staging & Output Headroom (N4)

Status: **decided** (owner, 2026-07-13) — headroom policy, documented; no clamp.

## The observation

With a full-wet mix (Mix = 100%), the AestraVerb wet output can peak **above
unity** on hot input. Measured wet peaks for a loud dry vocal:

| Mode  | Wet peak |
|-------|---------:|
| Room  | ~1.56 |
| Plate | ~1.38 |
| Hall  | ~1.03 |

These come from the wet makeup/compensation gains (`kWetMakeupGain`,
`kWetCompGain[mode]`) applied so each mode reaches a comparable subjective level,
combined with dense constructive peaks in the FDN tail.

## Why this is not a bug

The reverb runs in 32-bit float. Nothing in the path clamps at ±1: `sanitize()`
only flushes NaN/denormals and mutes true blow-ups (`|value| >= 32`). So a wet
peak of 1.56 is **headroom, not distortion** — it reproduces cleanly and is
attenuated by any downstream gain, the mixer fader, or the plugin's own Mix
control below 100%.

The lab-only over-unity diagnostics (`AESTRA_REVERB_DIAGNOSTICS`) count samples
at or above unity precisely so this is measurable — they are **not** a clip
count.

## Decision

**Headroom policy.** Over-unity wet output is accepted as intentional float
headroom; downstream gain-staging is the user's responsibility, as with any
send/insert effect that can sum to more than unity. The engine does **not**
clamp the wet to ±1, because:

- it would add distortion where there is currently none;
- it would flatten the per-mode level relationships (`kWetCompGain`) that the
  voicing depends on;
- float delivery to the mixer means the peak is harmless.

No code change accompanies this decision — it records why the levels are left as
they are. If a future "bounded output" mode is ever wanted (wet peaks kept
<= unity), it would be a deliberate voicing pass that recalibrates
`kWetMakeupGain` / `kWetCompGain` together and re-checks every mode by ear; that
is out of scope here.
