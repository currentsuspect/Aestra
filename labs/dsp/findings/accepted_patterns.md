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
**Results**: 8.97 Mf/s → 27.40 Mf/s (3.06x speedup).
Speedup is lower than Sinc64Turbo's 4.35x because the 8-tap inner loop
is short enough that table lookup overhead (phase calc, bounds check) is
proportionally larger.
**Round**: 01 (session 001)
