# DSP Benchmark — CI Strategy

## The Problem

Local benchmark machine has 20-35% CV. Absolute Mf/s numbers are unreliable.
Cannot make performance claims or headroom estimates.

## The Solution

GitHub Actions runners provide:
- **Isolated environment** — clean VM, no background noise
- **Consistent hardware** — same runner type every time
- **AVX2 support** — most runners have AVX2 (this machine doesn't)
- **Reproducible** — every run gets dedicated resources
- **Version-controlled** — results tied to git commit

## Workflow

`.github/workflows/dsp-benchmark.yml`:
- Triggers on DSP code changes OR manual dispatch
- Builds `AestraSincBenchmark` in Release mode
- Runs with `--iterations 10` (configurable)
- Reports results as job summary with markdown table
- Uploads raw JSON as artifact

## Baseline Policy

- First successful CI run establishes baseline
- Subsequent runs compare against baseline
- Flag regressions > 5% (median, not mean)
- Update baseline only on accepted changes

## Caveats

- CI runners have SOME variance (~2-5% typical) — not perfect
- Runner hardware can change over time (GitHub updates runners)
- Truly reliable benchmark requires dedicated bare metal you control
- But CI is good enough for:
  - Confirming tier rankings (Sinc8 > Sinc16 > Sinc64)
  - Detecting regressions > 10%
  - Getting ballpark headroom estimates

## Next Steps

1. ✅ Create workflow file
2. Merge to `develop` → triggers first CI benchmark run
3. Capture first run as baseline
4. Use baseline for future comparisons
