# Invariants

1. **Ready semantics**: Build completion flips `isReady()` to true.
2. **Mip growth**: `samplesPerPeak` follows the documented multiplier.
3. **Peak bounds**: Returned peaks bound the source samples they summarize.
4. **Query safety**: Invalid queries return safe zero peaks.
5. **Clear semantics**: `clear()` resets readiness and cached levels.
6. **Concurrent safety**: Reader queries remain safe while rebuilds occur.
7. **Selection behavior**: Level selection must not skip to coarser data that
   breaks visible peak fidelity.
