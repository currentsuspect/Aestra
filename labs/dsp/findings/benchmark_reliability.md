# Benchmark Reliability

## Machine Variance

The benchmark machine has extreme variance that makes absolute performance
numbers unreliable:

| Algorithm | Range (5 runs) | Median | CV |
|-----------|---------------|--------|-----|
| Cubic | 43–65 Mf/s | ~52 | ~20% |
| Sinc8 TURBO | 27–44 Mf/s | ~33 | ~22% |
| Sinc16 TURBO | 10–26 Mf/s | ~19 | ~35% |
| Sinc64 TURBO | 8–13 Mf/s | ~11 | ~20% |

**The ranking is consistent** (Sinc8 > Sinc16 > Sinc64) across all runs.
The ordering is valid. The absolute numbers are not.

## What This Means

- You cannot say "Sinc8 TURBO is 43 Mf/s" — it's 27–44 Mf/s depending on
  system state at the moment of measurement.
- You cannot calculate "X simultaneous clips" from these numbers — the
  headroom estimate would swing wildly between runs.
- Speedup ratios within a single run are more stable than cross-run
  comparisons (e.g., Sinc8 TURBO / Sinc64 TURBO in the same run).

## What Would Fix This

1. **CPU isolation**: `isolcpus=` kernel parameter, pin benchmark to one core
2. **CPU frequency scaling**: `cpufreq-set -g performance`
3. **Kill background processes**: browsers, IDEs, system daemons
4. **20+ iterations**: take median, not mean
5. **Quiet machine**: dedicated benchmark hardware, not a developer workstation

## What Remains Valid

- **Structural correctness**: The polyphase table approach eliminates sin(),
  divide, and normalize from the hot path. This is mathematically true
  regardless of benchmark variance.
- **Relative ordering**: Sinc8 TURBO is always faster than Sinc16 TURBO,
  which is always faster than Sinc64 TURBO. This ranking is consistent.
- **Quality tiers**: ~100dB (Sinc8), ~120dB (Sinc16), ~144dB (Sinc64) are
  determined by filter design, not benchmark conditions.

## What Does NOT Remain Valid

- Any absolute performance claim ("Sinc8 TURBO is 43 Mf/s")
- Any headroom estimate ("handles X simultaneous clips")
- Any comparison to the resampler lab's numbers (different runs, different noise)
