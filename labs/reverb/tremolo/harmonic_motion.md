# AestraVerb Harmonic-Motion Measurement

Steady 1/6-octave tone comb held through the reverb; each tone's level is tracked over time and its dB swing measured (typical = median over tones, worst = max). This captures the FDN peaks/notches moving across sustained tones. Budget target: typical <= ~0.5 dB, worst-harmonic <= ~2-3 dB.

## Mod-depth sweep (damping 0.5)

| Mode | depth | Typical dB | Worst dB | Worst Hz |
|------|-------|-----------|----------|----------|
| room | 0.00 | 0.10 | 6.19 | 190 |
| room | 0.07 | 1.74 | 6.99 | 6842 |
| room | 0.14 | 2.31 | 6.65 | 6842 |
| room | 0.30 | 3.48 | 8.90 | 1358 |
| hall | 0.00 | 0.35 | 6.16 | 240 |
| hall | 0.07 | 2.56 | 6.48 | 7680 |
| hall | 0.14 | 3.40 | 8.10 | 1358 |
| hall | 0.30 | 4.20 | 7.49 | 2155 |
| plate | 0.00 | 0.06 | 5.63 | 269 |
| plate | 0.07 | 2.02 | 5.54 | 269 |
| plate | 0.14 | 2.68 | 6.50 | 7680 |
| plate | 0.30 | 3.75 | 7.03 | 5431 |
| smooth_plate | 0.00 | 0.07 | 6.63 | 214 |
| smooth_plate | 0.07 | 1.36 | 6.62 | 339 |
| smooth_plate | 0.14 | 2.06 | 7.34 | 4838 |
| smooth_plate | 0.30 | 2.93 | 6.48 | 214 |
