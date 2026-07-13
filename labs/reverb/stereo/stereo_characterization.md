# AestraVerb Stereo Characterization (F5/F6 re-measurement)

Measured on the current engine (post SIMD ring-fix, post F4 modulation rewrite, post F6 output-vector mono-compat fix). Supersedes the review's F5/F6 numbers, which were taken on mislabeled modes and the old modulator. For the pre-F6-fix baseline and the candidate comparison, see f6_candidates.md. 48 kHz, Width 0.7, Decay 0.7, Size 0.6.

## Per-mode image

| Mode | EarlyCorr | LateCorr | Width | MonoRet(dB) | OnsetLag(smp) | EarlyLvlDiff(dB) |
|------|-----------|----------|-------|-------------|---------------|------------------|
| room | 0.176 | -0.136 | 1.146 | -3.29 | 1.0 | 0.71 |
| hall | 0.490 | 0.186 | 0.829 | -2.20 | 1.0 | 0.60 |
| plate | 0.056 | -0.195 | 1.216 | -3.41 | 1.0 | 0.41 |
| cathedral | 0.231 | -0.162 | 1.177 | -3.80 | 1.0 | 0.84 |
| chamber | 0.148 | -0.130 | 1.139 | -3.85 | 1.0 | 0.71 |
| bright_hall | 0.182 | -0.129 | 1.139 | -3.73 | 1.0 | 0.48 |
| ambience | 0.070 | -0.219 | 1.249 | -4.01 | 1.0 | 0.60 |
| scoring | 0.234 | -0.117 | 1.124 | -3.55 | 1.0 | 0.78 |
| smooth_plate | 0.079 | -0.158 | 1.173 | -3.76 | 1.0 | 0.46 |

**Column meaning.** EarlyCorr: inter-channel correlation over the 20 ms after onset (impulse). 1.0 = mono onset (the F5 concern). LateCorr: correlation of the reverb tail on decorrelated dense noise; strongly negative risks mono cancellation (the F6 concern). Width: side/mid RMS on the tail. MonoRet: mono fold-down RMS relative to L; 0 dB = no loss, negative = cancellation on mono sum. OnsetLag: best L->R cross-correlation lag. EarlyLvlDiff: L vs R early RMS.

## Static geometry (code-level cause)

- FDN injection vectors L/R correlation: **-0.380**
- FDN output vectors L/R correlation: **-0.291**
- Early reflections: R tap delay = L tap delay + `((tap%3)+1)` samples (1-3 smp, ~0.02-0.06 ms at 48 kHz); same gain both channels; cross-mix `earlyL += 0.28*R`, `earlyR += -0.22*L` per tap.
