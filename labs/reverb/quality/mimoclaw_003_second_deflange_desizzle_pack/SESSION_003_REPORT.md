# Aestra Verb Second De-Flange / De-Sizzle Pack — Session 003

Agent: **Resonance** | Operator: **Dylan**

## Changes Made (Session 002 → Session 003)

| Parameter | Session 002 | Session 003 |
|-----------|-------------|-------------|
| Default ModDepth | 0.14 | 0.10 |
| Room modDepthScalar | 0.30 | 0.15 |
| Hall modDepthScalar | 0.72 | 0.58 |
| Plate modDepthScalar | 0.70 | 0.50 |
| LFO base rates | 0.15-1.03 Hz | 0.158-1.058 Hz |
| LFO secondary rates | 0.061-0.325 Hz | 0.062-0.332 Hz |
| LFO primary/secondary blend | 55/45 | 45/55 |
| Room box-cut | -3.6 dB @ 380 Hz | -4.6 dB @ 430 Hz |
| Hall box-cut | -2.8 dB @ 480 Hz | -3.8 dB @ 520 Hz |
| Plate box-cut | -3.2 dB @ 1150 Hz | -4.2 dB @ 1250 Hz |
| Plate toneCutHz range | 6000-8500 Hz | 5500-7300 Hz |
| wetAirBlend (Room/Hall/Plate) | 0.10/0.06/0.08 | 0.07/0.045/0.055 |
| Plate post-allpass g | 0.38 | 0.32 |

## Standard Renders — Before vs After

| Source | Mode | Before Peak | After Peak | Before TailRMS | After TailRMS | Before HighRatio | After HighRatio |
|--------|------|-------------|------------|----------------|---------------|------------------|-----------------|
| snare | room | 0.243 | 0.243 | 0.0194 | 0.0194 | 0.465 | 0.466 |
| snare | hall | 0.205 | 0.204 | 0.0200 | 0.0199 | 0.327 | 0.338 |
| snare | plate | 0.183 | 0.187 | 0.0186 | 0.0185 | 0.337 | 0.350 |
| bright_ping | room | 0.256 | 0.256 | 0.0261 | 0.0262 | 0.558 | 0.558 |
| bright_ping | hall | 0.208 | 0.209 | 0.0268 | 0.0270 | 0.465 | 0.461 |
| bright_ping | plate | 0.242 | 0.240 | 0.0119 | 0.0120 | 0.620 | 0.621 |
| vocal_phrase | room | 1.422 | 1.422 | 0.3243 | 0.3243 | 0.389 | 0.390 |
| vocal_phrase | hall | 0.926 | 0.926 | 0.2726 | 0.2727 | 0.271 | 0.269 |
| vocal_phrase | plate | 1.202 | 1.202 | 0.3070 | 0.3070 | 0.332 | 0.328 |
| chord_stab | room | 0.314 | 0.314 | 0.0667 | 0.0667 | 0.013 | 0.012 |
| chord_stab | hall | 0.168 | 0.168 | 0.0520 | 0.0520 | 0.000 | 0.000 |
| chord_stab | plate | 0.276 | 0.276 | 0.0604 | 0.0604 | 0.012 | 0.009 |
| low_pulse | room | 0.426 | 0.426 | 0.0523 | 0.0523 | 0.000 | 0.000 |
| low_pulse | hall | 0.318 | 0.317 | 0.0620 | 0.0620 | 0.000 | 0.000 |
| low_pulse | plate | 0.467 | 0.467 | 0.0188 | 0.0188 | 0.000 | 0.000 |
| mix_bus | room | 0.210 | 0.210 | 0.0376 | 0.0376 | 0.045 | 0.046 |
| mix_bus | hall | 0.182 | 0.182 | 0.0379 | 0.0379 | 0.030 | 0.031 |
| mix_bus | plate | 0.249 | 0.249 | 0.0328 | 0.0328 | 0.066 | 0.067 |

## Stress Renders — Before vs After

| Source | Mode | Before Peak | After Peak | Before TailRMS | After TailRMS |
|--------|------|-------------|------------|----------------|---------------|
| stress_vocal_hall | hall | 0.926 | 0.926 | 0.2726 | 0.2727 |
| stress_vocal_plate | plate | 1.202 | 1.202 | 0.3070 | 0.3070 |
| stress_mix_hall | hall | 0.182 | 0.182 | 0.0379 | 0.0379 |
| stress_mix_plate | plate | 0.249 | 0.249 | 0.0328 | 0.0328 |
| stress_bright_plate_long | plate | 0.176 | 0.176 | 0.0024 | 0.0024 |

## File Index

All files are 48kHz 32-bit float stereo.

Naming: `{before|after}_{mode}_{source}.wav`

Audition **before** then **after** for each source/mode pair.
