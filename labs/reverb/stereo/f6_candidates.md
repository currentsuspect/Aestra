# AestraVerb F6 — Stereo Fix Candidate Comparison

Measured on the current engine (post ring-fix, post F4 modulation rewrite) by
patching the FDN output geometry and rebuilding — no permanent engine change was
committed to produce these numbers. Same test as `stereo_characterization.md`
(48 kHz, Width 0.7, decorrelated dense-noise tail).

Goal: reduce the F6 mono fold-down loss (baseline −4.4 to −5.1 dB on every mode
except Hall) toward Hall's well-behaved ~−2.9 dB, without collapsing width.

## Candidates

- **Baseline** — current `kOutputR` (L/R vector correlation −0.557), current `kDecorr`.
- **A — orthogonalized output vectors** — `kOutputR` re-derived orthogonal to
  `kOutputL` (vector correlation 0.000, same norm); `kDecorr` unchanged.
- **B — mild output vectors** — `kOutputR` = halfway between current and
  orthogonalized (vector correlation −0.29); `kDecorr` unchanged.
- **C — halved diff-boost** — current vectors (correlation −0.557), `kDecorr × 0.5`.

## Result (mean / worst across the 9 modes; Hall shown as the reference)

| Config | Mean mono (dB) | Worst mono (dB) | Mean width | Hall mono | Hall width |
|--------|---------------:|----------------:|-----------:|----------:|-----------:|
| Baseline                 | −4.56 | −5.13 | 1.419 | −2.94 | 1.040 |
| A — orthogonal (corr 0)  | −2.43 | −2.84 | 0.908 | −1.43 | 0.663 |
| B — mild (corr −0.29)    | −3.51 | −4.01 | 1.132 | −2.20 | 0.829 |
| C — kDecorr × 0.5        | −3.71 | −4.06 | 1.215 | −2.94 | 1.040 |

(Mono = mono fold-down RMS relative to L; closer to 0 dB = more mono-compatible.
Width = side/mid RMS on the tail; higher = wider.)

## Reading

- **The mono loss is set by the output-vector correlation.** Orthogonalizing the
  vectors (A) is the only candidate that removes the cancellation at its source —
  but it over-corrects: width collapses (mean 1.42 → 0.91, Hall to 0.66), and
  Hall's late correlation flips positive. Too narrow.
- **B (mild, −0.29) is the balanced point.** Worst-case mono −5.1 → −4.0, mean
  −4.6 → −3.5, while width stays wide (1.13). It reduces the anti-correlation at
  the source rather than masking it.
- **C (halve kDecorr) improves the metric (−4.6 → −3.7) but treats a symptom.**
  The diff-boost and width matrix are mono-preserving by construction; C only
  helps because it stops inflating the L channel that the metric divides by. The
  underlying −0.557 vector correlation — the actual cause — is untouched, and
  Hall (whose tail is already near-decorrelated) is unchanged.

## Recommendation

**B (mild output vectors, correlation ≈ −0.29)**, optionally combined with a
small `kDecorr` trim, targets Hall-like mono behavior (~−3.5 dB worst) while
keeping the tail wide. A is too narrow; C leaves the root cause in place. Final
target correlation and whether to also trim `kDecorr` are a voicing call —
this table is here so that call is made against numbers, not guesses. The fix
should ship with a mono-retention regression guard once the target is chosen.
