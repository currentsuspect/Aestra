# Reverb Lab — Rejected Patterns

Patterns that were tried and rejected, with reasons.

## Block-Based Processing (vectorizing across samples)

**Status**: Rejected (for now)
**Why**: AestraVerb's `process()` API is sample-by-sample due to parameter smoothing and LFO state updates that have sample-accurate dependencies. Block-based SIMD across time would require major architectural changes and could break modulation accuracy.

## Precomputed Delay-Line Coefficient Tables

**Status**: Rejected
**Why**: Delay lengths change dynamically with Size parameter. Precomputing all possible tables would use excessive memory. The cubic Hermite computation is cheap enough (4 muls + 3 adds) that table lookup isn't worth the cache pressure.

## Double-Precision Internal Processing

**Status**: Rejected
**Why**: Float precision is sufficient for reverb processing. Double would halve SIMD throughput and increase memory bandwidth with no audible quality improvement in this context.
