# Accepted Patterns

Optimizations that measurably improved DSP performance and passed all gates.

## Sinc8Turbo Polyphase Filter Bank

**Where**: `AestraAudio/include/DSP/Interpolators.h` — `Sinc8Turbo` struct
**What**: Precomputed 256-phase × 8-tap polyphase table (8KB). All sin(),
blackman window, and normalization math baked into table at startup.
Hot path is pure table lookup + 8-tap dot product. Half-phase symmetry
(128 phases). Float coefficients, 64-byte aligned.
**CI Results** (GitHub Actions, AVX2, 10 iterations):
- Sinc8 original: 28.63 Mf/s, 0.1% CV
- Sinc8 TURBO: 81.37 Mf/s, 0.3% CV
- **Speedup: 2.84x**
**Round**: 01 (session 001)

## Sinc16Turbo Polyphase Filter Bank

**Where**: `AestraAudio/include/DSP/Interpolators.h` — `Sinc16Turbo` struct
**What**: Precomputed 256-phase × 16-tap polyphase table (8KB).
Half-phase symmetry (128 phases). Float coefficients, 64-byte aligned.
**Initial issue**: 512-phase version (16KB table) showed 7.7% CV.
**Fix**: Reduced to 256 phases (8KB table). CV dropped to 1.2%, speed
unchanged at 45.93 Mf/s.
**CI Results** (GitHub Actions, AVX2, 10 iterations):
- Sinc16 TURBO: 45.93 Mf/s, 1.2% CV, ~120dB SNR
**Round**: 02 (session 002)

## Key Lesson: 8KB Table Size Limit

All TURBO interpolators must cap their table at 8KB. The 16KB Sinc16
table showed 7.7% CV because it competed with sample data for L1 cache
space. Reducing to 8KB fixed the variance without affecting speed or
quality (phase quantization error at 256 phases is ~0.2%, well below
the 16-tap filter's ~120dB noise floor).

## Sinc64 TURBO (Pre-existing, Validated)

**Where**: `AestraAudio/include/DSP/Interpolators.h` — `Sinc64Turbo` struct
**What**: Precomputed 2048-phase × 64-tap polyphase table (256KB).
Half-phase symmetry (1024 phases). SIMD dispatch (AVX2, AVX-512, SSE4.1, NEON).
**CI Results** (GitHub Actions, AVX2, 10 iterations):
- Sinc64 original: 5.97 Mf/s, 0.3% CV
- Sinc64 TURBO: 31.31 Mf/s, 0.1% CV
- **Speedup: 5.24x**
