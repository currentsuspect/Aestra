# AestraVerb Tremolo / Tail-Pump Measurement

50 ms broadband burst then silence; the decaying tail's dB envelope is detrended and the residual ripple measured. Default settings (Random mod, depth 0.3, damping 0.5, Mix 100%). RippleDb = RMS pump of the tail around its smooth decay (dB); WobbleHz = its dominant rate; the mod-OFF column is the unmodulated FDN baseline; the band columns split the output into low/mid/high.

| Mode | WobbleHz | RippleDb (mod on) | RippleDb (mod OFF) | Low | Mid | High |
|------|----------|-------------------|--------------------|-----|-----|------|
| room | 24.17 | 1.68 | 1.80 | 1.91 | 1.65 | 1.67 |
| hall | 0.73 | 1.77 | 1.69 | 2.05 | 1.68 | 1.72 |
| plate | 0.73 | 2.11 | 2.35 | 2.31 | 2.08 | 2.06 |
| cathedral | 0.73 | 1.83 | 1.84 | 1.93 | 1.72 | 1.88 |
| chamber | 13.18 | 1.68 | 1.72 | 1.88 | 1.66 | 1.72 |
| bright_hall | 0.73 | 1.66 | 1.74 | 1.86 | 1.61 | 1.67 |
| ambience | 21.97 | 1.72 | 1.59 | 1.93 | 1.71 | 1.60 |
| scoring | 0.73 | 1.83 | 1.74 | 2.01 | 1.77 | 1.80 |
| smooth_plate | 0.73 | 2.24 | 2.18 | 2.49 | 2.11 | 2.08 |

## Modulation-depth effect on tail ripple (does the LFO smear the beating?)

| Mode | Ripple @depth 0 | @depth 0.3 | @depth 1.0 |
|------|-----------------|------------|------------|
| hall | 1.69 | 1.77 | 1.72 |
| plate | 2.35 | 2.11 | 2.20 |
| room | 1.80 | 1.68 | 1.75 |
| smooth_plate | 2.18 | 2.24 | 2.20 |

## Damping exposure (tail ripple dB at default 0.5 vs low 0.1 damping)

| Mode | Ripple @damp 0.5 | Ripple @damp 0.1 |
|------|------------------|------------------|
| plate | 2.11 | 1.14 |
| hall | 1.77 | 1.05 |
| room | 1.68 | 1.11 |
