# Invariants

1. **BIBO stability**: Bounded input → bounded output (no NaN/Inf).
2. **Low-pass**: Attenuates frequencies above cutoff.
3. **High-pass**: Attenuates frequencies below cutoff.
4. **Band-pass**: Passes frequencies in the specified band.
5. **Resonance**: Boosts response at cutoff when Q > 0.
6. **Reset**: `reset()` clears all internal state to zero.
7. **Oversampling**: 2x/4x produces cleaner output at high frequencies.
8. **Parameter smoothing**: No clicks during parameter changes.
