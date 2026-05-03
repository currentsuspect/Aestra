# Reverb Lab — Bottlenecks

Known performance characteristics of AestraVerb.

## Per-Sample Hot Path

1. **FDN feedback loop** (8 lines): ~35 FLOPs per line = ~280 FLOPs per sample
   - Householder mean: 8 adds + 1 mul
   - Damping: 8 subs + 8 muls + 8 adds
   - Injection: 8 muls + 8 adds + 8 muls
   - Low-damp: 8 subs + 8 muls + 8 adds + 8 muls + 8 subs
   - Output dot: 8 muls + 8 adds
   - **This is the primary SIMD target.**

2. **Delay line reads** (8 reads): Memory-bound, random access
   - Cubic Hermite: 4 memory reads + ~10 FLOPs
   - Not vectorizable across lines (different buffers)
   - But quality improvement justifies the slight extra cost

3. **LFO updates** (8 oscillators): ~16 FLOPs per line
   - Vectorizable across lines (same operation, different data)
   - AVX2 reduces 8 scalar updates to 1 vector op

4. **Early reflections** (12 taps): ~48 memory reads + 24 muls + 24 adds
   - Partially vectorizable with 4-wide operations
   - But early reflections are a small fraction of total cost

## Memory Footprint

- Room mode @ 48kHz: ~8 * 3200 samples ≈ 25.6 KB delay lines
- Hall mode @ 48kHz: ~8 * 5000 samples ≈ 40 KB delay lines
- Predelay: ~24 KB max
- **Total working set**: ~100 KB, fits comfortably in L2 cache

## Callback Budget Target

- 256 samples @ 48kHz = 5.33 ms budget
- Target: < 0.8 ms (15% budget) for a single reverb instance
- This leaves headroom for multiple plugins + mixing
