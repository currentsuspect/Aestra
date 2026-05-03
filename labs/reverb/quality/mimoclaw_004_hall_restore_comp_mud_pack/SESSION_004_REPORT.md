# Aestra Verb Hall Restore + Wet Comp + Mud Cleanup — Session 004

Agent: **Resonance** | Operator: **Dylan**

## Changes Made (Session 003 → Session 004)

### Hall Restore

| Parameter | Session 003 | Session 004 |
|-----------|-------------|-------------|
| Hall modDepthScalar | 0.58 | 0.68 |
| Hall box-cut | -3.8 dB @ 520 Hz | -3.0 dB @ 520 Hz |
| Hall wetAirBlend | 0.045 | 0.058 |

### Wet Compensation

| Mode | Compensation Gain | dB |
|------|-------------------|----|
| Room | 1.096 | +0.8 dB |
| Hall | 1.047 | +0.4 dB |
| Plate | 1.122 | +1.0 dB |

### Mud Cleanup

| Mode | HP Cutoff | Blend |
|------|-----------|-------|
| Room | 110 Hz | 0.70 |
| Hall | 65 Hz | 0.40 |
| Plate | 100 Hz | 0.60 |

One-pole HP filter applied after wet makeup gain, before Plate post-allpass.

## Standard Renders

| Source | Mode | Peak | RMS | TailRMS | HighRatio | LowEnergy% | MidEnergy% |
|--------|------|------|-----|---------|-----------|------------|------------|
| snare | room | 0.267 | 0.0313 | 0.0194 | 0.493 | 1.9 | 40.2 |
| snare | hall | 0.222 | 0.0263 | 0.0209 | 0.347 | 6.7 | 51.5 |
| snare | plate | 0.210 | 0.0333 | 0.0192 | 0.380 | 3.0 | 44.4 |
| bright_ping | room | 0.282 | 0.0542 | 0.0289 | 0.589 | 0.0 | 10.5 |
| bright_ping | hall | 0.222 | 0.0435 | 0.0286 | 0.481 | 0.0 | 12.4 |
| bright_ping | plate | 0.269 | 0.0413 | 0.0135 | 0.654 | 0.0 | 18.0 |
| vocal_phrase | room | 1.315 | 0.2799 | 0.3153 | 0.396 | 45.5 | 54.5 |
| vocal_phrase | hall | 0.991 | 0.2420 | 0.2862 | 0.313 | 34.3 | 65.7 |
| vocal_phrase | plate | 1.205 | 0.2929 | 0.3171 | 0.348 | 42.2 | 57.8 |
| chord_stab | room | 0.327 | 0.0790 | 0.0701 | 0.019 | 3.4 | 96.6 |
| chord_stab | hall | 0.188 | 0.0529 | 0.0578 | 0.001 | 0.0 | 100.0 |
| chord_stab | plate | 0.298 | 0.0765 | 0.0659 | 0.017 | 0.8 | 99.2 |
| low_pulse | room | 0.282 | 0.0716 | 0.0368 | 0.000 | 100.0 | 0.0 |
| low_pulse | hall | 0.277 | 0.0736 | 0.0563 | 0.000 | 100.0 | 0.0 |
| low_pulse | plate | 0.370 | 0.0525 | 0.0150 | 0.000 | 100.0 | 0.0 |
| mix_bus | room | 0.205 | 0.0483 | 0.0396 | 0.036 | 14.3 | 78.2 |
| mix_bus | hall | 0.165 | 0.0404 | 0.0419 | 0.029 | 28.6 | 64.0 |
| mix_bus | plate | 0.202 | 0.0456 | 0.0358 | 0.053 | 30.7 | 64.0 |

## Stress Renders

| Source | Mode | Peak | RMS | TailRMS | LowEnergy% |
|--------|------|------|-----|---------|------------|
| stress_vocal_hall | hall | 0.991 | 0.2420 | 0.2862 | 34.3 |
| stress_vocal_plate | plate | 1.205 | 0.2929 | 0.3171 | 42.2 |
| stress_mix_hall | hall | 0.165 | 0.0404 | 0.0419 | 28.6 |
| stress_mix_plate | plate | 0.202 | 0.0456 | 0.0358 | 30.7 |
| stress_low_pulse_hall | hall | 0.277 | 0.0736 | 0.0563 | 100.0 |
| stress_low_pulse_room | room | 0.282 | 0.0716 | 0.0368 | 100.0 |
| stress_bright_plate_long | plate | 0.196 | 0.0171 | 0.0027 | 0.0 |

## File Index

All files are 48kHz 32-bit float stereo.
Naming: `after_{mode}_{source}.wav`
