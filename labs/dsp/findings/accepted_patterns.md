# Accepted Patterns

Optimizations that measurably improved DSP performance and passed all gates.

## Sinc8Turbo Polyphase Filter Bank

**Where**: `AestraAudio/include/DSP/Interpolators.h` — `Sinc8Turbo` struct
**What**: Precomputed 256-phase × 8-tap polyphase table (8KB). All sin(),
kaiser window, and normalization math baked into table at startup.
Hot path is pure table lookup + 8-tap dot product. Half-phase symmetry
exploits sinc's even function property (128 phases, not 256).
Float coefficients, 64-byte aligned for cache-line efficiency.
**Why it works**: Eliminates 8 sin() reductions, 8 divisions, 8 weight
multiplies, and 1 normalization per output sample. The 8KB table fits
comfortably in L1 cache alongside the sample data.
**Results**: 13.74 → 43.46 Mf/s (3.16x speedup).
**Round**: 01 (session 001)

## Sinc16Turbo Polyphase Filter Bank

**Where**: `AestraAudio/include/DSP/Interpolators.h` — `Sinc16Turbo` struct
**What**: Precomputed 512-phase × 16-tap polyphase table (16KB).
Half-phase symmetry (256 phases). Float coefficients, 64-byte aligned.
**Why it works**: Same principle as Sinc8Turbo — zero math in hot path.
~120dB SNR vs Sinc8's ~100dB.
**Results**: 29.35 Mf/s (new, no original to compare against since
Sinc16Interpolator wasn't in the benchmark before).
**Caveat**: 16KB table competes with sample data for L1 cache space.
Lands slower than Sinc8Turbo despite more taps.
**Round**: 02 (session 002)
