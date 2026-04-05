# Bottlenecks

No profiling done yet. The biquad filter is a DSP hot path — improvements
here benefit all audio processing.

## Potential Areas

- Per-sample process() call overhead
- SIMD vectorization of biquad coefficients
- Oversampling stage efficiency
- Parameter smoothing branch prediction
