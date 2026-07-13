# AestraVerb Stereo Characterization (F5/F6 re-measurement)

Measured on the current engine (post SIMD ring-fix, post F4 modulation rewrite). Supersedes the review's F5/F6 numbers, which were taken on mislabeled modes and the old modulator. 48 kHz, Width 0.7, Decay 0.7, Size 0.6.

## Per-mode image

| Mode | EarlyCorr | LateCorr | Width | MonoRet(dB) | OnsetLag(smp) | EarlyLvlDiff(dB) |
|------|-----------|----------|-------|-------------|---------------|------------------|
| room | 0.176 | -0.339 | 1.421 | -4.37 | 1.0 | 0.71 |
| hall | 0.490 | -0.039 | 1.040 | -2.94 | 1.0 | 0.60 |
| plate | 0.056 | -0.371 | 1.471 | -4.35 | 1.0 | 0.41 |
| cathedral | 0.231 | -0.376 | 1.484 | -4.90 | 1.0 | 0.84 |
| chamber | 0.148 | -0.359 | 1.457 | -5.03 | 1.0 | 0.71 |
| bright_hall | 0.182 | -0.355 | 1.449 | -4.86 | 1.0 | 0.48 |
| ambience | 0.070 | -0.418 | 1.559 | -5.13 | 1.0 | 0.60 |
| scoring | 0.234 | -0.332 | 1.412 | -4.60 | 1.0 | 0.78 |
| smooth_plate | 0.079 | -0.370 | 1.475 | -4.89 | 1.0 | 0.46 |

**Column meaning.** EarlyCorr: inter-channel correlation over the 20 ms after onset (impulse). 1.0 = mono onset (the F5 concern). LateCorr: correlation of the reverb tail on decorrelated dense noise; strongly negative risks mono cancellation (the F6 concern). Width: side/mid RMS on the tail. MonoRet: mono fold-down RMS relative to L; 0 dB = no loss, negative = cancellation on mono sum. OnsetLag: best L->R cross-correlation lag. EarlyLvlDiff: L vs R early RMS.

## Static geometry (code-level cause)

- FDN injection vectors L/R correlation: **-0.380**
- FDN output vectors L/R correlation: **-0.557**
- Early reflections: R tap delay = L tap delay + `((tap%3)+1)` samples (1-3 smp, ~0.02-0.06 ms at 48 kHz); same gain both channels; cross-mix `earlyL += 0.28*R`, `earlyR += -0.22*L` per tap.
