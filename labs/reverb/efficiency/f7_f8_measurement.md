# AestraVerb F7/F8 Efficiency Measurement

Measured on develop (post F4/F5/F6) with AestraReverbBenchmark. This is the
evidence behind two decisions: **do F8 (tail dormancy)**, and **skip the F7
dirty-domain control-cache refactor**.

## Baseline cost (active, profiling off)

| Mode  | CPU budget / 256-frame callback | Realtime |
|-------|--------------------------------:|---------:|
| Room  | 2.32% | 43x |
| Hall  | 3.07% | 33x |
| Plate | 3.26% | 31x |

Already fast — one active instance is ~3% of a callback on this dev machine.

## Stage profile (proportions; AESTRA_REVERB_PROFILE, Room)

| Stage | % of total |
|-------|-----------:|
| FDN Delay Read | 27.3 |
| FDN Feedback/Matrix | 15.7 |
| Early Reflections | 12.7 |
| Output/Mix | 12.5 |
| Diffuser | 8.9 |
| **LFO Normalize + Control** | **6.9** |
| Input/Predelay | 5.5 |
| Modulation/LFO | 5.3 |
| Parameter Smoothing | 5.3 |

The per-sample FDN pipeline (delay read + feedback + early + output + diffuser)
is ~78% of the cost. The control cache lives inside the 6.9% "LFO Normalize +
Control" stage and runs only once per 64 samples, so `updateControlCache` is a
small fraction of that 6.9%.

## Decision: skip the F7 dirty-domain control-cache refactor

Splitting `updateControlCache` into per-domain dirty recomputation targets a
sub-fraction of a 6.9% stage on a plugin already at ~3% budget. The realizable
saving is a fraction of a percent of a callback, against real regression risk
(coefficients not updating when they should). Not worth it. The cheap, safe F7
items remain candidates for a later pass (synced-predelay per-sample BPM reload
-> control rate; SIMD dispatch is already hoisted to a `static` check).

## Decision: do F8 tail dormancy

An **idle** instance (silent input, tail decayed) still runs that entire ~78%
FDN pipeline every sample. Dormancy skips it. Measured with the dormancy test:

- Dormant span vs active span: **~68x cheaper** (silent input, instance asleep).
- Transparent: silent input -> silent output; woken early reflections identical
  to a fresh instance (corr 1.000); energy envelope within ~0.8 dB.
- Measurement-safe: the dormant floor is -140 dBFS, below the tail-decay and
  modulation-purity probes' analysis range, so it never shortens a measured T60
  or splatters into the tail.

This is the high-value efficiency win for multi-instance sessions on the 4 GB /
low-core target: N idle reverb sends cost almost nothing.
